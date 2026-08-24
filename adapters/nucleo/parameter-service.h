#pragma once

// The live-parameter path's shim half (T045; FR-039, FR-042, contract PSRC3).
// Binds the three things the platform-independent parameter path needs — the
// compiled-in effect instance, the shadow, and the MIDI source — and exposes
// the single call the service loop makes.
//
// Split out of nucleo-main.cpp for the same file-size reason as
// clock-init.h, otg-fs-gpio-init.h, usb-audio-service.h and
// dsp-block-service.h. It sits here rather than under support/ for the same
// reason dsp-block-service.h does: nothing below decodes a USB-MIDI packet
// wrong on its own account, but it DOES name ACFX_EFFECT_TYPE (through
// effect-instance.h), which exists only as a compile definition on the
// firmware targets, AND it touches TinyUSB's tud_midi_packet_read(). Either
// alone would already disqualify it; together they make it doubly certain
// this header could not compile in the host doctest binary, and everything
// under support/ must (D1, FR-003). The logic itself — the shadow's
// last-write-wins collapse (ParameterShadow<N>, support/parameter-shadow.h),
// the CC-to-parameter-index resolution (mapCcToParam, support/midi-cc-map.h)
// and the dead-banded ring drain (MidiParameterSource<N>,
// support/parameter-source.h) — is all platform-independent and exercised
// directly by the host tests. This file is the wiring: decode the wire
// bytes, hand Control Change pairs to the source, poll the source into the
// shadow, and flush the shadow to the effect. Nothing here holds parameter
// state of its own.

#include <cstdint>

#include "dsp/param-id.h"
#include "effect-instance.h"
#include "parameter-shadow.h"
#include "parameter-source.h"

// TinyUSB's public API. Resolved the same way usb-audio-service.h and
// nucleo-main.cpp resolve it — through acfx_nucleo_tinyusb's PUBLIC include
// directories, which also put this directory (and so tusb_config.h) on the
// path.
#include "tusb.h"

namespace acfx::nucleo {

// Parameter count of whatever effect is compiled into THIS image, the same
// generic "ask the effect, don't hardcode a number" convention
// daisy-main.cpp's boundKnobs() uses for its ADC knobs. AppEffect::
// parameters() is static constexpr (every core effect header declares it
// so), so this is a genuine compile-time constant, not a runtime read.
static constexpr int kParamCount = static_cast<int>(AppEffect::parameters().size());

// The shadow every parameter source polls into, and the one MIDI source
// this adapter has today (FR-039/FR-040; contract PSRC1 names the seam,
// support/parameter-source.h's MidiParameterSource<N> is the first
// implementation). Namespace scope, not locals in ServiceParameters(): both
// carry state (the shadow's per-slot values/dirty flags, the source's
// pending-CC ring and last-written values) that must persist across service
// loop passes, and neither allocates — sizing is fixed by kParamCount, the
// same pattern dsp-block-service.h's g_dspBlockPath/g_blockClock and
// usb-audio-service.h's g_inputRing/g_outputRing already establish for this
// adapter's other namespace-scope state.
inline ParameterShadow<kParamCount> g_paramShadow;
inline MidiParameterSource<kParamCount> g_midiSource;

// Service the live-parameter path once. Called every pass of the service
// loop, between ServiceUsbAudioOut() and ServiceDspBlock() — parameters must
// land on the effect BEFORE the block that will use them is processed, or a
// CC that arrived this same pass would not take effect until the NEXT block
// instead of this one.
//
// Three bounded steps, in order:
//
//   1. DRAIN USB MIDI. tud_midi_packet_read() reads one 4-byte USB-MIDI
//      event packet per call and returns false once the RX fifo (itf 0,
//      CFG_TUD_MIDI_RX_EPSIZE-backed) is empty — it never blocks, so this
//      loop is bounded by the fifo's own finite depth, not by an artificial
//      cap that could strand a packet already received. Packet layout
//      (verified against the pinned TinyUSB 0.21.0 tree, external/
//      .cpm-cache/tinyusb/.../src/class/midi/midi_device.h):
//        packet[0] = (cable << 4) | CIN
//        packet[1] = MIDI status byte
//        packet[2] = data1
//        packet[3] = data2
//      A Control Change has status high-nibble 0xB0 (data1 = CC number,
//      data2 = value); every other status is silently ignored by this
//      adapter — FR-039/PSRC1 name MIDI CC as the source this task wires,
//      not general MIDI handling.
//   2. POLL the source into the shadow. Today there is exactly one source;
//      a second source registered later would get its own poll() call here,
//      same as this one.
//   3. FLUSH the shadow to the effect. ParameterShadow<N>::flush() invokes
//      the closure exactly once per dirty slot, in slot order (PS4), so
//      g_effect.setParameter() is called at most kParamCount times per
//      pass and never for a slot nothing wrote this block.
//
// Safe to call from the tud_task() loop (D26: single execution context) —
// no heap, no locks, and no step above can wait: draining stops the instant
// the fifo reports empty, the poll drains a bounded ring
// (MidiParameterSource::kCapacity), and the flush walks a fixed kParamCount
// slots.
inline void ServiceParameters() {
    std::uint8_t pkt[4];
    while (tud_midi_packet_read(pkt)) {
        if ((pkt[1] & 0xF0u) == 0xB0u) {
            g_midiSource.onControlChange(pkt[2], pkt[3]);  // cc, value
        }
        // Non-CC MIDI (note on/off, pitch bend, etc.) is ignored by this
        // adapter.
    }

    g_midiSource.poll(g_paramShadow);

    g_paramShadow.flush(
        [](acfx::ParamId id, float v) { g_effect.setParameter(id, v); });
}

}  // namespace acfx::nucleo
