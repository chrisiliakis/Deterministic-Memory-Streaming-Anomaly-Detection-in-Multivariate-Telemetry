#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>

namespace stream_guard {

template <typename T, size_t Capacity>
requires (std::is_trivially_copyable_v<T> && (Capacity > 1) && ((Capacity & (Capacity - 1)) == 0))
class SpscRingBuffer {
private:
    static constexpr size_t kMask = Capacity - 1;

    alignas(64) std::atomic<size_t> write_idx_{0};
    alignas(64) size_t cached_read_idx_{0};

    alignas(64) std::atomic<size_t> read_idx_{0};
    alignas(64) size_t cached_write_idx_{0};

    alignas(64) std::array<T, Capacity> buffer_{};

public:
    constexpr SpscRingBuffer() noexcept = default;

    SpscRingBuffer(const SpscRingBuffer&) = delete;
    SpscRingBuffer& operator=(const SpscRingBuffer&) = delete;

    [[nodiscard]] bool try_push(const T& item) noexcept {
        const size_t w = write_idx_.load(std::memory_order_relaxed);
        if (w - cached_read_idx_ == Capacity) {
            cached_read_idx_ = read_idx_.load(std::memory_order_acquire);
            if (w - cached_read_idx_ == Capacity) {
                return false; // Ring buffer is full
            }
        }
        buffer_[w & kMask] = item;
        write_idx_.store(w + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool try_pop(T& out_item) noexcept {
        const size_t r = read_idx_.load(std::memory_order_relaxed);
        if (cached_write_idx_ == r) {
            cached_write_idx_ = write_idx_.load(std::memory_order_acquire);
            if (cached_write_idx_ == r) {
                return false; // Ring buffer is empty
            }
        }
        out_item = buffer_[r & kMask];
        read_idx_.store(r + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] size_t size_approx() const noexcept {
        const size_t w = write_idx_.load(std::memory_order_acquire);
        const size_t r = read_idx_.load(std::memory_order_acquire);
        return w - r;
    }
};

} // namespace stream_guard