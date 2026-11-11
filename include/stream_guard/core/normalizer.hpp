#pragma once

#include <array>
#include <cmath>
#include <cstddef>

namespace stream_guard {

template <size_t Channels>
class OnlineNormalizer {
private:
    std::array<double, Channels> mean_{};
    std::array<double, Channels> m2_{};
    size_t count_{0};
    bool freeze_after_warmup_{false};
    size_t warmup_samples_{256};

public:
    explicit constexpr OnlineNormalizer(size_t warmup_samples = 256, bool freeze = false) noexcept
        : freeze_after_warmup_(freeze), warmup_samples_(warmup_samples) {}

    constexpr void reset() noexcept {
        mean_.fill(0.0);
        m2_.fill(0.0);
        count_ = 0;
    }

    [[nodiscard]] constexpr std::array<float, Channels> normalize(
        const std::array<float, Channels>& raw) noexcept {
        if (!freeze_after_warmup_ || count_ < warmup_samples_) {
            ++count_;
            const double inv_n = 1.0 / static_cast<double>(count_);
            for (size_t i = 0; i < Channels; ++i) {
                const double x = static_cast<double>(raw[i]);
                const double delta = x - mean_[i];
                mean_[i] += delta * inv_n;
                const double delta2 = x - mean_[i];
                m2_[i] += delta * delta2;
            }
        }

        std::array<float, Channels> out{};
        if (count_ < 2) {
            return out;
        }
        const double denom = static_cast<double>(
            (freeze_after_warmup_ && count_ >= warmup_samples_) ? warmup_samples_ - 1 : count_ - 1);
        for (size_t i = 0; i < Channels; ++i) {
            const double variance = m2_[i] / denom;
            const double std_dev = std::sqrt(variance + 1e-8);
            out[i] = static_cast<float>((static_cast<double>(raw[i]) - mean_[i]) / std_dev);
        }
        return out;
    }
};

} // namespace stream_guard