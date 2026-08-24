#pragma once

// The compiled-in effect instance and its startup prepare() call (T034;
// FR-036, FR-036a, FR-036b / D28). Split out of nucleo-main.cpp for the same
// file-size reason as clock-init.h, otg-fs-gpio-init.h and
// usb-audio-service.h.
//
// THE ONE NUMBER THAT MATTERS HERE: the effect is prepared with
// maxBlockSize = kBlockFrames (48), NOT kMaxPacketFrames (49).
//
// FR-036/FR-036a: the effect MUST be prepared with a maximum block size of 48
// frames, at 48 kHz, 2 channels, because that is genuinely the largest block
// process() can ever receive — the DSP processes fixed 48-frame blocks drawn
// from g_inputRing (T033), never a raw USB packet. maxBlockSize therefore
// equals the block size; there is no headroom term.
//
// FR-036b (D28): this SUPERSEDES design record D15's 49-frame prepare (D15 is
// struck through in the design record, replaced by D28). D15 sized the
// prepare to TUD_AUDIO_EP_SIZE's 49 frames back when the block followed the
// packet directly. Once the ring became the decoupling boundary (FR-030a), a
// 49-frame packet changes ring OCCUPANCY and NOTHING else — process() still
// only ever receives 48. Preparing at 49 here would let transport framing
// leak across the ring, which is the precise leak the ring's decoupling
// exists to prevent (see usb-audio-service.h's ONE READ PER PASS comment).
//
// kMaxPacketFrames (49) remains correct for the transport-side packet buffer
// (FR-028, g_outPacketBuffer in usb-audio-service.h) — it was never an
// effect-side quantity, and this file does not use it. A future reader who
// spots the 48 here and reaches for kMaxPacketFrames to "harmonize" the two
// would be reintroducing the exact leak FR-036b closes; the static_asserts
// below exist to make that edit fail the BUILD instead of failing silently
// on hardware.

#include "dsp/process-context.h"
#include "sample-format.h"
#include ACFX_EFFECT_HEADER

namespace acfx::nucleo {

// The concrete effect type/header are injected at BUILD time by the
// acfx_add_effect_nucleo CMake factory (ACFX_EFFECT_TYPE / ACFX_EFFECT_HEADER
// compile definitions — adapters/nucleo/CMakeLists.txt), one concrete type
// per firmware image (acfx_nucleo -> SvfEffect, acfx_nucleo_delay ->
// ModulatedDelayEffect). Nothing here names a concrete effect.
using AppEffect = ACFX_EFFECT_TYPE;

// Statically allocated compiled-in effect instance. Namespace scope, not a
// local in main(): no heap, no dynamic dispatch, lives in .bss for the whole
// image lifetime. T033's block-assembly/process() loop reads and mutates this
// SAME instance; this file owns only construction and the one prepare() call.
inline AppEffect g_effect;

// Restates FR-036's own number as a build-time guard, not merely a comment:
// if a future edit to sample-format.h ever changes kBlockFrames away from 48,
// or collapses the two constants into one, this fails the BUILD rather than
// silently letting transport framing reach the effect.
static_assert(kBlockFrames == 48,
              "FR-036: the effect prepare block size must be exactly 48 "
              "frames, matching the DSP's fixed block cadence (FR-036a)");
static_assert(kBlockFrames != kMaxPacketFrames,
              "FR-036b/D28: the effect-side prepare size (kBlockFrames) and "
              "the transport-side packet bound (kMaxPacketFrames) are "
              "DIFFERENT quantities on purpose — the ring is what keeps them "
              "independent. If these two constants are ever made equal, this "
              "guard is vacuous and the collapse needs re-examination against "
              "D28/FR-036b, not deletion of this assert.");

// Prepares g_effect for the one run condition this adapter ever operates
// under: 48 kHz, 48-frame blocks, stereo (FR-036). Called once from main(),
// before the service loop starts; the audio stream is not yet running at
// that point (T033's process() calls have not begun), satisfying every
// effect's "prepare while stopped" precondition.
inline void PrepareEffect() {
    const acfx::ProcessContext ctx{48000.0, kBlockFrames, kChannels};
    g_effect.prepare(ctx);
}

} // namespace acfx::nucleo
