#pragma once

#include <cmath>
#include <stdexcept>
#include <string>

// Potentiometer — the tone-stack primitive's control surface, modelled as
// BUILD-TIME math (data-model.md "Taper / WiperSplit"; contracts/potentiometer.md;
// FR-006). A potentiometer is NOT a new circuit-vocabulary element: at build
// time a wiper maps a mechanical position through a taper law to two ordinary
// frozen-vocabulary `Resistor` legs. The component-abstractions v1 vocabulary
// stays frozen exactly as it was left (FR-003); the pot concept lives entirely
// here.
//
// A 3-terminal divider (`wiper`) splits a track of resistance `rTrack` at a
// wiper position `pos` in [0, 1] into an upper leg `rTop` (track high end ->
// wiper) and a lower leg `rBottom` (wiper -> track low end). Before flooring
// `rTop + rBottom == rTrack`, with `rBottom = f * rTrack` and `rTop =
// (1 - f) * rTrack` for the taper fraction `f`. A 2-terminal use (`rheostat`,
// e.g. the FMV mid pot to ground) returns the single lower leg.
//
// Taper law (research.md R3, design D10): `Linear` is `f = pos`; `Log` is the
// exponential "audio" taper `f = (base^pos - 1) / (base - 1)` with `base = 10`
// (one-decade curve, `f(0)=0`, `f(1)=1`). `Antilog` is deliberately ABSENT
// (D10) — not a stub: no v1 exemplar needs it (passive Baxandall uses linear
// pots), and a dead/throwing enum case would be exactly the fallback the repo
// bans. It is added only when a future stack requires mirrored controls.
//
// End-resistance floor (research.md R4, design D8): a real pot never reaches
// 0 ohm. Each returned leg is floored at a FIXED 10 ohm modelled contact/end
// resistance via `leg = max(leg, 10)`, applied PER LEG — so (for any real tone
// pot, where `rTrack` >> 2*kEndResistanceOhms) the floor bites only at the
// extremes (`pos = 0` / `pos = 1`) and mid-travel is untouched. (For a
// pathologically small `rTrack` < ~2*floor the floor can also bite mid-travel;
// no real tone pot — 10k..1M — is anywhere near that.) Near an endpoint
// `rTop + rBottom` may exceed `rTrack` by up to one floor;
// that is physically correct (series end resistance, not stolen from the
// track). The floor is a documented physical value, never a bug-hiding
// fallback: it keeps a built netlist from ever holding a 0-ohm short that would
// trip `Netlist::prepare()`.
//
// Errors, never fallbacks (Constitution V, FR-010): `pos` outside [0, 1] or a
// non-positive `rTrack` raise a descriptive `std::invalid_argument` on the
// build/control thread. There is no silent clamp of `pos` and no audio path.
//
// Physics in double. Header-only, zero-overhead, no heap, no I/O. Platform
// independence (Constitution IV): standard library only; no desktop or MCU
// platform-specific headers. C++17.

namespace acfx {

// The pot's mechanical-to-electrical law. v1 = { Linear, Log } (D10).
enum class Taper { Linear, Log };

// A 3-terminal divider wiper result: two ordinary Resistor leg values.
struct WiperSplit {
    double rTop;     // track high end -> wiper (ohms)
    double rBottom;  // wiper -> track low end (ohms)
};

// Fixed modelled wiper contact/end resistance (ohms) — the per-leg floor (D8).
inline constexpr double kEndResistanceOhms = 10.0;

// Base of the exponential "audio" taper law (R3).
inline constexpr double kLogTaperBase = 10.0;

// Map a mechanical position in [0, 1] to an electrical fraction f in [0, 1].
// Linear: f = pos. Log: exponential audio taper, f(0)=0, f(1)=1.
//
// PRECONDITION: `pos` in [0, 1] — UNCHECKED here (noexcept). The public callers
// wiper()/rheostat() validate `pos` first (FR-010); this is the shared taper
// kernel behind that gate. A direct caller passing `pos` outside [0, 1] gets an
// extrapolated fraction with no error, so validate before calling if you invoke
// it directly.
inline double taperFraction(double pos, Taper taper) noexcept {
    if (taper == Taper::Log) {
        return (std::pow(kLogTaperBase, pos) - 1.0) / (kLogTaperBase - 1.0);
    }
    return pos;  // Taper::Linear
}

namespace detail {

// Validate the shared preconditions for wiper()/rheostat() (FR-010).
inline void validatePot(double rTrack, double pos) {
    if (!(rTrack > 0.0)) {
        throw std::invalid_argument(
            "tone-stack potentiometer: rTrack must be > 0 (got " +
            std::to_string(rTrack) + ")");
    }
    if (!(pos >= 0.0 && pos <= 1.0)) {
        throw std::invalid_argument(
            "tone-stack potentiometer: pos must be in [0, 1] (got " +
            std::to_string(pos) + ")");
    }
}

// Apply the fixed per-leg end-resistance floor.
inline double floorLeg(double leg) noexcept {
    return leg > kEndResistanceOhms ? leg : kEndResistanceOhms;
}

}  // namespace detail

// 3-terminal divider: split `rTrack` at `pos` (through `taper`) into floored
// { rTop, rBottom }. Throws std::invalid_argument on bad input (FR-010).
inline WiperSplit wiper(double rTrack, double pos, Taper taper) {
    detail::validatePot(rTrack, pos);
    const double f = taperFraction(pos, taper);
    const double rBottom = f * rTrack;
    const double rTop = (1.0 - f) * rTrack;
    return WiperSplit{detail::floorLeg(rTop), detail::floorLeg(rBottom)};
}

// 2-terminal rheostat (single lower leg), e.g. the FMV mid pot to ground.
// Returns the floored `f * rTrack`. Throws on bad input (FR-010).
inline double rheostat(double rTrack, double pos, Taper taper) {
    detail::validatePot(rTrack, pos);
    const double f = taperFraction(pos, taper);
    return detail::floorLeg(f * rTrack);
}

}  // namespace acfx
