#pragma once

#include <type_traits>
#include <utility>

// one-port.h — the OnePort port interface for the WDF primitive family
// (contracts/wdf-one-ports.md I1–I3/W1; data-model.md "one-port port interface
// (concept)"; research R3). This is the foundational header every wave-domain
// leaf (Resistor, Capacitor, Inductor, sources, terminations, short/open) is
// checked against. It carries NO state and NO leaf implementations — only the
// duck-typed concept trait and the voltage-wave inverse helpers.
//
// The OnePort protocol (a *duck type*, NOT a virtual base class — no inheritance,
// no vtable on the wave path; I3 static dispatch). A type T is a OnePort iff it
// exposes ALL of the following, every wave-path member `noexcept` and heap-free
// (I1 uniform surface, G1 RT-safety):
//
//   double portResistance() const noexcept;   // Rp — the port reference resistance
//   double reflected()      const noexcept;    // b  — the reflected wave (up-sweep output)
//   void   incident(double a) noexcept;        // a  — the incident wave (down-sweep input)
//   static constexpr bool isAdaptable;         // is b independent of this sample's a?
//
// Call-ordering contract, selected by isAdaptable (I2, research R3):
//   - isAdaptable == true  (adaptable): reflected() is valid BEFORE this sample's
//       incident() — it reads stored state or a constant. The up-sweep reads b;
//       the down-sweep later delivers a via incident(a).
//   - isAdaptable == false (reflective): incident(a) is called FIRST, THEN
//       reflected() returns f(a) (root-evaluation order).
//
// Voltage-wave convention W1 (all leaves; current i referenced into the port):
//   forward:  a = v + Rp·i,  b = v − Rp·i
//   inverse:  v = (a + b) / 2,  i = (a − b) / (2·Rp)   — waveToVoltage / waveToCurrent below.

namespace acfx::wdf {

// -----------------------------------------------------------------------------
// is_one_port<T> — C++17 concept-emulation trait (SFINAE / std::void_t).
// -----------------------------------------------------------------------------
//
// `is_one_port<T>::value` (and the variable template `is_one_port_v<T>`) is
// `true` iff T exposes the full OnePort surface with the contracted signatures,
// `false` otherwise (any member missing, wrong return type, or — critically — a
// wave-path member that is not `noexcept`, per I1). Detection is duck-typed:
// there is no base class and T need not name this header.
//
// Implementation: the primary template is the false case. A single partial
// specialization is selected (via std::void_t) only when every required member
// expression is well-formed; its `value` then AND-folds the exact return-type
// and `noexcept`-ness requirements. Splitting well-formedness (SFINAE, in the
// void_t) from the semantic checks (return type + noexcept, in `value`) keeps a
// present-but-wrong member — e.g. a `reflected()` that is not `noexcept` — a
// hard `false` rather than a substitution failure that silently drops back to
// the primary template with the same result. Both paths yield `false`; the
// split is what makes the noexcept rejection expressible in C++17 without
// `concept`/`requires`.

template <typename T, typename = void>
struct is_one_port {
    static constexpr bool value = false;
};

template <typename T>
struct is_one_port<
    T,
    std::void_t<
        decltype(std::declval<const T&>().portResistance()),
        decltype(std::declval<const T&>().reflected()),
        decltype(std::declval<T&>().incident(std::declval<double>())),
        decltype(T::isAdaptable)>> {
    static constexpr bool value =
        // portResistance(): double, const-callable, noexcept.
        std::is_same_v<decltype(std::declval<const T&>().portResistance()), double> &&
        noexcept(std::declval<const T&>().portResistance()) &&
        // reflected(): double, const-callable, noexcept.
        std::is_same_v<decltype(std::declval<const T&>().reflected()), double> &&
        noexcept(std::declval<const T&>().reflected()) &&
        // incident(double): returns void, noexcept.
        std::is_same_v<decltype(std::declval<T&>().incident(std::declval<double>())), void> &&
        noexcept(std::declval<T&>().incident(std::declval<double>())) &&
        // isAdaptable: a static constant contextually convertible to bool.
        std::is_convertible_v<decltype(T::isAdaptable), bool>;
};

template <typename T>
inline constexpr bool is_one_port_v = is_one_port<T>::value;

// -----------------------------------------------------------------------------
// Voltage-wave inverse helpers (contract W1).
// -----------------------------------------------------------------------------
//
// Recover the physical port quantities from an incident/reflected wave pair.
// Pure, noexcept, allocation-free — usable on the audio-callback path.

// v = (a + b) / 2  — the port voltage from the wave pair.
inline double waveToVoltage(double a, double b) noexcept {
    return (a + b) / 2.0;
}

// i = (a − b) / (2·Rp)  — the port current (referenced into the port) from the
// wave pair and the port reference resistance Rp. Rp is expected > 0 (every
// leaf validates this at construction; not re-checked here on the hot path).
inline double waveToCurrent(double a, double b, double Rp) noexcept {
    return (a - b) / (2.0 * Rp);
}

}  // namespace acfx::wdf
