#pragma once

#include <array>
#include <cstddef>
#include <tuple>

#include "primitives/circuit/wdf/one-port.h"
#include "primitives/circuit/wdf/adaptor-detail.h"

// parallel-adaptor.h — the N-port PARALLEL scattering junction for the WDF
// primitive family (contracts/wdf-adaptors.md C1/C2/C4/C5/C6; data-model.md
// "ParallelAdaptor"). A parallel adaptor models the shared-voltage constraint
// (one port voltage across all ports, Σ i = 0) and is ITSELF a OnePort, so it
// composes as a child of another adaptor (an interior tree node) or sits at the
// tree root under a reflective termination.
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
// adaptor-detail.h; this file supplies only the PARALLEL coefficient formula.

namespace acfx::wdf {

// ParallelAdaptor<Child...> — owns its children BY VALUE in a std::tuple
// (composition, not inheritance). N = sizeof...(Child) >= 1.
//
// Parallel scattering (contract C4, data-model.md): with conductances
// G_k = 1/R_k and the adapted upward-port conductance G_up = Σ_k G_k (so the
// upward port is reflection-free), and a_k the child reflected waves captured
// in the up-sweep,
//   b_u = (Σ_k G_k·a_k) / G_up                       (up-sweep, reflection-free)
//   b_k = v2 − a_k,  v2 = a_u + (Σ_k G_k·a_k)/G_up = a_u + b_u   (down-sweep)
// where G_up = Σ_k G_k and portResistance() = 1 / G_up. The per-child down-sweep
// needs only the shared node voltage v2; the reused (Σ G_k·a_k)/G_up term is
// exactly the cached b_u.
template <class... Child>
class ParallelAdaptor {
public:
    static constexpr std::size_t N = sizeof...(Child);

    // Compile-time conformance guards (contract C2), emitted via the shared
    // aggregate: arity >= 1, all children OnePorts, all children adaptable.
    // A non-adaptable (reflective) child fails to compile here.
    static_assert(detail::adaptorArityOk<Child...>,
                  "ParallelAdaptor requires at least one child (C2.1).");
    static_assert(detail::adaptorChildrenAreOnePorts<Child...>,
                  "ParallelAdaptor children must all satisfy the OnePort concept (C2.2).");
    static_assert(detail::adaptorChildrenAdaptable<Child...>,
                  "ParallelAdaptor children must all be adaptable "
                  "(C2.3 delay-free-loop guard: the reflective port is the root).");

    // isAdaptable: a parallel adaptor is adaptable — its upward reflected wave
    // b_u depends only on the child reflected waves, never on this sample's
    // not-yet-delivered upward incident a_u (contract C4). This lets it be a
    // child of another adaptor.
    static constexpr bool isAdaptable = true;

    // Construction (off the hot path — MAY throw). Takes children by value and
    // moves them into the tuple. Gathers each child's port resistance, rejects
    // any non-finite / <= 0 value (naming the child index; no clamping), then
    // precomputes G_k = 1/R_k, G_up = Σ_k G_k, and coeff_[k] = G_k / G_up.
    explicit ParallelAdaptor(Child... children)
        : children_(std::move(children)...) {
        Rk_ = detail::gatherPortResistances(children_);
        detail::validatePortResistances(Rk_);  // throws on invalid R_k
        double sum = 0.0;
        for (std::size_t k = 0; k < N; ++k) {
            Gk_[k] = 1.0 / Rk_[k];
            sum += Gk_[k];
        }
        Gup_ = sum;
        Rup_ = 1.0 / Gup_;
        for (std::size_t k = 0; k < N; ++k) {
            coeff_[k] = Gk_[k] / Gup_;
        }
    }

    // portResistance() — the adapted upward port resistance R_up = 1 / Σ_k G_k
    // (the parallel combination of the child resistances).
    double portResistance() const noexcept { return Rup_; }

    // reflected() (UP-SWEEP) — captures each child's reflected wave into the
    // per-sample cache and returns b_u = (Σ_k G_k·a_k) / G_up. Reflection-free:
    // the result is independent of this sample's upward incident a_u (contract
    // C4/C5). The cache is `mutable` because the OnePort concept requires this
    // method to be const; the cache is logically part of the same sample's
    // evaluation.
    double reflected() const noexcept {
        cachedChildWave_ = detail::gatherReflected(children_);
        double weighted = 0.0;
        for (std::size_t k = 0; k < N; ++k) {
            weighted += coeff_[k] * cachedChildWave_[k];
        }
        return weighted;  // (Σ_k G_k·a_k)/G_up, since coeff_[k] = G_k/G_up
    }

    // incident(a_u) (DOWN-SWEEP) — with the shared node voltage wave
    // v2 = a_u + (Σ_k G_k·cachedChildWave_[k])/G_up = a_u + b_u, delivers
    // b_k = v2 − cachedChildWave_[k] to each child, strictly in child order via
    // fanIncident. Must be called after reflected() in the same sample (C5) so
    // the cache is populated.
    void incident(double aUp) noexcept {
        double weighted = 0.0;
        for (std::size_t k = 0; k < N; ++k) {
            weighted += coeff_[k] * cachedChildWave_[k];
        }
        const double v2 = aUp + weighted;  // weighted == cached b_u
        detail::fanIncident(children_, [&](std::size_t k) {
            return v2 - cachedChildWave_[k];
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
    std::array<double, N> Gk_{};      // each child's conductance G_k = 1/R_k
    double Gup_{0.0};                 // adapted upward conductance Σ_k G_k
    double Rup_{0.0};                 // adapted upward resistance 1 / G_up
    std::array<double, N> coeff_{};   // down-sweep coefficient G_k / G_up
    mutable std::array<double, N> cachedChildWave_{};  // up-sweep cache a_k
};

}  // namespace acfx::wdf
