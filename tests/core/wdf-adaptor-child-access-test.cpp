#include <doctest/doctest.h>

#include <cmath>
#include <type_traits>

#include "primitives/circuit/wdf/one-port.h"
#include "primitives/circuit/wdf/series-adaptor.h"
#include "primitives/circuit/wdf/parallel-adaptor.h"
#include "primitives/circuit/wdf/wave-elements.h"
#include "primitives/circuit/wdf/wave-sources.h"

// WDF adaptor child-access suite (wdf-adaptors feature, US6/T010).
//
// Validation-only: child<I>() (the composition seam used to assemble and
// inspect adaptor trees) already exists on both SeriesAdaptor and
// ParallelAdaptor. This file proves three things about it:
//
//   1. child<I>() returns a reference to the EXACT static type of the owned
//      child (no type erasure / no slicing) -- checked with a static_assert
//      against std::is_same_v on the decayed return type.
//   2. child<I>() reaches the SAME owned object the up-sweep (reflected())
//      reads: mutating a nested ResistiveVoltageSource's drive voltage
//      through the accessor changes a subsequent reflected() call, proving
//      by-value tuple ownership + observable mutation (not a copy).
//   3. The const overload compiles and is usable from a const reference.
//
// Oracle (series adaptor, contract C4): b_u = -Sum_k a_k, where a_k is each
// child's reflected() wave. A Resistor's reflected() is always 0 (leaf R2);
// a ResistiveVoltageSource's reflected() equals its current drive voltage E
// (leaf VS1).

using acfx::wdf::ParallelAdaptor;
using acfx::wdf::Resistor;
using acfx::wdf::ResistiveVoltageSource;
using acfx::wdf::SeriesAdaptor;

TEST_CASE("wdf adaptor child access reaches the exact owned child type") {
    SeriesAdaptor<Resistor, ResistiveVoltageSource> adaptor(
        Resistor(1000.0), ResistiveVoltageSource(1000.0, 0.0));

    // child<0>() is the Resistor; child<1>() is the ResistiveVoltageSource.
    // Assert the FULL reference-qualified return type (not std::decay_t, which would
    // strip & and const and let a by-value or const-incorrect accessor pass): the
    // non-const accessor must return a mutable lvalue reference to the exact type.
    static_assert(std::is_same_v<decltype(adaptor.child<0>()), Resistor&>,
                  "child<0>() must return exactly Resistor& (mutable ref, no erasure/slicing)");
    static_assert(
        std::is_same_v<decltype(adaptor.child<1>()), ResistiveVoltageSource&>,
        "child<1>() must return exactly ResistiveVoltageSource& (mutable ref, no erasure/slicing)");

    CHECK(adaptor.child<0>().portResistance() == doctest::Approx(1000.0));
    CHECK(adaptor.child<1>().portResistance() == doctest::Approx(1000.0));

    // const overload: readable through a const reference. Assert const-PRESERVATION —
    // the const accessor must return `const Resistor&`, not a mutable ref or a by-value
    // copy (AUDIT-BARRAGE-codex-01/claude-01: std::decay_t would hide such a regression).
    const auto& cref = adaptor;
    static_assert(
        std::is_same_v<decltype(cref.child<0>()), const Resistor&>,
        "const child<0>() must return exactly const Resistor& (const-preserving reference)");
    CHECK(cref.child<0>().portResistance() == doctest::Approx(1000.0));

    // Up-sweep BEFORE mutation: b_u = -(Resistor.reflected() + source.reflected())
    //                              = -(0.0 + 0.0) = 0.0.
    const double before = adaptor.reflected();
    CHECK(std::abs(before - 0.0) < 1e-15);

    // Mutate the nested source THROUGH the accessor.
    adaptor.child<1>().setVoltage(2.0);

    // Up-sweep AFTER mutation: b_u = -(0.0 + 2.0) = -2.0 -- this only holds if
    // child<1>() returned a reference to the SAME object the sweep reads, not
    // a copy.
    const double after = adaptor.reflected();
    CHECK(std::abs(after - (-2.0)) < 1e-15);
}

TEST_CASE("wdf adaptor child access reaches a nested child through two levels") {
    // Outer series adaptor: child<0>() is a plain Resistor, child<1>() is a
    // ParallelAdaptor<Resistor, ResistiveVoltageSource> -- the source is
    // reached via adaptor.child<1>().child<1>().
    using Inner = ParallelAdaptor<Resistor, ResistiveVoltageSource>;
    SeriesAdaptor<Resistor, Inner> adaptor(
        Resistor(500.0), Inner(Resistor(1000.0), ResistiveVoltageSource(1000.0, 0.0)));

    static_assert(std::is_same_v<std::decay_t<decltype(adaptor.child<1>())>, Inner>,
                  "child<1>() must return exactly the Inner ParallelAdaptor type");
    static_assert(
        std::is_same_v<std::decay_t<decltype(adaptor.child<1>().child<1>())>,
                       ResistiveVoltageSource>,
        "nested child<1>().child<1>() must return exactly ResistiveVoltageSource");

    CHECK(adaptor.child<1>().child<1>().portResistance() == doctest::Approx(1000.0));

    // Oracle: inner parallel reflected() = coeff0*a0 + coeff1*a1 with both
    // children at R=1000 => coeff0 == coeff1 == 0.5, a0 = Resistor.reflected()
    // == 0, a1 = source.reflected() == E. Outer series reflected() =
    // -(outerResistor.reflected() + inner.reflected()) = -(0 + inner.reflected()).
    const double before = adaptor.reflected();
    CHECK(std::abs(before - 0.0) < 1e-15);

    adaptor.child<1>().child<1>().setVoltage(2.0);

    // inner.reflected() becomes 0.5*0 + 0.5*2.0 = 1.0, so outer becomes -1.0.
    const double after = adaptor.reflected();
    CHECK(std::abs(after - (-1.0)) < 1e-15);
}
