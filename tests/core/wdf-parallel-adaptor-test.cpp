#include <doctest/doctest.h>

#include <cmath>

#include "primitives/circuit/wdf/one-port.h"
#include "primitives/circuit/wdf/parallel-adaptor.h"

// WDF ParallelAdaptor scattering suite (wdf-adaptors feature, T005/T006).
//
// Covers the ParallelAdaptor<Child...> up-sweep (reflected()) / down-sweep
// (incident()) scattering relations and the parallel current-divider +
// shared-voltage sanity check (contracts/wdf-adaptors.md C1/C4/C5;
// data-model.md "ParallelAdaptor").
//
// T006 (core/primitives/circuit/wdf/parallel-adaptor.h, namespace acfx::wdf)
// MUST define `ParallelAdaptor<Child...>`:
//
//   template <class... Child> class ParallelAdaptor {
//     public:
//       explicit ParallelAdaptor(Child... children);
//       double portResistance() const noexcept;   // 1/G_up, G_up = Sum_k 1/R_k
//       double reflected()      const noexcept;    // b_u  (up-sweep)
//       void   incident(double a_u) noexcept;       // down-sweep
//       static constexpr bool isAdaptable = true;
//       template <std::size_t I> auto&       child() noexcept;
//       template <std::size_t I> const auto& child() const noexcept;
//   };
//
// This test file intentionally does NOT compile until T006 lands
// (`primitives/circuit/wdf/parallel-adaptor.h` does not exist yet, and
// `acfx::wdf::ParallelAdaptor` is undefined) -- that is the expected RED
// state for T005.

namespace {

// Test-local PROBE one-port: observes exactly what incident wave each child
// receives from the adaptor's down-sweep. The shipped Resistor discards its
// incident (memoryless), so it cannot serve as an observer here -- Probe
// records the last delivered incident wave instead. Adapted / reflection-free
// (reflected() returns a fixed pre-set value, independent of prior
// incident() calls), satisfying the ParallelAdaptor child constraints (C2).
struct Probe {
    double Rp{1.0};
    double refl{0.0};
    mutable double lastIncident{0.0};

    double portResistance() const noexcept { return Rp; }
    double reflected() const noexcept { return refl; }
    void incident(double a) noexcept { lastIncident = a; }
    static constexpr bool isAdaptable = true;
};

constexpr double kTightTol = 1e-12;

}  // namespace

static_assert(acfx::wdf::is_one_port_v<Probe>,
              "test-local Probe must satisfy the OnePort concept trait");
static_assert(
    acfx::wdf::is_one_port_v<acfx::wdf::ParallelAdaptor<Probe, Probe>>,
    "ParallelAdaptor<Probe, Probe> must satisfy the OnePort concept trait (contract C1)");

TEST_CASE("ParallelAdaptor portResistance() is the parallel combination (1/Rup = 1/Ra + 1/Rb)") {
    const double Ra = 1000.0;
    const double Rb = 3000.0;

    acfx::wdf::ParallelAdaptor<Probe, Probe> adaptor(Probe{Ra, 0.0, 0.0}, Probe{Rb, 0.0, 0.0});

    // 1/Ra + 1/Rb = 1/1000 + 1/3000 = 4/3000 = 1/750, i.e. Rup = 750 exactly.
    const double expectedRup = 750.0;
    CHECK(std::abs((1.0 / Ra) + (1.0 / Rb) - (1.0 / expectedRup)) < kTightTol);
    CHECK(adaptor.portResistance() == doctest::Approx(expectedRup).epsilon(kTightTol));
    CHECK(std::abs(1.0 / adaptor.portResistance() - ((1.0 / Ra) + (1.0 / Rb))) < kTightTol);
}

TEST_CASE("ParallelAdaptor reflected() (up-sweep) is b_u = (Ga*a0 + Gb*a1)/Gup, independent of a_u") {
    const double Ra = 1000.0;
    const double Rb = 3000.0;
    const double Ga = 1.0 / Ra;
    const double Gb = 1.0 / Rb;
    const double Gup = Ga + Gb;
    const double a0 = 0.4;
    const double a1 = -0.1;

    acfx::wdf::ParallelAdaptor<Probe, Probe> adaptor(Probe{Ra, a0, 0.0}, Probe{Rb, a1, 0.0});

    const double bu = adaptor.reflected();
    const double expectedBu = (Ga * a0 + Gb * a1) / Gup;  // = 0.275 exactly
    CHECK(std::abs(expectedBu - 0.275) < kTightTol);
    CHECK(bu == doctest::Approx(expectedBu).epsilon(kTightTol));

    // Reflection-free: a fresh adaptor with the same child reflected() values
    // yields the identical b_u -- reflected() must not depend on this
    // sample's not-yet-delivered a_u (contract C4: "b_u independent of a_u").
    acfx::wdf::ParallelAdaptor<Probe, Probe> adaptor2(Probe{Ra, a0, 0.0}, Probe{Rb, a1, 0.0});
    const double buAgain = adaptor2.reflected();
    CHECK(buAgain == doctest::Approx(bu).epsilon(kTightTol));
}

TEST_CASE("ParallelAdaptor incident(a_u) (down-sweep) delivers child_k.incident = v2 - a_k") {
    // Concrete numeric fixture: Ra=1000, Rb=3000, a0=0.4, a1=-0.1, a_u=1.5.
    const double Ra = 1000.0;
    const double Rb = 3000.0;
    const double Ga = 1.0 / Ra;
    const double Gb = 1.0 / Rb;
    const double Gup = Ga + Gb;
    const double a0 = 0.4;
    const double a1 = -0.1;
    const double au = 1.5;

    acfx::wdf::ParallelAdaptor<Probe, Probe> adaptor(Probe{Ra, a0, 0.0}, Probe{Rb, a1, 0.0});

    // C5 call ordering: up-sweep (reflected()) before down-sweep (incident())
    // in the same sample, so the adaptor caches a0/a1 before scattering a_u.
    const double bu = adaptor.reflected();  // = 0.275 (verified above)
    adaptor.incident(au);

    // v2 = a_u + (Sum_child G_child*a_child)/G_up = a_u + b_u = 1.5 + 0.275 = 1.775.
    const double v2 = au + bu;
    const double expected0 = v2 - a0;  // 1.775 - 0.4  = 1.375
    const double expected1 = v2 - a1;  // 1.775 - (-0.1) = 1.875

    CHECK(std::abs(v2 - 1.775) < kTightTol);
    CHECK(std::abs(expected0 - 1.375) < kTightTol);
    CHECK(std::abs(expected1 - 1.875) < kTightTol);

    CHECK(adaptor.child<0>().lastIncident == doctest::Approx(expected0).epsilon(kTightTol));
    CHECK(adaptor.child<1>().lastIncident == doctest::Approx(expected1).epsilon(kTightTol));

    // Equivalent all-ports form: b_k = 2*(Sum_i G_i*a_i)/G_total - a_k, with the
    // sum over ALL ports (incl. upward) and G_total = 2*Gup. Cross-check both
    // children against this form to guard against a coefficient/index slip.
    const double Gtotal = 2.0 * Gup;
    const double sumAll = Gup * au + Ga * a0 + Gb * a1;
    const double allPortsExpected0 = 2.0 * sumAll / Gtotal - a0;
    const double allPortsExpected1 = 2.0 * sumAll / Gtotal - a1;
    CHECK(std::abs(allPortsExpected0 - expected0) < kTightTol);
    CHECK(std::abs(allPortsExpected1 - expected1) < kTightTol);
}

TEST_CASE(
    "ParallelAdaptor current-divider sanity: shared voltage across children, "
    "i_a/i_b == Rb/Ra for adapted children (a0=a1=0)") {
    const double Ra = 1000.0;
    const double Rb = 3000.0;
    const double au = 2.0;

    acfx::wdf::ParallelAdaptor<Probe, Probe> adaptor(Probe{Ra, 0.0, 0.0}, Probe{Rb, 0.0, 0.0});

    adaptor.reflected();   // up-sweep: caches a0 = a1 = 0 (adapted children)
    adaptor.incident(au);  // down-sweep: drives the upward port

    // Shared voltage: with a0 = a1 = 0, v2 = a_u, so each child receives
    // exactly a_u as its incident wave -- the defining parallel behavior.
    CHECK(adaptor.child<0>().lastIncident == doctest::Approx(au).epsilon(kTightTol));
    CHECK(adaptor.child<1>().lastIncident == doctest::Approx(au).epsilon(kTightTol));

    const double va = acfx::wdf::waveToVoltage(0.0, adaptor.child<0>().lastIncident);
    const double vb = acfx::wdf::waveToVoltage(0.0, adaptor.child<1>().lastIncident);
    CHECK(std::abs(va - vb) <= 1e-12);

    // Current divider: i_k referenced into each child's port.
    const double ia = acfx::wdf::waveToCurrent(0.0, adaptor.child<0>().lastIncident, Ra);
    const double ib = acfx::wdf::waveToCurrent(0.0, adaptor.child<1>().lastIncident, Rb);
    REQUIRE(ib != 0.0);

    const double ratio = ia / ib;
    const double expectedRatio = Rb / Ra;  // == Ga/Gb
    CHECK(std::abs(ratio - expectedRatio) / std::abs(expectedRatio) <= 1e-12);
}
