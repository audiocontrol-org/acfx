#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

#include "primitives/circuit/wdf/one-port.h"

// adaptor-detail.h — the shared variadic-sweep machinery both SeriesAdaptor and
// ParallelAdaptor compose (data-model.md "Shared: adaptor-detail.h"; contract
// C2/C3/C6; research R1/R3/R4/R5/R7). It carries NO adaptor state and NO
// scattering formula: it provides only (a) the three compile-time conformance
// guards, (b) the generic index-sequence folds over a std::tuple<Child...> that
// the up-sweep / down-sweep drive, and (c) the construction-time resistance
// validation. Each adaptor supplies only its own coefficient formula.
//
// Everything here is header-only, C++17, duck-typed / static-dispatch only: no
// inheritance, no virtual, no RTTI, no std::function, no heap on the wave path.
// The per-sample folds (gatherReflected, fanIncident) are noexcept and
// allocation-free (audio path, contract C6). validatePortResistances runs at
// construction (off the hot path) and MAY throw (contract C3, research R7).

namespace acfx::wdf {
namespace detail {

// -----------------------------------------------------------------------------
// (a) Conformance checks — usable by both adaptors as static_assert operands.
// -----------------------------------------------------------------------------
//
// Three independent predicates, exposed as constexpr variable templates so an
// adaptor can write `static_assert(detail::adaptorArityOk<Child...>, "...")`.
// A convenience aggregate (AdaptorConformance) that emits all three with
// messages is provided below.

// Arity: at least one child (an empty pack is rejected). Fold-free — a plain
// size comparison — so it is meaningful even with zero children.
template <typename... Child>
inline constexpr bool adaptorArityOk = (sizeof...(Child) >= 1);

// Every child satisfies the shipped OnePort concept trait. Empty pack folds to
// true (arity is the check that rejects emptiness).
template <typename... Child>
inline constexpr bool adaptorChildrenAreOnePorts = (is_one_port_v<Child> && ...);

// isAdaptableChild<T> — guarded read of `T::isAdaptable`. A non-OnePort T need
// not even declare `isAdaptable`, so reading it directly would be a hard error
// rather than a clean predicate; the primary template reports false for any
// non-OnePort, and only the OnePort specialization reads the member. This keeps
// adaptorChildrenAdaptable well-formed for ANY pack, so the arity / one-port
// static_asserts fire with their own messages instead of a member-access error.
template <typename T, bool = is_one_port_v<T>>
struct isAdaptableChild : std::false_type {};

template <typename T>
struct isAdaptableChild<T, true> : std::bool_constant<T::isAdaptable> {};

// Every child is adaptable (the delay-free-loop guard, research R3): a
// non-adaptable OnePort (ShortCircuit / OpenCircuit / any isAdaptable == false)
// makes this false, so an adaptor over it fails to compile. Empty pack folds to
// true.
template <typename... Child>
inline constexpr bool adaptorChildrenAdaptable = (isAdaptableChild<Child>::value && ...);

// AdaptorConformance<Child...> — instantiate (e.g. as a zero-size member or a
// local `using`) to emit all three guards with contract-worded messages in one
// place. The predicates above remain available for adaptors that prefer inline
// static_asserts.
template <typename... Child>
struct AdaptorConformance {
    static_assert(adaptorArityOk<Child...>,
                  "WDF adaptor requires at least one child (C2.1: empty child pack rejected).");
    static_assert(adaptorChildrenAreOnePorts<Child...>,
                  "WDF adaptor children must all satisfy the OnePort concept (C2.2).");
    static_assert(adaptorChildrenAdaptable<Child...>,
                  "WDF adaptor children must all be adaptable "
                  "(C2.3 delay-free-loop guard: no reflective child; the reflective port is the root).");
};

// -----------------------------------------------------------------------------
// (b) Index-sequence fold helpers over std::tuple<Child...>.
// -----------------------------------------------------------------------------
//
// Generic in the child set; the caller (adaptor) supplies only the coefficient
// formula for the down-sweep. gatherPortResistances runs at construction;
// gatherReflected and fanIncident run per sample and are noexcept + heap-free.

// Gather each child's portResistance() into a std::array<double, N>, in child
// order. Construction-time (feeds R_k / G_k and validation). noexcept because
// portResistance() is noexcept by the OnePort concept.
template <typename... Child, std::size_t... I>
std::array<double, sizeof...(Child)> gatherPortResistancesImpl(
    const std::tuple<Child...>& children, std::index_sequence<I...>) noexcept {
    return std::array<double, sizeof...(Child)>{std::get<I>(children).portResistance()...};
}

template <typename... Child>
std::array<double, sizeof...(Child)> gatherPortResistances(
    const std::tuple<Child...>& children) noexcept {
    return gatherPortResistancesImpl(children, std::index_sequence_for<Child...>{});
}

// Gather each child's reflected() into a std::array<double, N> — the up-sweep
// cache (research R4). Single evaluation per child per sample, in child order.
// noexcept + heap-free (wave path); reflected() is const noexcept by concept.
template <typename... Child, std::size_t... I>
std::array<double, sizeof...(Child)> gatherReflectedImpl(
    const std::tuple<Child...>& children, std::index_sequence<I...>) noexcept {
    return std::array<double, sizeof...(Child)>{std::get<I>(children).reflected()...};
}

template <typename... Child>
std::array<double, sizeof...(Child)> gatherReflected(
    const std::tuple<Child...>& children) noexcept {
    return gatherReflectedImpl(children, std::index_sequence_for<Child...>{});
}

// Fan a per-child incident wave back to each child.incident(...) — the
// down-sweep. `computeIncident` is a caller-supplied callable
// `double(std::size_t k)` returning the incident wave for child k (the adaptor's
// scattering formula). Delivered strictly in child order (0..N-1) via the
// left-to-right comma fold. noexcept + heap-free (wave path); a throwing
// callable would terminate, which is the intended RT-safety posture. No
// std::function — the callable is a template parameter (static dispatch).
template <typename... Child, typename Fn, std::size_t... I>
void fanIncidentImpl(std::tuple<Child...>& children, Fn&& computeIncident,
                     std::index_sequence<I...>) noexcept {
    (std::get<I>(children).incident(computeIncident(I)), ...);
}

template <typename... Child, typename Fn>
void fanIncident(std::tuple<Child...>& children, Fn&& computeIncident) noexcept {
    fanIncidentImpl(children, std::forward<Fn>(computeIncident), std::index_sequence_for<Child...>{});
}

// -----------------------------------------------------------------------------
// (c) Construction-time validation (off the hot path — MAY throw).
// -----------------------------------------------------------------------------
//
// Given the gathered child port resistances, throw std::invalid_argument (naming
// the offending child INDEX and the bad value) when any is non-finite or <= 0.
// It NEVER clamps, substitutes, or falls back — an invalid resistance is a hard
// construction error (contract C3, research R7, repo no-fallback standard). The
// empty-child case is handled by the arity static_assert in (a), not here.
template <std::size_t N>
void validatePortResistances(const std::array<double, N>& portResistances) {
    for (std::size_t k = 0; k < N; ++k) {
        const double Rk = portResistances[k];
        if (!std::isfinite(Rk) || !(Rk > 0.0)) {
            throw std::invalid_argument(
                "WDF adaptor: child index " + std::to_string(k) +
                " has an invalid portResistance() = " + std::to_string(Rk) +
                " (must be finite and > 0; no clamping or fallback).");
        }
    }
}

}  // namespace detail
}  // namespace acfx::wdf
