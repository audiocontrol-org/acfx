#pragma once

#include <cstdint>

#include "effects/saturation/saturation-core.h"
#include "effects/saturation/saturation-voicings.h"
#include "labs/program-dependent-saturation/dynamics-modulator.h"
#include "primitives/dynamics/envelope-follower.h"
#include "primitives/filters/svf-primitive.h"

// ProgramDependentSaturationCore — the RT-safe per-channel composition
// kernel (T003): the SURFACE only. This header declares the class's composed
// members and method signatures; method BODIES are deliberately deferred to
// T010 (the per-sample chain: sidechain HPF -> detect -> normalize ->
// per-target base+offset -> SaturationCore::process, per data-model.md
// "Per-sample chain"). This lets the Effect wrapper (T004) and the rest of
// the foundational substrate build against a stable, normative shape before
// the chain is implemented (contracts/program-dependent-saturation-effect-api.md
// "ProgramDependentSaturationCore").
//
// Composition (data-model.md "Entity — ProgramDependentSaturationCore"),
// one set per channel:
//   saturation  SaturationCore     the nonlinearity (voicings, drive/bias/
//                                  tone/mix/output) — composed UNCHANGED
//                                  (FR-004/026); no nonlinearity kernel is
//                                  added here.
//   detector    EnvelopeFollower   shared level detection + ballistics,
//                                  decibel domain; feeds all four modulators.
//   scFilter    SvfPrimitive       sidechain highpass (bypassed at 0 Hz).
//   driveMod/biasMod/toneMod/mixMod  DynamicsModulator x4  per-target signed-
//                                  offset mappers (envelope -> offset), each
//                                  with its own depth + curve, no cross-talk.
//
// DynamicsModulator is included from its PRE-GRADUATION lab path
// (core/labs/program-dependent-saturation/dynamics-modulator.h) because T009
// (the atomic graduation `git mv` to core/primitives/dynamics/) has not run
// yet as of this task; T009/T010 will update this include to the graduated
// primitive path.
//
// Platform-independent (Constitution IV): no host-framework or embedded-
// vendor headers. RT-safe by construction (Constitution VI): every member is
// a value (no heap allocation), and all coefficient/config work is destined
// for prepare()/setters, never process() — the stub bodies below do the
// minimum to compile and carry no audio-path behavior yet.
//
// See also: specs/program-dependent-saturation/spec.md,
//           specs/program-dependent-saturation/data-model.md,
//           specs/program-dependent-saturation/contracts/program-dependent-saturation-effect-api.md

namespace acfx {

enum class ModTarget : std::uint8_t { drive, bias, tone, mix };
enum class Detection : std::uint8_t { feedForward, feedBack };

class ProgramDependentSaturationCore {
public:
    // Prepares composed units; caches ref-window; no audio work. Body
    // deferred to T010.
    void prepare(float sampleRate) noexcept { (void)sampleRate; }

    // Clears detector/filter/saturation state + prevOutput. Body deferred
    // to T010.
    void reset() noexcept {}

    // ------------------------------------------------------------------
    // Static base parameters (the values the modulation offsets add to) —
    // forwarded to SaturationCore. Bodies deferred to T010.
    // ------------------------------------------------------------------
    void setStaticDrive(float gainLinear) noexcept { (void)gainLinear; }
    void setVoicing(SaturationVoicing voicing) noexcept { (void)voicing; }
    void setStaticTone(float tilt) noexcept { (void)tilt; }
    void setStaticMix(float wet) noexcept { (void)wet; }
    void setOutput(float gainLinear) noexcept { (void)gainLinear; }
    void setStaticBias(float bias) noexcept { (void)bias; }
    void setQuality(SaturationQuality quality) noexcept { (void)quality; }

    // ------------------------------------------------------------------
    // Detector (shared EnvelopeFollower) configuration. Bodies deferred
    // to T010.
    // ------------------------------------------------------------------
    void setDetectorMode(DetectMode mode) noexcept { (void)mode; }
    void setBallistics(Ballistics ballistics) noexcept { (void)ballistics; }
    void setAttack(float seconds) noexcept { (void)seconds; }
    void setRelease(float seconds) noexcept { (void)seconds; }
    void setDetection(Detection detection) noexcept { (void)detection; }
    // Normalization window (default -60..0). Body deferred to T010.
    void setRefWindow(float loDb, float hiDb) noexcept {
        (void)loDb;
        (void)hiDb;
    }

    // ------------------------------------------------------------------
    // Modulation matrix — per target depth + curve. Bodies deferred to T010.
    // ------------------------------------------------------------------
    void setDepth(ModTarget target, float signedDepth) noexcept {
        (void)target;
        (void)signedDepth;
    }
    void setCurve(ModTarget target, ModCurve curve) noexcept {
        (void)target;
        (void)curve;
    }

    // ------------------------------------------------------------------
    // Sidechain. Bodies deferred to T010.
    // ------------------------------------------------------------------
    void setExternalKey(bool enabled) noexcept { (void)enabled; }
    // 0 = bypass. Body deferred to T010.
    void setScHpf(float hz) noexcept { (void)hz; }

    // ------------------------------------------------------------------
    // Audio path. `x` is the main sample; `key` the optional external
    // sidechain sample. drive/bias/mix modulate per-sample; tone per-block
    // (call newBlock() at block start). Bodies deferred to T010.
    // ------------------------------------------------------------------

    // Applies the per-block tone offset (skipped if toneDepth==0). Stub:
    // does nothing (minimum to compile).
    void newBlock(float blockEnvNorm) noexcept { (void)blockEnvNorm; }

    // Stub: passes the main sample through unmodified (minimum to compile).
    float process(float x, float key) noexcept {
        (void)key;
        return x;
    }

private:
    // -------------------------------------------------------------------
    // Composed sub-units (data-model.md "Composed units"), one set per
    // channel.
    // -------------------------------------------------------------------
    SaturationCore   saturation_;
    EnvelopeFollower detector_;
    SvfPrimitive     scFilter_;
    DynamicsModulator driveMod_;
    DynamicsModulator biasMod_;
    DynamicsModulator toneMod_;
    DynamicsModulator mixMod_;

    // -------------------------------------------------------------------
    // Runtime state (per channel; cleared by reset(), RT-mutated in
    // process() once T010 lands).
    // -------------------------------------------------------------------
    float prevOutput_ = 0.0f;      // Feedback tap (final output y).
    float toneBlockOffset_ = 0.0f; // The per-block tone offset in effect.
};

} // namespace acfx
