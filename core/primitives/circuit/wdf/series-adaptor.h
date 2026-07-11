#pragma once

#include <array>
#include <cstddef>
#include <tuple>

#include "primitives/circuit/wdf/one-port.h"
#include "primitives/circuit/wdf/adaptor-detail.h"

// series-adaptor.h — the N-port SERIES scattering junction for the WDF
// primitive family (contracts/wdf-adaptors.md C1/C2/C4/C5/C6; data-model.md
// "SeriesAdaptor"). A series adaptor models the shared-current constraint
// (one port current through all ports, Σ v = 0) and is ITSELF a OnePort, so
// it composes as a child of another adaptor (an interior tree node) or sits
// at the tree root under a reflective termination.
//
// Header-only, C++17, duck-typed / static-dispatch only: no inheritance, no
// virtual, no RTTI, no std::function, no heap on the wave path. The per-sample
// members (reflected/incident) are noexcept and allocation-free (contract C6,
// G1 RT-safety). The constructor gathers + validates child port resistances
// and precomputes the down-sweep coefficients — that is off the hot path and
// MAY throw std::invalid_argument (contract C3, no clamping / no fallback).
//
// All shared variadic-sweep machinery (the three conformance guards, the
// index-sequence folds, the resistance validation) is reused from
// adaptor-detail.h; this file supplies only the SERIES coefficient formula.

namespace acfx::wdf {

// SeriesAdaptor<Child...> — owns its children BY VALUE in a std::tuple
// (composition, not inheritance). N = sizeof...(Child) >= 1.
//
// Series scattering (contract C4, data-model.md): with the all-ports incident
// sum S = a_u + Σ_k a_k (a_u the upward-port incident, a_k the child reflected
// waves captured in the up-sweep),
//   b_u = −Σ_k a_k                              (up-sweep, reflection-free)
//   b_k = a_k − coeff_[k]·S,  coeff_[k] = R_k / Rup_   (down-sweep)
// where Rup_ = Σ_k R_k. Note coeff_[k] = R_k/Rup_ ≡ 2·R_k/R_total with the
// all-ports total R_total = 2·Rup_ — NOT 2·R_k/Rup_ (that double-counts).
template <class... Child>
class SeriesAdaptor {
public:
    static constexpr std::size_t N = sizeof...(Child);

    // Compile-time conformance guards (contract C2), emitted via the shared
    // aggregate: arity >= 1, all children OnePorts, all children adaptable.
    // A non-adaptable (reflective) child fails to compile here.
    static_assert(detail::adaptorArityOk<Child...>,
                  "SeriesAdaptor requires at least one child (C2.1).");
    static_assert(detail::adaptorChildrenAreOnePorts<Child...>,
                  "SeriesAdaptor children must all satisfy the OnePort concept (C2.2).");
    static_assert(detail::adaptorChildrenAdaptable<Child...>,
                  "SeriesAdaptor children must all be adaptable "
                  "(C2.3 delay-free-loop guard: the reflective port is the root).");

    // isAdaptable: a series adaptor is adaptable — its upward reflected wave
    // b_u depends only on the child reflected waves, never on this sample's
    // not-yet-delivered upward incident a_u (contract C4). This lets it be a
    // child of another adaptor.
    static constexpr bool isAdaptable = true;

    // Construction (off the hot path — MAY throw). Takes children by value and
    // moves them into the tuple. Gathers each child's port resistance, rejects
    // any non-finite / <= 0 value (naming the child index; no clamping), then
    // precomputes Rup_ = Σ R_k and coeff_[k] = R_k / Rup_.
    explicit SeriesAdaptor(Child... children)
        : children_(std::move(children)...) {
        Rk_ = detail::gatherPortResistances(children_);
        detail::validatePortResistances(Rk_);  // throws on invalid R_k
        double sum = 0.0;
        for (std::size_t k = 0; k < N; ++k) {
            sum += Rk_[k];
        }
        Rup_ = sum;
        for (std::size_t k = 0; k < N; ++k) {
            coeff_[k] = Rk_[k] / Rup_;
        }
    }

    // portResistance() — the adapted upward port resistance R_up = Σ_k R_k.
    double portResistance() const noexcept { return Rup_; }

    // reflected() (UP-SWEEP) — captures each child's reflected wave into the
    // per-sample cache and returns b_u = −Σ_k a_k. Reflection-free: the result
    // is independent of this sample's upward incident a_u (contract C4/C5).
    // The cache is `mutable` because the OnePort concept requires this method
    // to be const; the cache is logically part of the same sample's evaluation.
    double reflected() const noexcept {
        cachedChildWave_ = detail::gatherReflected(children_);
        double sum = 0.0;
        for (std::size_t k = 0; k < N; ++k) {
            sum += cachedChildWave_[k];
        }
        return -sum;
    }

    // incident(a_u) (DOWN-SWEEP) — with S = a_u + Σ_k cachedChildWave_[k] the
    // all-ports incident sum, delivers b_k = cachedChildWave_[k] − coeff_[k]·S
    // to each child, strictly in child order via fanIncident. Must be called
    // after reflected() in the same sample (C5) so the cache is populated.
    void incident(double aUp) noexcept {
        double S = aUp;
        for (std::size_t k = 0; k < N; ++k) {
            S += cachedChildWave_[k];
        }
        detail::fanIncident(children_, [&](std::size_t k) {
            return cachedChildWave_[k] - coeff_[k] * S;
        });
    }

    // child<I>() — access the owned child one-port (composition seam for tree
    // assembly and inspection).
    template <std::size_t I>
    auto& child() noexcept { return std::get<I>(children_); }

    template <std::size_t I>
    const auto& child() const noexcept { return std::get<I>(children_); }

private:
    std::tuple<Child...> children_;
    std::array<double, N> Rk_{};      // each child's port resistance R_k
    double Rup_{0.0};                 // adapted upward resistance Σ_k R_k
    std::array<double, N> coeff_{};   // down-sweep coefficient R_k / Rup_
    mutable std::array<double, N> cachedChildWave_{};  // up-sweep cache a_k
};

}  // namespace acfx::wdf
