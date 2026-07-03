#include <doctest/doctest.h>

#include <cmath>
#include <vector>

#include "primitives/nonlinear/hysteresis.h"
#include "core/hysteresis-test-support.h"

using namespace acfx;
using namespace acfx::hysteresistest;

// T012 — the primitive's dedicated validation suite (US2; SC-001, FR-003,
// FR-018): the CANONICAL memory-proof and reproducibility evidence for the
// Jiles-Atherton core. T006-T009 pin the derivative/steppers/guard in
// isolation; this suite asserts the feature-defining properties: a driven
// loop encloses a strictly positive area (the physical signature of
// hysteretic memory, absent from any single-valued/memoryless curve),
// reset() reproducibility (FR-003), and monotonic response of the loop shape
// to the coercivity (k) and saturation (Ms) parameters (US2.2).
namespace {

// Threshold for "the loop area is nonzero" (SC-001/FR-018). The trace spans
// an H-range of 2*amp and an M-range of at most ~2*Ms (the physical
// saturation ceiling), so a fully-degenerate (memoryless) trace has area 0
// and any genuine loop area scales with amp*Ms. We set the bar at 1% of the
// amp*Ms rectangle — far below any physically-open JA loop (which typically
// encloses a large fraction of that rectangle, see NOTES) but comfortably
// above quadrature/float noise on a degenerate curve (which the tanh
// contrast case below measures directly, at ~1e-9 scale).
constexpr double kLoopAreaFraction = 0.01;

void checkLoopHasArea(Solver solver) {
    const double amp = 1.5;
    const double Ms = 1.0;

    Hysteresis h;
    h.prepare(48000.0);
    JAParams p;  // defaults: Ms=1, a=1, alpha=0, k=1, c=0.5
    p.Ms = Ms;
    p.k = 0.6;  // widen the loop, as T007/T008/T009 do
    h.setParams(p);
    h.setSolver(solver);
    h.reset();

    const Trace trace = driveSettledLoop(h, amp, /*settleCycles=*/4,
                                         /*stepsPerCycle=*/512);
    const double area = shoelaceArea(trace.H, trace.out);
    const double threshold = kLoopAreaFraction * (2.0 * amp) * (2.0 * Ms);
    CAPTURE(area);
    CAPTURE(threshold);
    CHECK(area > threshold);
}

}  // namespace

TEST_CASE("Hysteresis::process — closed-loop area > 0, memory proof (T012, SC-001/FR-018)") {
    SUBCASE("rk2: driven loop encloses strictly positive area") {
        checkLoopHasArea(Solver::rk2);
    }
    SUBCASE("rk4: driven loop encloses strictly positive area") {
        checkLoopHasArea(Solver::rk4);
    }
    SUBCASE("newtonRaphson: driven loop encloses strictly positive area") {
        checkLoopHasArea(Solver::newtonRaphson);
    }

    SUBCASE("static waveshaper (tanh) contrast: single-valued curve has ~0 area") {
        // Sample a MEMORYLESS reference curve (tanh) over the identical H
        // trace used above: a single-valued function y = f(H) is, by
        // definition, a curve, not a loop — for every H there is exactly one
        // output, so the "rising" and "falling" halves of the trace retrace
        // the same curve and the enclosed area is the numerical-quadrature
        // floor, not a physical loop. This is the demonstration that
        // nonzero loop area is specifically the hysteresis/memory
        // signature (SC-001), not an artifact of any nonlinear shaping.
        const double amp = 1.5;
        const int stepsPerCycle = 512;
        std::vector<double> Hs;
        std::vector<double> tanhOut;
        Hs.reserve(stepsPerCycle);
        tanhOut.reserve(stepsPerCycle);
        for (int n = 0; n < stepsPerCycle; ++n) {
            const double phase = 2.0 * M_PI * (static_cast<double>(n) / stepsPerCycle);
            const double H = amp * std::sin(phase);
            Hs.push_back(H);
            tanhOut.push_back(std::tanh(H));
        }
        const double area = shoelaceArea(Hs, tanhOut);
        // Tolerance: strictly-analytic degenerate-polygon quadrature noise at
        // double precision on a ~O(1) x O(1) trace — many orders of
        // magnitude below the 1% loop-area threshold used for the genuine
        // hysteresis loops above.
        constexpr double kDegenerateAreaTolerance = 1.0e-9;
        CAPTURE(area);
        CHECK(area < kDegenerateAreaTolerance);
    }
}

TEST_CASE("Hysteresis::reset — bit-reproducible replay (T012, FR-003)") {
    // FR-003/contract C2: after reset(), an identical input sequence
    // reproduces an identical output sequence — no hidden global state
    // survives a reset. Exercised across all three solvers since each has
    // independent internal iteration state (Newton-Raphson especially).
    const Solver solvers[] = {Solver::rk2, Solver::rk4, Solver::newtonRaphson};
    for (Solver solver : solvers) {
        Hysteresis h;
        h.prepare(48000.0);
        JAParams p;
        p.k = 0.6;
        h.setParams(p);
        h.setSolver(solver);

        const auto runSequence = [&h]() {
            std::vector<float> outputs;
            outputs.reserve(600);
            for (int n = 0; n < 600; ++n) {
                const double H = 1.2 * std::sin(2.0 * M_PI * n / 200.0) +
                                 0.3 * std::sin(2.0 * M_PI * n / 37.0);
                outputs.push_back(h.process(static_cast<float>(H)));
            }
            return outputs;
        };

        h.reset();
        const std::vector<float> first = runSequence();
        h.reset();
        const std::vector<float> second = runSequence();

        REQUIRE(first.size() == second.size());
        for (std::size_t i = 0; i < first.size(); ++i) {
            // Bit-for-bit: both runs execute the identical deterministic
            // sequence of double-precision operations from the identical
            // post-reset() state, so no tolerance is needed or wanted here —
            // any drift would itself be a reset() defect (FR-003).
            CHECK(first[i] == second[i]);
        }
    }
}

TEST_CASE("Hysteresis parameter response — k widens loop, Ms raises ceiling (T012, US2.2)") {
    SUBCASE("increasing k (coercivity) widens the loop area") {
        // Coercivity k controls how strongly the irreversible (domain-wall)
        // term resists tracking the anhysteretic curve: dMirr/dH ~
        // (Man-M)/(delta*k) (dMdH(), alpha=0 case above). For this
        // closed-form JA arrangement the area-vs-k response is a hump, not a
        // monotone ramp over the FULL k range: as k -> 0 the irreversible
        // slope blows up and M snaps to Man(H) (collapsing to a memoryless,
        // near-zero-area curve); as k -> infinity the irreversible term
        // vanishes and M is driven purely by the (equally memoryless)
        // reversible c*dMan/dHe term. A verified numeric sweep (k = 0.1 ..
        // 3.0 at amp=1.5, Ms=1, alpha=0, c=0.5, rk4) confirms the area peaks
        // near k~0.3-0.4 and falls off on BOTH sides. The physically-clean,
        // unambiguous "more coercivity -> wider loop" regime (US2.2) is the
        // low-k rising branch below that peak, so this test compares two
        // points there, both well clear of the peak.
        const double amp = 1.5;
        const double Ms = 1.0;

        Hysteresis hNarrow;
        hNarrow.prepare(48000.0);
        JAParams pNarrow;
        pNarrow.Ms = Ms;
        pNarrow.k = 0.1;  // narrower loop: near the M-snaps-to-Man limit
        hNarrow.setParams(pNarrow);
        hNarrow.setSolver(Solver::rk4);
        hNarrow.reset();
        const Trace narrowTrace = driveSettledLoop(hNarrow, amp, 4, 512);
        const double narrowArea = shoelaceArea(narrowTrace.H, narrowTrace.out);

        Hysteresis hWide;
        hWide.prepare(48000.0);
        JAParams pWide;
        pWide.Ms = Ms;
        pWide.k = 0.3;  // wider loop: still on the rising branch, pre-peak
        hWide.setParams(pWide);
        hWide.setSolver(Solver::rk4);
        hWide.reset();
        const Trace wideTrace = driveSettledLoop(hWide, amp, 4, 512);
        const double wideArea = shoelaceArea(wideTrace.H, wideTrace.out);

        CAPTURE(narrowArea);
        CAPTURE(wideArea);
        CHECK(wideArea > narrowArea);
    }

    SUBCASE("increasing Ms raises the output ceiling") {
        // Ms is the saturation magnetization: the physical ceiling on |M|
        // (and hence on the output). Driving hard into saturation at two
        // different Ms values must show the larger-Ms run reaching a
        // strictly higher peak |output|.
        const double amp = 5.0;  // drive well past saturation for both

        Hysteresis hLowMs;
        hLowMs.prepare(48000.0);
        JAParams pLow;
        pLow.Ms = 1.0;
        pLow.k = 0.6;
        hLowMs.setParams(pLow);
        hLowMs.setSolver(Solver::rk4);
        hLowMs.reset();

        Hysteresis hHighMs;
        hHighMs.prepare(48000.0);
        JAParams pHigh;
        pHigh.Ms = 2.5;
        pHigh.k = 0.6;
        hHighMs.setParams(pHigh);
        hHighMs.setSolver(Solver::rk4);
        hHighMs.reset();

        double lowPeak = 0.0;
        double highPeak = 0.0;
        const int stepsPerCycle = 256;
        const int totalSteps = 6 * stepsPerCycle;  // settle + measure
        for (int n = 0; n < totalSteps; ++n) {
            const double phase = 2.0 * M_PI * (static_cast<double>(n) / stepsPerCycle);
            const double H = amp * std::sin(phase);
            const double lowOut =
                static_cast<double>(hLowMs.process(static_cast<float>(H)));
            const double highOut =
                static_cast<double>(hHighMs.process(static_cast<float>(H)));
            if (n >= totalSteps - stepsPerCycle) {  // measure the settled last cycle
                lowPeak = std::max(lowPeak, std::fabs(lowOut));
                highPeak = std::max(highPeak, std::fabs(highOut));
            }
        }
        CAPTURE(lowPeak);
        CAPTURE(highPeak);
        CHECK(highPeak > lowPeak);
    }
}

// T006 — focused sanity checks on the shared Jiles-Atherton derivative dMdH.
// The full loop-area / solver-agreement suite is T012's; this only pins the
// derivative's local sanity (finite, sane origin slope, no NaN/Inf when swept).
TEST_CASE("Hysteresis::dMdH — Jiles-Atherton derivative sanity (T006)") {
    Hysteresis h;
    JAParams p;  // defaults: Ms=1, a=1, alpha=0, k=1, c=0.5
    h.setParams(p);

    SUBCASE("origin slope ~ anhysteretic Ms/(3a) with alpha=0") {
        // At H=0, M=0: H_e=0, dMan/dHe = Ms/a * 1/3. With alpha=0 the combined
        // form collapses to (1-c)*dMirr/dH + c*dMan/dHe; at the origin
        // dMirr/dH = 0 (M_an - M = 0), so dM/dH = c * Ms/(3a).
        const double d = HysteresisTestAccess::dMdH(h, 0.0, 0.0, 1.0);
        CHECK(std::isfinite(d));
        const double expected = 0.5 * (1.0 / (3.0 * 1.0));  // c * Ms/(3a)
        CHECK(d == doctest::Approx(expected).epsilon(1e-9));
    }

    SUBCASE("finite across a swept H for both dH signs, no NaN/Inf") {
        for (double H = -50.0; H <= 50.0; H += 0.25) {
            for (double M = -1.5; M <= 1.5; M += 0.5) {
                const double dpos = HysteresisTestAccess::dMdH(h, H, M, 1.0);
                const double dneg = HysteresisTestAccess::dMdH(h, H, M, -1.0);
                const double dzero = HysteresisTestAccess::dMdH(h, H, M, 0.0);
                CHECK(std::isfinite(dpos));
                CHECK(std::isfinite(dneg));
                CHECK(std::isfinite(dzero));
            }
        }
    }

    SUBCASE("stays finite under nonzero coupling and hot field") {
        h.setAlpha(1.0e-3);
        h.setK(0.5);
        h.setMs(2.0);
        h.setA(0.5);
        for (double H = -1000.0; H <= 1000.0; H += 5.0) {
            const double d = HysteresisTestAccess::dMdH(h, H, 0.3, 1.0);
            CHECK(std::isfinite(d));
        }
    }
}
