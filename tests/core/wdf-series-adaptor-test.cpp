#include <doctest/doctest.h>

#include <cmath>

#include "primitives/circuit/wdf/one-port.h"
#include "primitives/circuit/wdf/series-adaptor.h"

// WDF SeriesAdaptor scattering suite (wdf-adaptors feature, T003/T004).
//
// Covers the SeriesAdaptor<Child...> up-sweep (reflected()) / down-sweep
// (incident()) scattering relations and the series voltage-divider sanity
// check (contracts/wdf-adaptors.md C1/C4/C5; data-model.md "SeriesAdaptor").
//
// T004 (core/primitives/circuit/wdf/series-adaptor.h, namespace acfx::wdf)
// MUST define `SeriesAdaptor<Child...>`:
//
//   template <class... Child> class SeriesAdaptor {
//     public:
//       explicit SeriesAdaptor(Child... children);
//       double portResistance() const noexcept;   // R_up = Sum_k R_k
//       double reflected()      const noexcept;    // b_u  (up-sweep)
//       void   incident(double a_u) noexcept;       // down-sweep
//       static constexpr bool isAdaptable = true;
//       template <std::size_t I> auto&       child() noexcept;
//       template <std::size_t I> const auto& child() const noexcept;
//   };
//
// This test file intentionally does NOT compile until T004 lands
// (`primitives/circuit/wdf/series-adaptor.h` does not exist yet, and
// `acfx::wdf::SeriesAdaptor` is undefined) -- that is the expected RED
// state for T003.

namespace {

// Test-local PROBE one-port: observes exactly what incident wave each child
// receives from the adaptor's down-sweep. The shipped Resistor discards its
// incident (memoryless), so it cannot serve as an observer here -- Probe
// records the last delivered incident wave instead. Adapted / reflection-free
// (reflected() returns a fixed pre-set value, independent of prior
// incident() calls), satisfying the SeriesAdaptor child constraints (C2).
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
static_assert(acfx::wdf::is_one_port_v<acfx::wdf::SeriesAdaptor<Probe, Probe>>,
              "SeriesAdaptor<Probe, Probe> must satisfy the OnePort concept trait (contract C1)");

TEST_CASE("SeriesAdaptor portResistance() is the sum of child port resistances (R_up = Ra + Rb)") {
    const double Ra = 1000.0;
    const double Rb = 3000.0;

    acfx::wdf::SeriesAdaptor<Probe, Probe> adaptor(Probe{Ra, 0.0, 0.0}, Probe{Rb, 0.0, 0.0});

    CHECK(adaptor.portResistance() == doctest::Approx(Ra + Rb));
}

TEST_CASE("SeriesAdaptor reflected() (up-sweep) is b_u = -(a0 + a1), independent of a_u") {
    const double Ra = 1000.0;
    const double Rb = 3000.0;
    const double a0 = 0.5;
    const double a1 = -0.2;

    acfx::wdf::SeriesAdaptor<Probe, Probe> adaptor(Probe{Ra, a0, 0.0}, Probe{Rb, a1, 0.0});

    const double bu = adaptor.reflected();
    CHECK(bu == doctest::Approx(-(a0 + a1)).epsilon(kTightTol));

    // Reflection-free: a fresh adaptor with the same child reflected() values
    // yields the identical b_u -- reflected() must not depend on this
    // sample's not-yet-delivered a_u (contract C4: "b_u independent of a_u").
    acfx::wdf::SeriesAdaptor<Probe, Probe> adaptor2(Probe{Ra, a0, 0.0}, Probe{Rb, a1, 0.0});
    const double buAgain = adaptor2.reflected();
    CHECK(buAgain == doctest::Approx(bu).epsilon(kTightTol));
}

TEST_CASE("SeriesAdaptor incident(a_u) (down-sweep) delivers child_k.incident = a_k - (R_k/R_up)*S") {
    // Concrete numeric fixture: Ra=1000, Rb=3000, a0=0.5, a1=-0.2, a_u=2.0.
    const double Ra = 1000.0;
    const double Rb = 3000.0;
    const double Rup = Ra + Rb;  // 4000
    const double a0 = 0.5;
    const double a1 = -0.2;
    const double au = 2.0;
    const double S = au + a0 + a1;  // 2.3 -- all-ports incident sum

    acfx::wdf::SeriesAdaptor<Probe, Probe> adaptor(Probe{Ra, a0, 0.0}, Probe{Rb, a1, 0.0});

    // C5 call ordering: up-sweep (reflected()) before down-sweep (incident())
    // in the same sample, so the adaptor caches a0/a1 before scattering a_u.
    adaptor.reflected();
    adaptor.incident(au);

    const double expected0 = a0 - (Ra / Rup) * S;  // 0.5 - 0.25*2.3 = -0.075
    const double expected1 = a1 - (Rb / Rup) * S;  // -0.2 - 0.75*2.3 = -1.925

    CHECK(std::abs(expected0 - (-0.075)) < kTightTol);
    CHECK(std::abs(expected1 - (-1.925)) < kTightTol);

    CHECK(adaptor.child<0>().lastIncident == doctest::Approx(expected0).epsilon(kTightTol));
    CHECK(adaptor.child<1>().lastIncident == doctest::Approx(expected1).epsilon(kTightTol));

    // Coefficient sanity: R_k/R_up (NOT 2*R_k/R_up -- that would double-count
    // against the all-ports total R_total = 2*R_up, per data-model.md).
    CHECK(std::abs((Ra / Rup) - 0.25) < kTightTol);
    CHECK(std::abs((Rb / Rup) - 0.75) < kTightTol);
}

TEST_CASE("SeriesAdaptor series voltage-divider sanity: v_a/v_b == Ra/Rb for adapted children (a0=a1=0)") {
    const double Ra = 1000.0;
    const double Rb = 3000.0;
    const double au = 2.0;

    acfx::wdf::SeriesAdaptor<Probe, Probe> adaptor(Probe{Ra, 0.0, 0.0}, Probe{Rb, 0.0, 0.0});

    adaptor.reflected();   // up-sweep: caches a0 = a1 = 0 (adapted children)
    adaptor.incident(au);  // down-sweep: drives the upward port

    const double va = acfx::wdf::waveToVoltage(0.0, adaptor.child<0>().lastIncident);
    const double vb = acfx::wdf::waveToVoltage(0.0, adaptor.child<1>().lastIncident);

    REQUIRE(vb != 0.0);
    const double ratio = va / vb;
    const double expectedRatio = Ra / Rb;

    CHECK(std::abs(ratio - expectedRatio) / std::abs(expectedRatio) <= 1e-12);
}
