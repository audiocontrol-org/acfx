#pragma once

#include <array>
#include <cmath>
#include <cstddef>

#include "dsp/audio-block.h"
#include "effects/program-dependent-saturation/program-dependent-saturation-core.h"

// Per-block process ROUTING for ProgramDependentSaturationEffect (T034 external
// key + T036 stereo linking), split out of program-dependent-saturation-effect.h
// to keep that wrapper within the ~300-500 line budget (FR-025). These are pure
// free functions over the channel-core array (composition, not inheritance):
// they own only the key-mapping + linked/perChannel dispatch, delegating all DSP
// to the composed ProgramDependentSaturationCore. Allocation-free / lock-free /
// bounded (Constitution VI): the only scratch is a bounded stack array of channel
// pointers. Mirrors the routing idiom in CompressorEffect::processBlock/keyAt.

namespace acfx::pds {

// The key sample for main channel `ch` at sample `n`. No sidechain block
// (sc == nullptr / empty) => the channel's own input. With a sidechain: use its
// channel `ch`, falling back to channel 0 when it has fewer channels (a mono key
// feeds every channel). Read-only, so the detect pass sees pre-write values.
// MIRRORS CompressorEffect::keyAt EXACTLY.
//
// SAMPLE-COUNT rule: a sidechain may carry FEWER samples than the main block.
// Never read past it — clamp the read to the last valid sample (scSamples - 1)
// so a shorter sidechain holds its final value.
//
// NOTE: whether the core actually DETECTS on this key is gated by the
// externalKey flag inside ProgramDependentSaturationCore::detectNorm
// (`externalKey_ ? key : x`); keyAt only supplies the candidate — with the
// external sidechain OFF the core ignores it and stays internal.
template <std::size_t MaxChannels>
inline float keyAt(const std::array<float*, MaxChannels>& mainCh,
                   const AudioBlock* sc, int scChannels, int scSamples,
                   int ch, int n) noexcept {
    if (sc == nullptr || scChannels <= 0 || scSamples <= 0)
        return mainCh[static_cast<std::size_t>(ch)][n];
    const int scCh = (ch < scChannels) ? ch : 0;
    const int scN  = (n < scSamples) ? n : scSamples - 1;
    return sc->channel(scCh)[scN];
}

// Shared per-block routing for both process() overloads. `sc == nullptr` =>
// internal key; `linked` selects the StereoLink mode; `externalKey` mirrors the
// effect's externalSidechain flag (only used to resolve the linked detection
// source — the perChannel path lets the core gate it). Per-block tone
// (Decision 4) is applied up-front per channel via newBlock(core.lastNorm()): a
// one-block control-rate lag, coherent for perChannel and linked (in linked
// every core carried the SAME shared norm last block).
template <std::size_t MaxChannels>
inline void processBlock(std::array<ProgramDependentSaturationCore, MaxChannels>& cores,
                         int numChannels, bool externalKey, bool linked,
                         AudioBlock& io, const AudioBlock* sc) noexcept {
    const int channels = io.numChannels() < numChannels ? io.numChannels() : numChannels;
    const int samples = io.numSamples();
    const int scChannels = (sc != nullptr) ? sc->numChannels() : 0;
    const int scSamples  = (sc != nullptr) ? sc->numSamples() : 0;

    std::array<float*, MaxChannels> mainCh{};
    for (int ch = 0; ch < channels; ++ch) {
        mainCh[static_cast<std::size_t>(ch)] = io.channel(ch);
        // Per-block tone tilt (previous block's norm). Hoisted out of the sample
        // loop so both passes below share it; cores are independent so ordering
        // across channels is irrelevant.
        ProgramDependentSaturationCore& core = cores[static_cast<std::size_t>(ch)];
        core.newBlock(core.lastNorm());
    }

    // KNOWN LIMITATION (mirrors CompressorEffect F7): in linked mode only the
    // designated core (channel 0) advances its detector / SC-HPF state; the
    // others sit idle. A linked -> perChannel switch therefore re-converges those
    // idle detectors over the attack/release time — a brief transient, NOT a
    // wrong steady state. Warming every detector was rejected: in feedBack
    // topology it would tap each channel's own previous output, reintroducing the
    // per-channel image drift linked mode exists to remove.
    if (linked && channels > 0) {
        for (int n = 0; n < samples; ++n) {
            // Detect pass: resolve each channel's detection source with the SAME
            // rule the core uses (externalKey ? key : mainInput), then take the
            // largest-magnitude source across channels.
            float maxSrc = 0.0f;
            float maxMag = -1.0f;
            for (int ch = 0; ch < channels; ++ch) {
                const float keySample = keyAt(mainCh, sc, scChannels, scSamples, ch, n);
                const float src =
                    externalKey ? keySample : mainCh[static_cast<std::size_t>(ch)][n];
                const float mag = std::fabs(src);
                if (mag > maxMag) {
                    maxMag = mag;
                    maxSrc = src;
                }
            }
            // One shared detection on the designated core; passing the already-
            // resolved source as BOTH args makes the core's internal
            // `externalKey_ ? key : x` a no-op (both branches == maxSrc). In
            // feedBack topology detectNorm ignores maxSrc and taps core 0's
            // previous output — the documented linked feedback state (channel 0).
            const float norm = cores[0].detectNorm(maxSrc, maxSrc);
            // Apply pass: the SAME norm modulates every channel (image-stable).
            for (int ch = 0; ch < channels; ++ch) {
                float* x = mainCh[static_cast<std::size_t>(ch)];
                x[n] = cores[static_cast<std::size_t>(ch)].processWithNorm(x[n], norm);
            }
        }
        return;
    }

    // perChannel: independent detection + modulation per channel. The detect pass
    // reads keyAt before the apply overwrites any output, so an internal key is
    // never read after overwrite.
    for (int ch = 0; ch < channels; ++ch) {
        float* x = mainCh[static_cast<std::size_t>(ch)];
        ProgramDependentSaturationCore& core = cores[static_cast<std::size_t>(ch)];
        for (int n = 0; n < samples; ++n) {
            const float key = keyAt(mainCh, sc, scChannels, scSamples, ch, n);
            x[n] = core.process(x[n], key);
        }
    }
}

} // namespace acfx::pds
