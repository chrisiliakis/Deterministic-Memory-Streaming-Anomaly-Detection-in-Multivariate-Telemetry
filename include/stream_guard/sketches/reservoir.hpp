#pragma once

#include <cstddef>
#include <cstdint>
#include <random>

namespace stream_guard {

template <size_t WindowSize>
class SlidingWindowSampler {
    static_assert(WindowSize > 0);

private:
    size_t cursor_{0};
    size_t active_{0};

public:
    struct SlotDecision {
        bool should_insert{true};
        bool should_evict{false};
        size_t slot_index{0};
    };

    constexpr SlidingWindowSampler() noexcept = default;

    constexpr void reset() noexcept {
        cursor_ = 0;
        active_ = 0;
    }

    [[nodiscard]] constexpr SlotDecision next_slot() noexcept {
        const bool evict = (active_ == WindowSize);
        const size_t idx = cursor_;
        cursor_ = (cursor_ + 1) % WindowSize;
        if (active_ < WindowSize) {
            ++active_;
        }
        return SlotDecision{true, evict, idx};
    }

    [[nodiscard]] constexpr size_t active_count() const noexcept { return active_; }
    [[nodiscard]] constexpr bool is_full() const noexcept { return active_ == WindowSize; }
};

template <size_t Capacity>
class ReservoirSampler {
    static_assert(Capacity > 0);

private:
    uint64_t stream_count_{0};

public:
    struct SampleDecision {
        bool accepted{false};
        bool evicted{false};
        size_t slot_index{0};
    };

    constexpr ReservoirSampler() noexcept = default;

    constexpr void reset() noexcept { stream_count_ = 0; }

    template <typename RNG>
    [[nodiscard]] SampleDecision offer(RNG& rng) noexcept {
        ++stream_count_;
        if (stream_count_ <= Capacity) {
            return SampleDecision{true, false, static_cast<size_t>(stream_count_ - 1)};
        }
        std::uniform_int_distribution<uint64_t> dist(0, stream_count_ - 1);
        const uint64_t j = dist(rng);
        if (j < Capacity) {
            return SampleDecision{true, true, static_cast<size_t>(j)};
        }
        return SampleDecision{false, false, 0};
    }

    [[nodiscard]] constexpr uint64_t stream_count() const noexcept { return stream_count_; }
    [[nodiscard]] constexpr size_t active_count() const noexcept {
        return stream_count_ < Capacity ? static_cast<size_t>(stream_count_) : Capacity;
    }
};

} // namespace stream_guard