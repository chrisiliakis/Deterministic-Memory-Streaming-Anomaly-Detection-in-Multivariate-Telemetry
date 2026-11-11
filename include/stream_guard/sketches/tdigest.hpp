#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>

namespace stream_guard {

template <size_t MaxCentroids = 128, size_t BufferSize = 256>
class TDigest {
    static_assert(MaxCentroids >= 16 && BufferSize >= 32);

public:
    struct Centroid {
        double mean{0.0};
        double weight{0.0};
    };

private:
    std::array<Centroid, MaxCentroids> centroids_{};
    std::array<Centroid, MaxCentroids> scratch_{};
    std::array<Centroid, MaxCentroids + BufferSize> work_{};
    std::array<float, BufferSize> unmerged_{};
    size_t num_centroids_{0};
    size_t num_unmerged_{0};
    double total_weight_{0.0};
    double min_val_{1e300};
    double max_val_{-1e300};
    double compression_{80.0};

    [[nodiscard]] static double k_scale(double q, double compression) noexcept {
        return (compression / (2.0 * std::numbers::pi)) * std::asin(2.0 * q - 1.0);
    }

    void compress() noexcept {
        if (num_unmerged_ == 0) return;

        size_t total_items = 0;
        double decayed_existing_weight = 0.0;
        for (size_t i = 0; i < num_centroids_; ++i) {
            Centroid c = centroids_[i];
            c.weight *= 0.995;
            decayed_existing_weight += c.weight;
            work_[total_items++] = c;
        }

        for (size_t i = 0; i < num_unmerged_; ++i) {
            const double v = static_cast<double>(unmerged_[i]);
            if (v < min_val_) min_val_ = v;
            if (v > max_val_) max_val_ = v;
            work_[total_items++] = Centroid{v, 1.0};
        }

        const double new_weight = decayed_existing_weight + static_cast<double>(num_unmerged_);
        num_unmerged_ = 0;

        std::sort(work_.begin(), work_.begin() + static_cast<std::ptrdiff_t>(total_items),
                  [](const Centroid& a, const Centroid& b) { return a.mean < b.mean; });

        scratch_[0] = work_[0];
        size_t out_idx = 0;
        double weight_so_far = 0.0;

        for (size_t i = 1; i < total_items; ++i) {
            const double proposed = scratch_[out_idx].weight + work_[i].weight;
            const double q0 = weight_so_far / new_weight;
            const double q1 = (weight_so_far + proposed) / new_weight;

            if ((k_scale(q1, compression_) - k_scale(q0, compression_) <= 1.0) ||
                (out_idx == MaxCentroids - 1)) {
                scratch_[out_idx].mean =
                    (scratch_[out_idx].mean * scratch_[out_idx].weight + work_[i].mean * work_[i].weight) /
                    proposed;
                scratch_[out_idx].weight = proposed;
            } else {
                weight_so_far += scratch_[out_idx].weight;
                ++out_idx;
                scratch_[out_idx] = work_[i];
            }
        }

        num_centroids_ = out_idx + 1;
        for (size_t i = 0; i < num_centroids_; ++i) {
            centroids_[i] = scratch_[i];
        }
        total_weight_ = new_weight;
    }

public:
    explicit constexpr TDigest(double compression = 80.0) noexcept : compression_(compression) {}

    void add(float value) noexcept {
        if (std::isnan(value) || std::isinf(value)) return;
        unmerged_[num_unmerged_++] = value;
        if (num_unmerged_ == BufferSize) {
            compress();
        }
    }

    [[nodiscard]] float quantile(double q) noexcept {
        if (num_unmerged_ > 0) {
            compress();
        }
        if (num_centroids_ == 0) return 0.0f;
        if (q <= 0.0) return static_cast<float>(min_val_);
        if (q >= 1.0) return static_cast<float>(max_val_);
        if (num_centroids_ == 1) return static_cast<float>(centroids_[0].mean);

        const double index = q * total_weight_;
        if (index < centroids_[0].weight / 2.0) {
            return static_cast<float>(min_val_ + (2.0 * index / centroids_[0].weight) *
                                                     (centroids_[0].mean - min_val_));
        }

        double weight_so_far = centroids_[0].weight / 2.0;
        for (size_t i = 0; i < num_centroids_ - 1; ++i) {
            const double dw = (centroids_[i].weight + centroids_[i + 1].weight) / 2.0;
            if (weight_so_far + dw > index) {
                const double z1 = index - weight_so_far;
                const double z2 = weight_so_far + dw - index;
                return static_cast<float>((centroids_[i].mean * z2 + centroids_[i + 1].mean * z1) / dw);
            }
            weight_so_far += dw;
        }

        const double last_w = centroids_[num_centroids_ - 1].weight;
        const double z1 = index - total_weight_ + last_w / 2.0;
        const double z2 = last_w / 2.0 - z1;
        return static_cast<float>((centroids_[num_centroids_ - 1].mean * z2 + max_val_ * z1) /
                                  (last_w / 2.0));
    }

    [[nodiscard]] size_t count() const noexcept {
        return static_cast<size_t>(total_weight_) + num_unmerged_;
    }
};

} // namespace stream_guard