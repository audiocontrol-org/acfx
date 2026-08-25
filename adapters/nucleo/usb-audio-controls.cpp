// UAC2 Clock Source control replies -- where this device's "48 kHz" actually
// lives (FR-020, FR-024).
//
// ===========================================================================
// WHY THIS FILE EXISTS, AND WHY IT IS SEPARATE FROM usb-descriptors.cpp
// ===========================================================================
//
// UAC2 puts NO sample rate in any descriptor. Unlike UAC1, whose Type I Format
// descriptor lists the supported frequencies inline, a UAC2 Type I Format
// descriptor states only the subslot size and bit resolution; the rate is
// readable ONLY through the Clock Source entity's Sampling Frequency Control,
// which a host reads with a class-specific GET CUR / GET RANGE request on
// endpoint 0. So "advertise exactly one 48 kHz / 16-bit / stereo format per
// direction" is not something descriptor bytes alone can satisfy -- the "16-bit
// stereo" half is in usb-descriptors.cpp, and the "48 kHz" half is here.
//
// SCOPE, STATED PLAINLY. This is not a descriptor callback, and the task that
// authored it (T027) was scoped to descriptors. No task in tasks.md owns these
// requests, and leaving them unanswered would hand the host TinyUSB's weak
// default (audio_device.c:277-336), which returns false and stalls the
// request. A host that cannot read the sample rate cannot open the stream:
// the board would enumerate, show up as an audio device, and then do nothing
// -- precisely the silent half-failure the descriptor work exists to prevent.
// It is carried in its OWN translation unit rather than folded into
// usb-descriptors.cpp so the addition is visible, reviewable, and removable in
// one piece if the operator would rather it live elsewhere.
//
// IF A LATER TASK DEFINES tud_audio_get_req_entity_cb IN nucleo-main.cpp, the
// link fails with a duplicate symbol. That is the intended outcome: one of the
// two gets deleted deliberately, rather than the codebase carrying two answers
// to one question.
//
// LINKAGE MATTERS MORE HERE THAN ANYWHERE ELSE IN THIS ADAPTER. Every audio
// application callback in TinyUSB 0.21.0 is TU_ATTR_WEAK with a permissive
// default body. A misspelled or C++-mangled definition therefore links
// CLEANLY against the weak stub -- no warning, no missing symbol -- and the
// device simply never reports its rate. `extern "C"` below is not decoration
// (research.md R13.0).

#include "usb-descriptors.h"
#include "usb-audio-service.h"
#include "sample-rate.h"

namespace acfx::nucleo {
// The currently-selected sample rate (US2, FR-004). Owned here: the frequency
// GET CUR below reports it, and the strong tud_audio_set_req_entity_cb at the
// bottom of this file is the ONLY writer. Declared extern in usb-audio-service.h
// for the poll-loop consumers (T010/T011). Default is the US2 default rate.
std::uint32_t g_currentSampleRateHz = kDefaultSampleRateHz;
}  // namespace acfx::nucleo

// The frequency control is now READ-WRITE and the clock is INT_VAR_CLK (US2,
// FR-004): the host may READ the current rate / the offered RANGE (handled in
// the GET callback below) and WRITE a new rate (handled by the strong
// tud_audio_set_req_entity_cb at the bottom of this file). The validity control
// remains read-only. A SET of an unsupported rate is refused (returns false ->
// the stack stalls it); a SET of a supported rate is stored in
// g_currentSampleRateHz.
extern "C" bool tud_audio_get_req_entity_cb(std::uint8_t rhport,
                                            tusb_control_request_t const* p_request) {
    using namespace acfx::nucleo;

    const std::uint8_t entityId = TU_U16_HIGH(p_request->wIndex);
    const std::uint8_t controlSelector = TU_U16_HIGH(p_request->wValue);

    // The clock is the only entity in this descriptor set with a host-readable
    // control -- there is no Feature Unit, no selector, no multiplier.
    // Anything else is the host asking about something we never advertised;
    // returning false stalls it, which is the honest answer.
    if (entityId != kEntityClock) {
        return false;
    }

    if (controlSelector == AUDIO20_CS_CTRL_SAM_FREQ) {
        if (p_request->bRequest == AUDIO20_CS_REQ_CUR) {
            // Report the rate the host most recently selected (US2, FR-004),
            // NOT a compile-time constant: after a successful SET (below) the
            // host reads CUR back to confirm the switch took effect.
            audio20_control_cur_4_t current = {
                static_cast<std::int32_t>(tu_htole32(g_currentSampleRateHz))};
            return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &current,
                                                              sizeof(current));
        }

        if (p_request->bRequest == AUDIO20_CS_REQ_RANGE) {
            // ONE subrange per supported rate, each with bMin == bMax and
            // bRes 0: the class spec's way of offering a DISCRETE set of rates
            // (US2, FR-004), here {44100} and {48000}. Sourced from
            // kSupportedSampleRatesHz so the RANGE the host reads and the SET it
            // is allowed to make (isSupportedSampleRate, below) can never
            // disagree -- a host that could select a rate the RANGE did not
            // advertise, or vice versa, would get wrong-speed audio with no
            // error. bRes 0 marks each subrange a single discrete point, not a
            // continuous span the host could pick an unimplemented rate within.
            constexpr int kCount = kSupportedSampleRatesCount;
            audio20_control_range_4_n_t(kCount) range = {};
            range.wNumSubRanges = tu_htole16(kCount);
            for (int i = 0; i < kCount; ++i) {
                const std::int32_t rate =
                    static_cast<std::int32_t>(tu_htole32(kSupportedSampleRatesHz[i]));
                range.subrange[i].bMin = rate;
                range.subrange[i].bMax = rate;
                range.subrange[i].bRes = 0;
            }
            return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &range,
                                                              sizeof(range));
        }

        return false;
    }

    // Clock validity. This clock is valid whenever the host is talking to us,
    // because it IS the host: the SOF is the only sample clock this device has
    // (FR-024). There is no lock to lose and no PLL to wait on, so there is no
    // state to track -- the answer is unconditionally "valid".
    if (controlSelector == AUDIO20_CS_CTRL_CLK_VALID &&
        p_request->bRequest == AUDIO20_CS_REQ_CUR) {
        audio20_control_cur_1_t valid = {1};
        return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &valid,
                                                          sizeof(valid));
    }

    return false;
}

// ===========================================================================
// T009 (US2, FR-004): Clock Source Sampling-Frequency SET handler.
//
// STRONG, extern "C", NON-inline, in a .cpp — the TASK-37 weak-callback trap
// ([[tinyusb-weak-callback-linkage-trap]]). TinyUSB ships a TU_ATTR_WEAK
// tud_audio_set_req_entity_cb whose default returns false and STALLS every SET
// (audio_device.c:357-363). A host that cannot SET the clock frequency cannot
// select 44.1 kHz — the multi-rate half of this feature would silently no-op on
// silicon. An `inline` or header definition would emit a weak COMDAT symbol the
// linker may resolve to that default instead; only a strong .cpp definition
// wins. This is the SET counterpart to the GET handler above and shares its
// g_currentSampleRateHz.
//
// The driver invokes this from audiod_control_complete (audio_device.c:1317),
// i.e. the EP0 control-transfer DATA-stage completion in tud_task() context —
// the same single execution context as the poll-loop consumers of
// g_currentSampleRateHz (D26), so the plain store below needs no atomicity.
//
// REAL-TIME / EP0 DISCIPLINE (research §R9): this handler ONLY validates the
// requested rate and stores it. No heap, no locks, no blocking, and NO effect
// re-preparation or ring reset here — reacting to the rate change (re-running
// PrepareEffect at the new rate, resetting rings) is deferred to a poll-loop
// service step (T010/T011), never done in this EP0 callback.
extern "C" bool tud_audio_set_req_entity_cb(std::uint8_t rhport,
                                            tusb_control_request_t const* p_request,
                                            std::uint8_t* pBuff) {
    (void) rhport;
    using namespace acfx::nucleo;

    const std::uint8_t entityId = TU_U16_HIGH(p_request->wIndex);
    const std::uint8_t controlSelector = TU_U16_HIGH(p_request->wValue);

    // The clock is the only entity with a writable control; the only writable
    // control is the sampling frequency, and it is written with a SET CUR.
    // Anything else is refused (return false -> the stack stalls it), which is
    // the honest answer for a control this device never advertised as writable.
    if (entityId != kEntityClock) {
        return false;
    }
    if (controlSelector != AUDIO20_CS_CTRL_SAM_FREQ ||
        p_request->bRequest != AUDIO20_CS_REQ_CUR) {
        return false;
    }

    // The 4-byte SET CUR payload (audio20_control_cur_4_t) the host wrote,
    // little-endian on the wire; tu_unaligned_read32 yields the value on this
    // little-endian target, mirroring how the driver reads it internally
    // (audio_device.c:1309) and how the shipped uac2_headset example does.
    const std::uint32_t requested = tu_unaligned_read32(pBuff);

    // Accept ONLY a rate this device actually advertises (isSupportedSampleRate
    // over the shared kSupportedSampleRatesHz table). Rejecting an unsupported
    // rate stalls the SET, so the host keeps the previously agreed rate rather
    // than silently switching to one the conversion path cannot produce.
    if (!isSupportedSampleRate(requested)) {
        return false;
    }
    g_currentSampleRateHz = requested;

    // Arm the rate-change latch (T011): this is the ONLY thing this EP0
    // callback does toward reacting to the new rate. g_rateChangeLatch is
    // usb-audio-service.h's shared RateChangeLatch instance;
    // requestRateChange() only stores the rate and raises a flag — no heap,
    // no locks, no blocking, and per that header's own comment nothing here
    // re-prepares the effect or touches a ring. The poll-loop's
    // ServiceRateChange() (rate-change-service.h, included only from
    // nucleo-main.cpp) is what consumes this and does the actual
    // reconfiguration, off EP0 context.
    g_rateChangeLatch.requestRateChange(requested);
    return true;
}

// ===========================================================================
// T047 (US7, FR-029, FR-029a, D22): audio streaming alt-setting tracking.
//
// These live HERE, in a .cpp, and are NOT `inline` — see usb-audio-service.h's
// `extern bool g_outStreaming` comment for the full reasoning. TinyUSB's
// tud_audio_set_itf_cb / tud_audio_set_itf_close_ep_cb are TU_ATTR_WEAK with a
// permissive default (audio_device.c:325-336); ONLY a STRONG definition
// overrides the weak default. An `inline` definition (as this first shipped)
// emits a COMDAT/weak symbol that the linker is free to resolve to TinyUSB's
// weak default instead — verified by disassembly, where the callback compiled
// down to `movs r0,#1; bx lr` (the default `return true`, never touching the
// state below) — so the board would link cleanly, pass every host test (which
// drives the pure logic directly), and still never detect capture-only on
// silicon. The same weak-symbol/`extern "C"` linkage trap this file's header
// comment documents for the Clock Source controls.
//
// Both requests carry the target interface in wIndex's low byte and the new
// alt setting in wValue's low byte (USB 2.0 9.4.10 SET_INTERFACE). Only the
// two audio streaming interfaces carry alt settings; MIDI/CDC do not, and any
// other itf value is ignored. Both return true: this adapter advertises no
// alt setting it would refuse, and stalling SET_INTERFACE would hang the exact
// enumeration step D22 exists to make behave well. These fire from tud_task()
// in the same single execution context as the service loop that reads the
// state (D26), so a plain bool needs no atomicity.
// ===========================================================================

namespace acfx::nucleo {
bool g_outStreaming = false;
bool g_inStreaming = false;

// T015 (US3, FR-005/FR-010): the selected PCM format. Owned here beside the
// streaming flags, written ONLY by the strong tud_audio_set_itf_cb below, read
// by the poll-loop converters (T019) and the format-transition lifecycle
// (T017/T018). Declared extern in usb-audio-service.h. Default Pcm16.
AudioFormat g_currentAudioFormat = AudioFormat::Pcm16;
}  // namespace acfx::nucleo

// Fires on a SET_INTERFACE that OPENS an endpoint (alt != 0).
//
// T015 EXTENSION: this device now advertises TWO non-zero alts per streaming
// interface -- alt 1 (16-bit) and alt 2 (packed-24-bit). So "is this direction
// streaming" is any non-zero alt (NOT just alt 1, which the pre-T015 code
// checked and which would have wrongly read a 24-bit stream as closed), and the
// alt value ALSO selects the format recorded in g_currentAudioFormat. This
// callback still ONLY records state -- no heap, no locks, no transport reset;
// the format-transition reaction is deferred to the poll loop (T018), exactly
// as the streaming-flag reconciliation already is (ServiceUsbLifecycle).
extern "C" bool tud_audio_set_itf_cb(uint8_t rhport,
                                     tusb_control_request_t const* p_request) {
    (void) rhport;
    using namespace acfx::nucleo;
    const uint8_t itf = tu_u16_low(p_request->wIndex);
    const uint8_t alt = tu_u16_low(p_request->wValue);
    const bool streaming = (alt == kAltStreaming || alt == kAltStreaming24);
    if (itf == kItfNumAudioStreamingOut) {
        g_outStreaming = streaming;
    } else if (itf == kItfNumAudioStreamingIn) {
        g_inStreaming = streaming;
    } else {
        return true;  // MIDI/CDC carry no alt settings; nothing to record.
    }
    // Record which format the selected alt declares. Both directions share one
    // format selection here; the format-transition lifecycle (T018) owns any
    // cross-direction consistency question.
    //
    // Arm the format-change latch (T018) ONLY when the alt actually selects a
    // format DIFFERENT from the one already recorded — mirrors the SET
    // callback's write-then-arm order for rate changes
    // (tud_audio_set_req_entity_cb above), and additionally guards against
    // arming on a no-op re-select of the SAME alt (e.g. the host re-issuing
    // SET_INTERFACE for the direction that did NOT change while opening the
    // other one): g_formatChangeLatch is usb-audio-service.h's shared
    // FormatChangeLatch instance; requestFormatChange() only stores the new
    // format and raises a flag — no heap, no locks, no blocking, and per that
    // header's own comment nothing here resets a ring or touches the DSP. The
    // poll-loop's ServiceFormatChange() (format-change-service.h, included
    // only from nucleo-main.cpp) is what consumes this and performs the
    // transport reset/re-prime, off EP0 context.
    if (alt == kAltStreaming && g_currentAudioFormat != AudioFormat::Pcm16) {
        g_currentAudioFormat = AudioFormat::Pcm16;
        g_formatChangeLatch.requestFormatChange(AudioFormat::Pcm16);
    } else if (alt == kAltStreaming24 && g_currentAudioFormat != AudioFormat::Pcm24) {
        g_currentAudioFormat = AudioFormat::Pcm24;
        g_formatChangeLatch.requestFormatChange(AudioFormat::Pcm24);
    }
    return true;
}

// Fires on a SET_INTERFACE that CLOSES an endpoint (alt == 0).
extern "C" bool tud_audio_set_itf_close_ep_cb(uint8_t rhport,
                                              tusb_control_request_t const* p_request) {
    (void) rhport;
    const uint8_t itf = tu_u16_low(p_request->wIndex);
    const uint8_t alt = tu_u16_low(p_request->wValue);
    if (alt != acfx::nucleo::kAltZeroBandwidth) {
        return true;
    }
    if (itf == acfx::nucleo::kItfNumAudioStreamingOut) {
        acfx::nucleo::g_outStreaming = false;
    } else if (itf == acfx::nucleo::kItfNumAudioStreamingIn) {
        acfx::nucleo::g_inStreaming = false;
    }
    return true;
}
