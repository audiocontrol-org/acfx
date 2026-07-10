#include <doctest/doctest.h>
#include "support/allocation-sentinel.h"

// WDF real-time safety suite (wdf-primitives feature, T008).
//
// Covers zero-heap invariant during reflected()/incident() operations,
// absence of allocations in hot paths, deterministic behavior under RT
// constraints, and AllocationSentinel verification.
// Test cases added by T008+.
