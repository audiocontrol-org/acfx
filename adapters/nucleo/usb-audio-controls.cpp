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

// Only GET is implemented. Both Clock Source controls are declared READ-ONLY
// in the descriptor (see TUD_AUDIO20_DESC_CLK_SRC in usb-descriptors.cpp), so
// a SET request must be refused -- which the stack's weak
// tud_audio_set_req_entity_cb default already does by returning false and
// stalling. Writing our own set handler that rejected everything would be the
// same behaviour with more code; one that ACCEPTED another rate would
// contradict the descriptor and FR-020.
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
            audio20_control_cur_4_t current = {
                static_cast<std::int32_t>(tu_htole32(kSampleRateHz))};
            return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &current,
                                                              sizeof(current));
        }

        if (p_request->bRequest == AUDIO20_CS_REQ_RANGE) {
            // ONE subrange, bMin == bMax, bRes 0: the class spec's way of
            // saying "this rate and no other" (FR-020). Widening the range, or
            // adding a second subrange, would let a host select a rate the
            // conversion path (support/sample-format.h) does not implement --
            // and a host that picks an unimplemented rate does not get an
            // error, it gets wrong-speed audio.
            audio20_control_range_4_n_t(1) range = {};
            range.wNumSubRanges = tu_htole16(1);
            range.subrange[0].bMin = static_cast<std::int32_t>(tu_htole32(kSampleRateHz));
            range.subrange[0].bMax = static_cast<std::int32_t>(tu_htole32(kSampleRateHz));
            range.subrange[0].bRes = 0;
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
