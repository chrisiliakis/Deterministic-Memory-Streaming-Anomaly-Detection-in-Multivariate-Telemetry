#pragma once

#include <atomic>
#include <cstdlib>
#include <new>

namespace stream_guard::testing {

inline std::atomic<size_t> g_heap_allocations{0};
inline thread_local bool g_DisallowAllocations = false;

class ScopedAllocGuard {
    size_t start_allocs_{0};

public:
    ScopedAllocGuard() noexcept : start_allocs_(g_heap_allocations.load()) {
        g_DisallowAllocations = true;
    }
    ~ScopedAllocGuard() noexcept {
        g_DisallowAllocations = false;
    }
    [[nodiscard]] size_t allocations_in_scope() const noexcept {
        return g_heap_allocations.load() - start_allocs_;
    }
};

} // namespace stream_guard::testing

void* operator new(std::size_t size) {
    if (stream_guard::testing::g_DisallowAllocations) {
        stream_guard::testing::g_heap_allocations.fetch_add(1, std::memory_order_relaxed);
    }
    if (void* ptr = std::malloc(size)) return ptr;
    throw std::bad_alloc{};
}

void operator delete(void* ptr) noexcept { std::free(ptr); }
void operator delete(void* ptr, std::size_t) noexcept { std::free(ptr); }