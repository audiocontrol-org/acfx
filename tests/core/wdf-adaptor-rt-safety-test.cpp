#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <stdexcept>

#include "primitives/circuit/wdf/parallel-adaptor.h"
#include "primitives/circuit/wdf/series-adaptor.h"
#include "support/allocation-sentinel.h"

// wdf-adaptor-rt-safety-test.cpp (T009, US5) -- proves the two properties
// contract G1/C3 promise for SeriesAdaptor / ParallelAdaptor:
//
//   (a) SC-005: the per-sample wave path (reflected()/incident()) over an
//       assembled adaptor TREE performs ZERO heap allocations/deallocations
//       across a large sweep.
//   (b) SC-006: adaptor CONSTRUCTION validates every child's portResistance()
//       and throws std::invalid_argument (no clamping, no fallback) for a
//       negative, zero, NaN, or infinite child Rp -- for both adaptor kinds.
//
// A test-local `Probe` OnePort stands in for a leaf here specifically
// because it does NOT validate its own Rp at construction (unlike the
// shipped Resistor, which would throw at ITS OWN construction before the
// adaptor ever got a chance to). Probe lets a bad Rp reach the adaptor's
// constructor so the adaptor's own validatePortResistances() is what fires.

using namespace acfx::wdf;
using acfx::test::AllocationSentinel;

namespace {

// Probe -- a minimal test-only OnePort. Rp and the up-sweep reflected wave
// are both freely settable (no validation), so the adaptor under test is the
// only thing enforcing C3. lastIncident is mutable-observed state from the
// down-sweep (not read by these tests, but kept so Probe fully satisfies the
// OnePort concept's per-sample call shape without a dummy no-op incident()).
struct Probe {
    double Rp;
    double refl;
    mutable double lastIncident = 0.0;

    static constexpr bool isAdaptable = true;

    explicit Probe(double RpIn, double reflIn = 0.0) noexcept : Rp(RpIn), refl(reflIn) {}

    double portResistance() const noexcept { return Rp; }
    double reflected() const noexcept { return refl; }
    void incident(double a) noexcept { lastIncident = a; }
};

}  // namespace

// -----------------------------------------------------------------------------
// SC-005 -- zero-heap wave path over a NESTED adaptor tree.
// -----------------------------------------------------------------------------

TEST_CASE("wdf adaptor rt-safety: zero-heap per-sample sweep over a nested Series/Parallel tree") {
    // Tree built OUTSIDE the measured window: SeriesAdaptor<Probe,
    // ParallelAdaptor<Probe, Probe>> with valid (positive, finite) Rp on
    // every probe -- construction validation is exercised separately below.
    using Inner = ParallelAdaptor<Probe, Probe>;
    using Tree = SeriesAdaptor<Probe, Inner>;

    Tree tree(Probe(100.0), Inner(Probe(50.0), Probe(75.0)));

    AllocationSentinel::reset();
    for (int n = 0; n < 100000; ++n) {
        const double b = tree.reflected();          // up-sweep
        tree.incident(0.5 * b + 0.01 * n);           // down-sweep, varying value
    }
    const std::size_t allocations = AllocationSentinel::allocations();
    const std::size_t deallocations = AllocationSentinel::deallocations();

    CHECK_MESSAGE(allocations == 0, "nested adaptor tree wave path allocated ", allocations, " times");
    CHECK_MESSAGE(deallocations == 0, "nested adaptor tree wave path deallocated ", deallocations,
                  " times");
}

// -----------------------------------------------------------------------------
// SC-006 -- construction validation throws std::invalid_argument, no clamp.
// -----------------------------------------------------------------------------

TEST_CASE("wdf adaptor rt-safety: SeriesAdaptor construction throws std::invalid_argument for invalid child Rp") {
    // A type alias (rather than the bare template-id) keeps the internal
    // comma out of the CHECK_THROWS_AS macro invocation below -- the
    // preprocessor would otherwise split "SeriesAdaptor<Probe, Probe>(...)"
    // into two macro arguments at that comma.
    using SP = SeriesAdaptor<Probe, Probe>;

    CHECK_THROWS_AS(SP(Probe(100.0), Probe(-1.0)), std::invalid_argument);
    CHECK_THROWS_AS(SP(Probe(100.0), Probe(0.0)), std::invalid_argument);
    CHECK_THROWS_AS(SP(Probe(100.0), Probe(std::nan(""))), std::invalid_argument);
    CHECK_THROWS_AS(SP(Probe(100.0), Probe(std::numeric_limits<double>::infinity())),
                    std::invalid_argument);

    // A valid all-positive construction must NOT throw.
    CHECK_NOTHROW(SP(Probe(100.0), Probe(200.0)));
}

TEST_CASE("wdf adaptor rt-safety: ParallelAdaptor construction throws std::invalid_argument for invalid child Rp") {
    using PP = ParallelAdaptor<Probe, Probe>;

    CHECK_THROWS_AS(PP(Probe(100.0), Probe(-1.0)), std::invalid_argument);
    CHECK_THROWS_AS(PP(Probe(100.0), Probe(0.0)), std::invalid_argument);
    CHECK_THROWS_AS(PP(Probe(100.0), Probe(std::nan(""))), std::invalid_argument);
    CHECK_THROWS_AS(PP(Probe(100.0), Probe(std::numeric_limits<double>::infinity())),
                    std::invalid_argument);

    // A valid all-positive construction must NOT throw.
    CHECK_NOTHROW(PP(Probe(100.0), Probe(200.0)));
}

// -----------------------------------------------------------------------------
// research R7 -- empty child set is a COMPILE-TIME error (arity static_assert,
// C2.1), not a runtime one. Left commented out deliberately: uncommenting
// either line fails the build with "SeriesAdaptor/ParallelAdaptor requires
// at least one child (C2.1)." There is nothing to CHECK_THROWS at runtime
// here -- the empty-pack case never reaches a constructor body.
//
// SeriesAdaptor<> emptySeries;
// ParallelAdaptor<> emptyParallel;
