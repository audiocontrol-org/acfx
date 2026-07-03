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

    // LINKED (FR-013/SC-011): one shared detection drives a common modulation on
    // every channel from the LARGEST-MAGNITUDE detector source ACROSS the linked
    // channels — so the loudest channel governs the whole group and the stereo
    // image stays stable. The cross-channel max is taken in the domain the active
    // topology detects in:
    //   feedForward — the resolved INPUT (externalKey ? key : mainInput). The
    //                 designated core (0) runs the shared detection incl. its
    //                 SC-HPF (detectNorm), passing the source as both args so the
    //                 core's own key fork is a no-op.
    //   feedBack    — the previous OUTPUT. Each core tracks its own prevOutput();
    //                 the max across channels (last sample's outputs) is the
    //                 shared feedback source, run through detectNormShared (no
    //                 SC-HPF on a fed-back output, matching per-channel feedback).
    // Only the designated core (0) advances the shared detector state; a linked ->
    // perChannel switch re-converges the idle detectors over the attack/release
    // time — a brief transient, not a wrong steady state.
    if (linked && channels > 0) {
        const bool feedback = cores[0].feedbackDetection();
        for (int n = 0; n < samples; ++n) {
            float norm;
            if (feedback) {
                // Cross-channel max of the PREVIOUS outputs (output domain).
                float maxOut = 0.0f;
                float maxMag = -1.0f;
                for (int ch = 0; ch < channels; ++ch) {
                    const float out = cores[static_cast<std::size_t>(ch)].prevOutput();
                    const float mag = std::fabs(out);
                    if (mag > maxMag) {
                        maxMag = mag;
                        maxOut = out;
                    }
                }
                norm = cores[0].detectNormShared(maxOut);
            } else {
                // Cross-channel max of the resolved inputs (input domain).
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
                norm = cores[0].detectNorm(maxSrc, maxSrc);
            }
            // Apply pass: the SAME norm modulates every channel (image-stable);
            // each core's prevOutput() updates here, feeding next sample's max.
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
