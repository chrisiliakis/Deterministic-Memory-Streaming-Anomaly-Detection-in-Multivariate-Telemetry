#pragma once

#include <array>
#include <cstddef>

namespace stream_guard {

template <size_t Channels, size_t ShingleSize, size_t Stride = 1>
class DelayEmbedder {
    static_assert(Channels > 0 && ShingleSize > 0 && Stride > 0);

public:
    static constexpr size_t kEmbeddedDim = Channels * ShingleSize;
    static constexpr size_t kHistorySpan = (ShingleSize - 1) * Stride + 1;
    using InputVector = std::array<float, Channels>;
    using EmbeddedVector = std::array<float, kEmbeddedDim>;

private:
    std::array<InputVector, kHistorySpan> history_{};
    size_t head_{0};
    size_t count_{0};

public:
    constexpr DelayEmbedder() noexcept = default;

    constexpr void reset() noexcept {
        head_ = 0;
        count_ = 0;
    }

    [[nodiscard]] constexpr bool push(const InputVector& sample, EmbeddedVector& out_shingle) noexcept {
        history_[head_] = sample;
        if (count_ < kHistorySpan) {
            ++count_;
        }

        const bool ready = (count_ == kHistorySpan);
        if (ready) {
            size_t out_idx = 0;
            for (size_t s = 0; s < ShingleSize; ++s) {
                const size_t hist_pos = (head_ + kHistorySpan - (s * Stride)) % kHistorySpan;
                for (size_t c = 0; c < Channels; ++c) {
                    out_shingle[out_idx++] = history_[hist_pos][c];
                }
            }
        }

        head_ = (head_ + 1) % kHistorySpan;
        return ready;
    }

    [[nodiscard]] constexpr bool is_ready() const noexcept { return count_ == kHistorySpan; }
};

} // namespace stream_guard