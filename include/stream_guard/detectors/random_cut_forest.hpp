#pragma once

#include "stream_guard/core/delay_embedder.hpp"
#include "stream_guard/core/types.hpp"
#include "stream_guard/detectors/random_cut_tree.hpp"
#include "stream_guard/sketches/reservoir.hpp"
#include "stream_guard/sketches/tdigest.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <random>

namespace stream_guard {

template <
    size_t Channels,
    size_t ShingleSize = 4,
    size_t NumTrees = 32,
    size_t WindowSize = 256
>
class RandomCutForest {
public:
    static constexpr size_t kEmbeddedDim = Channels * ShingleSize;
    using Frame = TelemetryFrame<Channels>;
    using Tree = RandomCutTree<kEmbeddedDim, WindowSize>;

private:
    DelayEmbedder<Channels, ShingleSize> embedder_{};
    SlidingWindowSampler<WindowSize> window_sampler_{};
    std::array<Tree, NumTrees> trees_{};
    std::array<std::array<uint32_t, WindowSize>, NumTrees> slot_leaves_{};
    TDigest<128, 256> score_digest_{80.0};
    std::mt19937_64 rng_;
    double quantile_cutoff_{0.995};

public:
    explicit RandomCutForest(uint64_t seed = 42ULL, double quantile_cutoff = 0.995) noexcept
        : rng_(seed), quantile_cutoff_(quantile_cutoff) {
        for (auto& tree_slots : slot_leaves_) {
            tree_slots.fill(kNullIdx);
        }
    }

    [[nodiscard]] AnomalyResult process_frame(const Frame& frame) noexcept {
        AnomalyResult result{};
        result.sequence_id = frame.sequence_id;

        typename DelayEmbedder<Channels, ShingleSize>::EmbeddedVector shingle{};
        if (!embedder_.push(frame.channels, shingle)) {
            return result;
        }

        const auto slot = window_sampler_.next_slot();
        float total_codisp = 0.0f;

        for (size_t t = 0; t < NumTrees; ++t) {
            if (slot.should_evict) {
                const uint32_t old_leaf = slot_leaves_[t][slot.slot_index];
                trees_[t].delete_point(old_leaf);
            }
            const uint32_t new_leaf = trees_[t].insert_point(shingle, rng_);
            slot_leaves_[t][slot.slot_index] = new_leaf;
            total_codisp += trees_[t].codisp(new_leaf);
        }

        const size_t active = window_sampler_.active_count();
        const float fill_scale = static_cast<float>(WindowSize) / static_cast<float>(active);
        const float mean_codisp = (total_codisp / static_cast<float>(NumTrees)) * fill_scale;

        result.codisp_score = mean_codisp;
        result.is_warmed_up = window_sampler_.is_full();

        if (result.is_warmed_up) {
            const float thresh = score_digest_.quantile(quantile_cutoff_);
            result.adaptive_threshold = std::max(thresh, 6.0f);
            result.is_anomaly = (mean_codisp > result.adaptive_threshold);

            if (!result.is_anomaly) {
                score_digest_.add(mean_codisp);
            }
        } else if (active >= WindowSize / 2) {
            score_digest_.add(mean_codisp);
        }

        return result;
    }
};

} // namespace stream_guard