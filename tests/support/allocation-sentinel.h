#pragma once

#include <cstddef>

// A thread-local heap-allocation counter used by the no-allocation invariant test
// (FR-014, research.md decision 6). The global operator new/delete overrides live
// in allocation-sentinel.cpp and bump these counters. Test-only support code.

namespace acfx::test {

struct AllocationSentinel {
    // Zero the counters (call immediately before a measured region).
    static void reset() noexcept;
    // Number of heap allocations on this thread since the last reset().
    static std::size_t allocations() noexcept;
    // Number of heap deallocations on this thread since the last reset().
    static std::size_t deallocations() noexcept;
};

} // namespace acfx::test
