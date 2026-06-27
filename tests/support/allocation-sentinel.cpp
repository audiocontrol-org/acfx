#include "allocation-sentinel.h"

#include <cstdlib>
#include <new>

// Global operator new/delete overrides that count heap traffic per thread. Kept
// in one translation unit so the counters and the overrides share linkage. The
// counters are thread-local: the test resets them on the audio-processing thread
// and asserts zero across a prepare-then-N-process sequence (FR-014).

namespace {
thread_local std::size_t g_allocations = 0;
thread_local std::size_t g_deallocations = 0;
} // namespace

namespace acfx::test {

void AllocationSentinel::reset() noexcept {
    g_allocations = 0;
    g_deallocations = 0;
}
std::size_t AllocationSentinel::allocations() noexcept { return g_allocations; }
std::size_t AllocationSentinel::deallocations() noexcept { return g_deallocations; }

} // namespace acfx::test

void* operator new(std::size_t size) {
    ++g_allocations;
    if (void* p = std::malloc(size != 0 ? size : 1))
        return p;
    throw std::bad_alloc();
}

void* operator new[](std::size_t size) {
    ++g_allocations;
    if (void* p = std::malloc(size != 0 ? size : 1))
        return p;
    throw std::bad_alloc();
}

void operator delete(void* p) noexcept {
    if (p) {
        ++g_deallocations;
        std::free(p);
    }
}
void operator delete[](void* p) noexcept {
    if (p) {
        ++g_deallocations;
        std::free(p);
    }
}
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete[](p); }
