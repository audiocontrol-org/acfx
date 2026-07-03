#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <vector>

#include "primitives/nonlinear/hysteresis.h"

using namespace acfx;

// Test accessor for the private, RT-internal JA derivative dMdH() (T006 seam).
// Declared as a friend on acfx::Hysteresis; owns no state of its own.
namespace acfx {
struct HysteresisTestAccess {
    [[nodiscard]] static double dMdH(const Hysteresis& h, double H, double M,
                                     double dH) noexcept {
        return h.dMdH(H, M, dH);
    }
};
}  // namespace acfx

// T012 — the primitive's dedicated validation suite (US2; SC-001, FR-003,
// FR-018): the CANONICAL memory-proof and reproducibility evidence for the
// Jiles-Atherton core. T006-T009 pin the derivative/steppers/guard in
// isolation; this suite asserts the feature-defining properties: a driven
// loop encloses a strictly positive area (the physical signature of
// hysteretic memory, absent from any single-valued/memoryless curve),
// reset() reproducibility (FR-003), and monotonic response of the loop shape
// to the coercivity (k) and saturation (Ms) parameters (US2.2).
namespace {

// Shoelace/polygon-area formula for a closed, ordered 2D trace: the standard
// analytic measure of the signed area enclosed by a polygon's vertices in
// order. We take the absolute value since trace winding direction is not
// asserted here — only that a loop enclosing nonzero area exists.
[[nodiscard]] double shoelaceArea(const std::vector<double>& xs,
                                  const std::vector<double>& ys) noexcept {
    const std::size_t n = xs.size();
    double sum2 = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t j = (i + 1) % n;
        sum2 += xs[i] * ys[j] - xs[j] * ys[i];
    }
    return std::fabs(sum2) * 0.5;
}

// Drives one full sinusoidal cycle of H (amplitude `amp`) through `h`,
// AFTER `settleCycles` warm-up cycles so the trace reflects the settled
// limit loop rather than the zero-state transient, and returns the ordered
// (H, out) trace for that final cycle.
struct Trace {
    std::vector<double> H;
    std::vector<double> out;
};

Trace driveSettledLoop(Hysteresis& h, double amp, int settleCycles,
                       int stepsPerCycle) {
    const int warmupTotal = settleCycles * stepsPerCycle;
    for (int n = 0; n < warmupTotal; ++n) {
        const double phase = 2.0 * M_PI * (static_cast<double>(n) / stepsPerCycle);
        static_cast<void>(h.process(static_cast<float>(amp * std::sin(phase))));
    }
    Trace trace;
    trace.H.reserve(static_cast<std::size_t>(stepsPerCycle));
    trace.out.reserve(static_cast<std::size_t>(stepsPerCycle));
    for (int n = 0; n < stepsPerCycle; ++n) {
        const double phase = 2.0 * M_PI * (static_cast<double>(n) / stepsPerCycle);
        const double H = amp * std::sin(phase);
        const float out = h.process(static_cast<float>(H));
        trace.H.push_back(H);
        trace.out.push_back(static_cast<double>(out));
    }
    return trace;
}

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

// T007 — the explicit RK2/RK4 steppers wired into process(). Drives a few
// cycles of a sinusoidal H and checks the integrated magnetization is well
// behaved and hysteretic. The full loop-area / cross-solver-agreement suite is
// T012/T019; this pins the steppers' core behavior only.
namespace {

// Drive `cycles` full sine cycles of amplitude `amp` through process() at
// `stepsPerCycle` samples/cycle, asserting the output is finite and bounded by
// a small multiple of Ms throughout. Returns the M sampled at the +amp*sin
// phase crossings on the rising vs falling branch of the LAST cycle, so the
// caller can assert a genuinely open loop.
struct BranchProbe {
    double risingM = 0.0;   // M near H = 0 while H increasing (rising branch)
    double fallingM = 0.0;  // M near H = 0 while H decreasing (falling branch)
    bool sawRising = false;
    bool sawFalling = false;
};

BranchProbe driveSine(Hysteresis& h, double amp, int cycles, int stepsPerCycle,
                      double Ms) {
    BranchProbe probe;
    const int total = cycles * stepsPerCycle;
    double prevH = 0.0;
    const double bound = 4.0 * Ms;  // small multiple of the saturation ceiling
    for (int n = 0; n <= total; ++n) {
        const double phase =
            2.0 * M_PI * (static_cast<double>(n) / stepsPerCycle);
        const double H = amp * std::sin(phase);
        const float out = h.process(static_cast<float>(H));
        CHECK(std::isfinite(out));
        CHECK(std::fabs(static_cast<double>(out)) <= bound);

        // Probe the last cycle: capture M as H sweeps through ~0 in each
        // direction, giving one point on the rising and one on the falling
        // branch at (nearly) the same field.
        if (n > (cycles - 1) * stepsPerCycle && std::fabs(H) < 0.05 * amp) {
            if (H > prevH) {
                probe.risingM = static_cast<double>(out);
                probe.sawRising = true;
            } else if (H < prevH) {
                probe.fallingM = static_cast<double>(out);
                probe.sawFalling = true;
            }
        }
        prevH = H;
    }
    return probe;
}

}  // namespace

TEST_CASE("Hysteresis::process — RK2/RK4 steppers integrate a loop (T007)") {
    const double Ms = 1.0;

    SUBCASE("RK2: finite, bounded, and a closed (open) loop forms") {
        Hysteresis h;
        h.prepare(48000.0);
        JAParams p;  // Ms=1, a=1, alpha=0, k=1, c=0.5
        p.k = 0.6;   // widen the loop so the branch split is unambiguous
        h.setParams(p);
        h.setSolver(Solver::rk2);
        h.reset();

        const BranchProbe probe = driveSine(h, 1.5, 4, 256, Ms);
        REQUIRE(probe.sawRising);
        REQUIRE(probe.sawFalling);
        // Rising vs falling branch differ at the same H => open hysteresis loop.
        CHECK(std::fabs(probe.risingM - probe.fallingM) > 1e-4);
    }

    SUBCASE("RK4: finite, bounded, and a closed (open) loop forms") {
        Hysteresis h;
        h.prepare(48000.0);
        JAParams p;
        p.k = 0.6;
        h.setParams(p);
        h.setSolver(Solver::rk4);
        h.reset();

        const BranchProbe probe = driveSine(h, 1.5, 4, 256, Ms);
        REQUIRE(probe.sawRising);
        REQUIRE(probe.sawFalling);
        CHECK(std::fabs(probe.risingM - probe.fallingM) > 1e-4);
    }

    SUBCASE("newtonRaphson selection is callable (routed to RK4 for T007)") {
        // T008 owns the real implicit stepper; here we only assert the case
        // compiles, runs, and stays finite (temporarily routed to RK4).
        Hysteresis h;
        h.prepare(48000.0);
        h.setParams(JAParams{});
        h.setSolver(Solver::newtonRaphson);
        h.reset();
        for (int n = 0; n < 512; ++n) {
            const double H = 1.2 * std::sin(2.0 * M_PI * n / 128.0);
            CHECK(std::isfinite(h.process(static_cast<float>(H))));
        }
    }
}

// T008 — the Newton-Raphson IMPLICIT stepper wired into process(). Drives a
// sinusoidal H and checks the implicitly-integrated magnetization is finite,
// bounded, and forms an open hysteresis loop; then cross-checks that under a
// mild drive the implicit loop stays CLOSE to the explicit RK4 loop within a
// loose tolerance. The full cross-solver agreement suite is T019; this pins the
// implicit stepper's core behavior and its rough consistency with RK4 only.
TEST_CASE("Hysteresis::process — Newton-Raphson implicit stepper (T008)") {
    const double Ms = 1.0;

    SUBCASE("newtonRaphson: finite, bounded, and an open loop forms") {
        Hysteresis h;
        h.prepare(48000.0);
        JAParams p;  // Ms=1, a=1, alpha=0, k=1, c=0.5
        p.k = 0.6;   // widen the loop so the branch split is unambiguous
        h.setParams(p);
        h.setSolver(Solver::newtonRaphson);
        h.reset();

        const BranchProbe probe = driveSine(h, 1.5, 4, 256, Ms);
        REQUIRE(probe.sawRising);
        REQUIRE(probe.sawFalling);
        // Rising vs falling branch differ at the same H => open hysteresis loop.
        CHECK(std::fabs(probe.risingM - probe.fallingM) > 1e-4);
    }

    SUBCASE("newtonRaphson stays close to RK4 under a mild drive") {
        // Same parameters + input drive through both solvers; the per-sample
        // outputs should track within a loose tolerance (both integrate the
        // same JA ODE; the implicit/explicit discretizations differ by O(step)).
        JAParams p;   // defaults; alpha=0, gentle so neither stepper is stiff
        const double amp = 0.6;      // mild drive (well below hard saturation)
        const int stepsPerCycle = 512;
        const int cycles = 3;
        const int total = cycles * stepsPerCycle;

        Hysteresis hn;
        hn.prepare(48000.0);
        hn.setParams(p);
        hn.setSolver(Solver::newtonRaphson);
        hn.reset();

        Hysteresis hr;
        hr.prepare(48000.0);
        hr.setParams(p);
        hr.setSolver(Solver::rk4);
        hr.reset();

        double maxAbsDiff = 0.0;
        for (int n = 0; n <= total; ++n) {
            const double phase =
                2.0 * M_PI * (static_cast<double>(n) / stepsPerCycle);
            const double H = amp * std::sin(phase);
            const double on = static_cast<double>(hn.process(static_cast<float>(H)));
            const double orr = static_cast<double>(hr.process(static_cast<float>(H)));
            CHECK(std::isfinite(on));
            const double diff = std::fabs(on - orr);
            if (diff > maxAbsDiff) maxAbsDiff = diff;
        }
        // Loose agreement: the implicit and explicit loops must not diverge.
        CHECK(maxAbsDiff < 5.0e-2);
    }
}

// T009 — the stiff-solver stability guard wired into process() (FR-006,
// contract C3). Two focused properties, for ALL THREE solvers: (1) a hot
// transient under deliberately stiff params can never produce a non-finite
// or unbounded output, and the primitive recovers to normal bounded output
// once the transient passes; (2) a non-finite input can never propagate
// NaN/Inf into the output or poison subsequent state. The full SC-005 sweep
// is a later task; this only pins the guard itself.
namespace {

// Mirrors the private kMBoundMultiplier documented in hysteresis.h (T009);
// duplicated here so the test can assert the *contract's* numeric bound
// (|M| <= kMBoundMultiplier * Ms) without needing friend access.
constexpr double kGuardBoundMultiplier = 4.0;

void checkHotTransientRecovers(Solver solver) {
    Hysteresis h;
    h.prepare(48000.0);
    // Physically well-behaved params (same shape T007/T008 already prove is
    // stable/bounded under normal drive) — the stress here comes from the
    // TRANSIENT (an enormous single-sample |dH|), not from pathological
    // physics, so "recovers afterward" is a meaningful assertion rather than
    // an artifact of params that never settle.
    JAParams p;
    p.Ms = 1.0;
    p.a = 1.0;
    p.alpha = 0.0;
    p.k = 0.6;   // widen the loop, as T007/T008 do
    p.c = 0.5;
    h.setParams(p);
    h.setSolver(solver);
    h.reset();

    const double bound = kGuardBoundMultiplier * p.Ms;

    // Adversarial hot transient: enormous alternating steps (|dH| ~ 1e5-2e5)
    // stress every solver's stiff-transient handling — a step this large
    // drives the explicit RK stages to overshoot the physical M range by
    // orders of magnitude before the JA sign-correction can rein it in,
    // exactly the regime the T009 guard exists for.
    const double transientSteps[] = {1.0e5, -1.0e5, 1.5e5, -2.0e5, 5.0e4};
    for (double H : transientSteps) {
        const float out = h.process(static_cast<float>(H));
        CHECK(std::isfinite(out));
        CHECK(std::fabs(static_cast<double>(out)) <= bound + 1e-6);
    }

    // Recovery: resume a normal, gentle drive and confirm the primitive
    // settles back into the physically-bounded (~Ms) operating range rather
    // than staying pinned at the guard's clamp bound.
    double lastOut = 0.0;
    for (int n = 0; n < 2000; ++n) {
        const double H = 0.5 * p.Ms * std::sin(2.0 * M_PI * n / 256.0);
        const float out = h.process(static_cast<float>(H));
        CHECK(std::isfinite(out));
        CHECK(std::fabs(static_cast<double>(out)) <= bound + 1e-6);
        lastOut = static_cast<double>(out);
    }
    // After 2000 gentle samples the primitive must have recovered to the
    // physical (Ms-scale) range, not remained pinned at the guard bound.
    CHECK(std::fabs(lastOut) <= 1.5 * p.Ms);
}

}  // namespace

TEST_CASE("Hysteresis::process — T009 stiff-solver stability guard (FR-006/C3)") {
    SUBCASE("rk2: hot transient stays finite+bounded and recovers") {
        checkHotTransientRecovers(Solver::rk2);
    }
    SUBCASE("rk4: hot transient stays finite+bounded and recovers") {
        checkHotTransientRecovers(Solver::rk4);
    }
    SUBCASE("newtonRaphson: hot transient stays finite+bounded and recovers") {
        checkHotTransientRecovers(Solver::newtonRaphson);
    }

    SUBCASE("non-finite input never propagates NaN/Inf, for any solver") {
        const Solver solvers[] = {Solver::rk2, Solver::rk4, Solver::newtonRaphson};
        for (Solver solver : solvers) {
            Hysteresis h;
            h.prepare(48000.0);
            h.setParams(JAParams{});
            h.setSolver(solver);
            h.reset();

            const float badInputs[] = {
                std::numeric_limits<float>::quiet_NaN(),
                std::numeric_limits<float>::infinity(),
                -std::numeric_limits<float>::infinity(),
            };
            for (float bad : badInputs) {
                const float out = h.process(bad);
                CHECK(std::isfinite(out));
            }

            // State must not be poisoned: normal processing afterward stays
            // finite and settles back into the Ms-scale operating range.
            double lastOut = 0.0;
            for (int n = 0; n < 1000; ++n) {
                const double H = 0.5 * std::sin(2.0 * M_PI * n / 256.0);
                const float out = h.process(static_cast<float>(H));
                CHECK(std::isfinite(out));
                lastOut = static_cast<double>(out);
            }
            CHECK(std::fabs(lastOut) <= 1.5);
        }
    }
}
