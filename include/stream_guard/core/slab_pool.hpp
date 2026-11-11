#pragma once

#include "stream_guard/core/types.hpp"
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

namespace stream_guard {

template <typename NodeT, size_t MaxNodes>
class FixedSlabPool {
    static_assert(MaxNodes > 0 && MaxNodes < kNullIdx, "Invalid slab pool capacity");

private:
    alignas(64) std::array<NodeT, MaxNodes> storage_{};
    std::array<uint32_t, MaxNodes> next_free_{};
    uint32_t free_head_{0};
    uint32_t active_count_{0};

public:
    constexpr FixedSlabPool() noexcept { reset(); }

    constexpr void reset() noexcept {
        for (uint32_t i = 0; i < static_cast<uint32_t>(MaxNodes - 1); ++i) {
            next_free_[i] = i + 1U;
        }
        next_free_[MaxNodes - 1] = kNullIdx;
        free_head_ = 0U;
        active_count_ = 0U;
    }

    [[nodiscard]] constexpr uint32_t allocate() noexcept {
        if (free_head_ == kNullIdx) {
            return kNullIdx;
        }
        const uint32_t idx = free_head_;
        free_head_ = next_free_[idx];
        ++active_count_;
        storage_[idx] = NodeT{};
        return idx;
    }

    constexpr void deallocate(uint32_t idx) noexcept {
        assert(idx < MaxNodes && "Out-of-bounds slab index deallocation");
        next_free_[idx] = free_head_;
        free_head_ = idx;
        --active_count_;
    }

    [[nodiscard]] constexpr uint32_t active_count() const noexcept { return active_count_; }
    [[nodiscard]] constexpr size_t capacity() const noexcept { return MaxNodes; }

    [[nodiscard]] constexpr NodeT& operator[](uint32_t idx) noexcept {
        assert(idx < MaxNodes);
        return storage_[idx];
    }

    [[nodiscard]] constexpr const NodeT& operator[](uint32_t idx) const noexcept {
        assert(idx < MaxNodes);
        return storage_[idx];
    }
};

} // namespace stream_guard