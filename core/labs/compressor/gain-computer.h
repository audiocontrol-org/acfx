#pragma once

#include <cmath>
#include <cstdint>

// GainComputer — RT-safe stateless static-curve gain-reduction primitive.
//
// Maps an input level (dB) to a gain change (dB, <= 0 = attenuation) via a
// selectable curve mode (compress / limit / expand / gate) with a single
// unified quadratic C1 knee straddling the threshold (hard corner at
// knee = 0). This is the pure static-curve kernel that dynamics processors
// (compressors, limiters, expanders, gates, …) drive with an externally
// supplied level (see EnvelopeFollower); it holds NO runtime state of its
// own — it does not detect level or apply ballistics (see
// contracts/gain-computer-api.md).
//
// Lives in core/labs/compressor/ as the authoring lab for this primitive; it
// graduates — unchanged in its public contract — via `git mv` into
// core/primitives/dynamics/gain-computer.h per Constitution IX. The
// originating lab persists as README + host-only harness.
//
// This file is the kernel SKELETON (task T002): the public API from
// contracts/gain-computer-api.md is declared with inline stub bodies so the
// header compiles standalone. The setters already store their fields; the
// real piecewise curve math (unified knee, per-mode branches, range bound,
// ratio guard) is filled in by a later task (see
// specs/compressors/data-model.md, "Entity — GainComputer").
//
// Constitution refs:
//   IV   — platform-independent core: no JUCE / Daisy SDK / Teensy / effects /
//          harness includes here, ever.
//   VI   — real-time safety: no heap allocation, no locks, bounded work in
//          computeGainDb() (branch-only arithmetic, no transcendental).
//   VII  — strict typing & small modules: no `any`-equivalents, file stays
//          well under the 300-500 line guideline.
//
// See also: specs/compressors/spec.md,
//           specs/compressors/data-model.md,
//           specs/compressors/contracts/gain-computer-api.md

namespace acfx {

enum class GainMode : std::uint8_t { compress, limit, expand, gate };

class GainComputer {
public:
    // Configuration setters — store parameters; guarded against degenerate
    // input (FR-024). No cached per-sample coefficients are needed: the map
    // is direct arithmetic, computed fresh in computeGainDb().
    void setMode(GainMode mode) noexcept { mode_ = mode; }
    void setThreshold(float dB) noexcept { thresholdDb_ = dB; }
    // ratio < 1 guarded to 1; limit mode treats ratio as infinite regardless.
    void setRatio(float ratio) noexcept { ratio_ = ratio; }
    // 0 = hard corner; > 0 = unified quadratic C1 knee straddling threshold.
    void setKnee(float dB) noexcept { kneeDb_ = dB; }
    // Expander/gate max attenuation (floor), expected <= 0.
    void setRange(float dB) noexcept { rangeDb_ = dB; }

    // Pure static curve: input level in dB -> gain change in dB (<= 0 =
    // attenuation). No runtime state; identical inputs -> identical outputs,
    // call-order independent.
    //
    // Stub (task T002): returns unity (0 dB, no gain change) regardless of
    // input. The real piecewise map (per-mode branches + unified quadratic
    // C1 knee + range bound + ratio guard) is implemented in task T008.
    float computeGainDb(float levelDb) const noexcept {
        (void)levelDb;
        return 0.0f;
    }

private:
    // -----------------------------------------------------------------------
    // Configuration — all fields; GainComputer holds no other state (it is
    // stateless per FR-001).
    // -----------------------------------------------------------------------
    GainMode mode_       = GainMode::compress;
    float    thresholdDb_ = -18.0f; // dBFS (tuning-pass placeholder)
    float    ratio_       = 4.0f;   // >= 1; limit mode ignores it (infinite)
    float    kneeDb_      = 6.0f;   // dB, >= 0; 0 = hard corner
    float    rangeDb_     = -40.0f; // dB, <= 0; expand/gate attenuation floor
};

} // namespace acfx
