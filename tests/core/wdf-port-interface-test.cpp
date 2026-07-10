#include <doctest/doctest.h>

// WDF port interface contract suite (wdf-primitives feature, T002).
//
// Covers the duck-typed OnePort interface: portResistance() read accessor,
// incident(a) absorption contract, reflected() return value contract, and the
// isAdaptable trait. Test cases added by T003+.
