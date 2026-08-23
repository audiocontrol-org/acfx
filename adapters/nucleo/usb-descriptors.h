#pragma once

// USB descriptor set for the NUCLEO-F446RE composite device: UAC2 audio
// (STEREO IN *and* STEREO OUT), USB MIDI, and CDC serial, grouped into one
// composite device by Interface Association Descriptors (FR-018, FR-018a,
// FR-018b, D5).
//
// This header carries the NUMBERS -- interface numbers, endpoint addresses,
// entity IDs, the advertised audio format, and every descriptor length --
// because those numbers have three separate consumers that must agree:
//
//   1. usb-descriptors.cpp, which emits the descriptor bytes;
//   2. the alt-setting callbacks (T035 and friends), which decode a
//      SET_INTERFACE request's wIndex against OUR OWN streaming-interface
//      numbers -- TinyUSB's callbacks carry no direction argument, so the
//      interface number IS the direction discriminator (research.md R13.2);
//   3. adapters/nucleo/tusb_config.h, which sizes the driver's FIFOs against
//      the same packet size this descriptor advertises.
//
// Single-sourcing them here is not tidiness. Every mismatch in this file's
// subject matter fails SILENTLY: a wrong wTotalLength truncates enumeration, a
// wrong bNumInterfaces enumerates the device with a function missing, and a
// dangling string index can hang a host mid-enumeration. There is no error
// path to read; the symptom is a device that is merely *partly* there.
//
// PROVENANCE. Every TinyUSB macro, enum and callback name used here was read
// off the pinned tree at
// external/.cpm-cache/tinyusb/d34550b3aaa115e7ec09bea0c9e676531bf95dfb
// (TinyUSB 0.21.0), and the endpoint/FIFO budget it is built against is
// recorded with file:line citations in specs/nucleo-f446-adapter/research.md
// sections R13 (API) and R14 (budget). Nothing here is from memory.
//
// WHY HAND-ROLLED. TinyUSB ships exactly four UAC2 descriptor templates
// (src/device/usbd.h:591-660 region: MIC_ONE_CH, MIC_FOUR_CH, SPEAKER_MONO_FB,
// and the UAC1 pair) and NONE of them is duplex. The closest shipped example,
// examples/device/uac2_headset, is 1-channel IN with 2-channel OUT and
// hand-assembles its own descriptor from the TUD_AUDIO20_DESC_* primitives.
// This file does the same thing for stereo-in-with-stereo-out (FR-021, D10).

#include <cstdint>

#include "tusb.h"

#include "sample-format.h"

namespace acfx::nucleo {

// ---------------------------------------------------------------------------
// Interface numbers (FR-018 / FR-018a)
// ---------------------------------------------------------------------------
//
// USB requires interface numbers to be CONTIGUOUS FROM ZERO, and each IAD's
// bFirstInterface/bInterfaceCount must exactly cover the interfaces that
// follow it. An off-by-one here does not fail: the host simply parses one
// function short and the device enumerates with, say, MIDI missing and no
// diagnostic anywhere. The static_asserts at the bottom of this block are the
// only guard against that, so do not renumber without reading them.
//
// The order is audio -> MIDI -> CDC, and that order is load-bearing for the
// audio function specifically: TinyUSB's audiod_open() measures the length of
// "its" descriptor block by walking forward from the AudioControl interface
// until it hits either an IAD or a non-AudioStreaming interface
// (audio_device.c:867-873). Both of our terminators are present -- the MIDI
// IAD immediately follows the last audio streaming interface -- so the audio
// function's block is bounded correctly. Interleaving another function's
// interfaces between the audio ones would silently truncate that walk.
inline constexpr std::uint8_t kItfNumAudioControl = 0;
inline constexpr std::uint8_t kItfNumAudioStreamingOut = 1;  // host -> device
inline constexpr std::uint8_t kItfNumAudioStreamingIn = 2;   // device -> host
inline constexpr std::uint8_t kItfNumMidiControl = 3;
inline constexpr std::uint8_t kItfNumMidiStreaming = 4;
inline constexpr std::uint8_t kItfNumCdcControl = 5;
inline constexpr std::uint8_t kItfNumCdcData = 6;

// bNumInterfaces for the configuration descriptor. NOT a hand-count: it is
// derived from the last interface number so the two cannot drift apart.
inline constexpr std::uint8_t kItfCount = kItfNumCdcData + 1;  // 7

static_assert(kItfNumAudioStreamingOut == kItfNumAudioControl + 1);
static_assert(kItfNumAudioStreamingIn == kItfNumAudioStreamingOut + 1);
static_assert(kItfNumMidiControl == kItfNumAudioStreamingIn + 1);
static_assert(kItfNumMidiStreaming == kItfNumMidiControl + 1);
static_assert(kItfNumCdcControl == kItfNumMidiStreaming + 1);
static_assert(kItfNumCdcData == kItfNumCdcControl + 1);
static_assert(kItfCount <= CFG_TUD_INTERFACE_MAX,
              "More interfaces than the device stack will track");

// Interfaces each IAD claims. TUD_CDC_DESCRIPTOR emits its own IAD with a
// hard-coded count of 2 (usbd.h:264); the other two are written out here.
inline constexpr std::uint8_t kIadItfCountAudio = 3;  // AC + AS-out + AS-in
inline constexpr std::uint8_t kIadItfCountMidi = 2;   // AC + MS
inline constexpr std::uint8_t kIadItfCountCdc = 2;    // comm + data
static_assert(kIadItfCountAudio + kIadItfCountMidi + kIadItfCountCdc == kItfCount,
              "The three IADs must partition the interface space exactly");

// ---------------------------------------------------------------------------
// Alternate settings (FR-020, D4)
// ---------------------------------------------------------------------------
//
// Each streaming interface has exactly TWO alt settings: alt 0 carries no
// endpoint at all (the mandatory zero-bandwidth setting a host selects to
// close a stream), and alt 1 carries the one advertised format. There is no
// alt 2. Advertising a second format would let a host select a format this
// firmware does not implement -- a real hazard here, not a theoretical one,
// because the conversion path (support/sample-format.h) is 16-bit-only.
inline constexpr std::uint8_t kAltZeroBandwidth = 0;
inline constexpr std::uint8_t kAltStreaming = 1;

// ---------------------------------------------------------------------------
// Endpoint addresses (research.md R14.3)
// ---------------------------------------------------------------------------
//
// Taken unchanged from the endpoint table R14.3 derived and R14.4 computed the
// FIFO arithmetic against. Deviating from it silently invalidates that
// arithmetic, whose failure mode is `dfifo_alloc` returning false in a release
// build and ONE interface quietly failing to open (dcd_dwc2.c:237).
//
// The STM32F446's OTG_FS core provides 6 IN and 6 OUT endpoints INCLUDING EP0
// (stm32f446xx.h:15912-15913), i.e. numbers 1-5 are usable, and the driver's
// only hard guard is TU_ASSERT(epnum < ep_count) at dcd_dwc2.c:211. STM32F4 is
// not in the CFG_TUD_ENDPOINT_ONE_DIRECTION_ONLY list, so the same number may
// carry both an IN and an OUT endpoint -- which is why audio can use 0x01 and
// 0x81, and MIDI 0x02 and 0x82. This design uses 4 IN and 3 OUT non-control
// endpoints of 5 and 5 available; EP5 is free in both directions.
inline constexpr std::uint8_t kEpAudioOut = 0x01;   // iso, adaptive sink
inline constexpr std::uint8_t kEpAudioIn = 0x81;    // iso, async source
inline constexpr std::uint8_t kEpMidiOut = 0x02;    // bulk
inline constexpr std::uint8_t kEpMidiIn = 0x82;     // bulk
inline constexpr std::uint8_t kEpCdcNotify = 0x83;  // interrupt
inline constexpr std::uint8_t kEpCdcOut = 0x04;     // bulk
inline constexpr std::uint8_t kEpCdcIn = 0x84;      // bulk

// The one hard silicon limit, asserted rather than trusted.
static_assert((kEpAudioOut & 0x0F) <= 5 && (kEpAudioIn & 0x0F) <= 5 &&
                  (kEpMidiOut & 0x0F) <= 5 && (kEpMidiIn & 0x0F) <= 5 &&
                  (kEpCdcNotify & 0x0F) <= 5 && (kEpCdcOut & 0x0F) <= 5 &&
                  (kEpCdcIn & 0x0F) <= 5,
              "OTG_FS has endpoint numbers 0-5 only (stm32f446xx.h:15912)");

// wMaxPacketSize for the two bulk/interrupt functions. Bulk at full speed
// tops out at 64; the CDC notification endpoint only ever carries an 8-byte
// SERIAL_STATE notification.
inline constexpr std::uint16_t kEpMidiSize = CFG_TUD_MIDI_TX_EPSIZE;  // 64
inline constexpr std::uint16_t kEpCdcSize = CFG_TUD_CDC_TX_EPSIZE;    // 64
inline constexpr std::uint16_t kEpCdcNotifySize = 8;
static_assert(CFG_TUD_MIDI_RX_EPSIZE == CFG_TUD_MIDI_TX_EPSIZE,
              "TUD_MIDI_DESCRIPTOR takes ONE size for both directions");
static_assert(CFG_TUD_CDC_RX_EPSIZE == CFG_TUD_CDC_TX_EPSIZE,
              "TUD_CDC_DESCRIPTOR takes ONE size for both bulk directions");

// ---------------------------------------------------------------------------
// The one advertised audio format (FR-020, FR-018a, FR-018b, D21/FR-028)
// ---------------------------------------------------------------------------
//
// 48 kHz, 16-bit, stereo, in BOTH directions, and nothing else.
//
// NOTE FOR ANYONE LOOKING FOR THE SAMPLE RATE IN THE DESCRIPTOR BYTES: it is
// not there. Unlike UAC1, UAC2 carries no sample rate in any descriptor -- the
// Type I Format descriptor states only subslot size and bit resolution, and
// the rate lives exclusively behind the Clock Source entity's Sampling
// Frequency Control, which the host reads with a GET CUR / GET RANGE class
// request. That is why usb-audio-controls.cpp exists and answers those
// requests: the descriptor alone cannot say "48 kHz".
inline constexpr std::uint32_t kSampleRateHz = 48000;
inline constexpr std::uint8_t kBytesPerSample = 2;  // subslot size
inline constexpr std::uint8_t kBitsPerSample = 16;  // bit resolution
inline constexpr std::uint8_t kAudioChannels = 2;   // stereo, both directions

// wMaxPacketSize for both isochronous endpoints: 49 stereo frames, not 48.
// D21/FR-028 require accepting one extra frame per 1 ms packet, because a
// full-speed host may legitimately deliver 49 frames in a frame to stay in
// step with its own clock. Cross-checked three ways below so the descriptor,
// the driver's FIFO sizing, and the conversion path cannot disagree.
inline constexpr std::uint16_t kAudioEpSize =
    static_cast<std::uint16_t>(kMaxPacketFrames * kAudioChannels * kBytesPerSample);
static_assert(kAudioEpSize == 196);
static_assert(kAudioChannels == kChannels,
              "Descriptor channel count must match support/sample-format.h");
static_assert(kAudioEpSize == CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX,
              "Descriptor IN packet size must match the driver's IN FIFO sizing");
static_assert(kAudioEpSize == CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX,
              "Descriptor OUT packet size must match the driver's OUT FIFO sizing");
static_assert(kAudioEpSize == TUD_AUDIO_EP_SIZE(/*_is_highspeed*/ 0, kSampleRateHz,
                                                kBytesPerSample, kAudioChannels),
              "Disagrees with TinyUSB's own EP sizing helper (usbd.h:809)");
static_assert(kAudioEpSize <= 1023, "Full-speed isochronous packet ceiling");

// ---------------------------------------------------------------------------
// UAC2 entity IDs
// ---------------------------------------------------------------------------
//
// Arbitrary but must be unique within the audio function and non-zero: the
// host addresses entities by these IDs in class control requests, and
// usb-descriptors.cpp dispatches GET requests on kEntityClock.
inline constexpr std::uint8_t kEntityClock = 0x01;
inline constexpr std::uint8_t kEntityOutStreamInputTerminal = 0x02;   // USB in
inline constexpr std::uint8_t kEntityOutStreamOutputTerminal = 0x03;  // to DSP
inline constexpr std::uint8_t kEntityInStreamInputTerminal = 0x04;    // from DSP
inline constexpr std::uint8_t kEntityInStreamOutputTerminal = 0x05;   // USB out

// ---------------------------------------------------------------------------
// String descriptor indices
// ---------------------------------------------------------------------------
//
// EVERY index referenced by the device or configuration descriptor must exist
// in the table in usb-descriptors.cpp. A dangling index is not a parse error;
// on some hosts it stalls or hangs enumeration. kStringCount is asserted
// against the table's actual size there.
inline constexpr std::uint8_t kStrLangId = 0;
inline constexpr std::uint8_t kStrManufacturer = 1;
inline constexpr std::uint8_t kStrProduct = 2;
inline constexpr std::uint8_t kStrSerial = 3;
inline constexpr std::uint8_t kStrAudioFunction = 4;
inline constexpr std::uint8_t kStrAudioStreamOut = 5;
inline constexpr std::uint8_t kStrAudioStreamIn = 6;
inline constexpr std::uint8_t kStrMidi = 7;
inline constexpr std::uint8_t kStrCdc = 8;
inline constexpr std::uint8_t kStringCount = kStrCdc + 1;  // 9

// ---------------------------------------------------------------------------
// Descriptor lengths (COMPUTED, never hand-counted)
// ---------------------------------------------------------------------------
//
// wTotalLength must equal the exact number of bytes the configuration
// descriptor array actually contains. A too-small value truncates the host's
// read mid-function; a too-large value makes the host read past the end. Both
// enumerate "successfully" and lose a function. Every term below is a
// TUD_*_DESC_LEN macro from the pinned tree, and usb-descriptors.cpp closes
// the loop with a TU_VERIFY_STATIC against sizeof() of the real array -- which
// is the check that actually catches a mistake, because it compares the
// arithmetic against the emitted bytes rather than against itself.

// Class-specific AudioControl entity block (what TUD_AUDIO20_DESC_CS_AC's
// _totallen must describe; the macro adds its own 9-byte header).
inline constexpr std::uint16_t kAudioControlEntitiesLen =
    TUD_AUDIO20_DESC_CLK_SRC_LEN + TUD_AUDIO20_DESC_INPUT_TERM_LEN +
    TUD_AUDIO20_DESC_OUTPUT_TERM_LEN + TUD_AUDIO20_DESC_INPUT_TERM_LEN +
    TUD_AUDIO20_DESC_OUTPUT_TERM_LEN;  // 8 + 17 + 12 + 17 + 12 = 66

// One streaming interface: alt 0 (zero bandwidth) + alt 1 (the one format).
inline constexpr std::uint16_t kAudioStreamingItfLen =
    TUD_AUDIO20_DESC_STD_AS_LEN +          // alt 0, no endpoint
    TUD_AUDIO20_DESC_STD_AS_LEN +          // alt 1
    TUD_AUDIO20_DESC_CS_AS_INT_LEN +       //   AS general
    TUD_AUDIO20_DESC_TYPE_I_FORMAT_LEN +   //   Type I format
    TUD_AUDIO20_DESC_STD_AS_ISO_EP_LEN +   //   iso data endpoint
    TUD_AUDIO20_DESC_CS_AS_ISO_EP_LEN;     //   its class-specific companion
                                           // 9 + 9 + 16 + 6 + 7 + 8 = 55

// ***********************************************************************
// NO FEEDBACK ENDPOINT -- DELIBERATE (FR-027, D20). READ BEFORE "FIXING".
// ***********************************************************************
// There is no TUD_AUDIO20_DESC_STD_AS_ISO_FB_EP_LEN term above, and there is
// no feedback endpoint anywhere in this descriptor set. That is a design
// decision with three independent supports, not an omission:
//
//   1. This device HAS NO LOCAL CLOCK. The host's SOF is the only sample clock
//      (FR-024). A feedback endpoint exists to report the device's own rate
//      back to the host so the host can adjust its delivery rate. We have no
//      rate of our own to report; the value would be manufactured.
//   2. The USB 2.0 spec attaches explicit feedback to ASYNCHRONOUS OUT
//      endpoints. Our OUT endpoint is declared ADAPTIVE (FR-025: it consumes
//      whatever the host paces to it), and an adaptive sink takes no feedback
//      endpoint by definition. Our IN endpoint is asynchronous, and IN
//      endpoints never carry one.
//   3. The pinned driver does not want one: every feedback code path is inside
//      `#if CFG_TUD_AUDIO_ENABLE_FEEDBACK_EP`, there is no #error or TU_ASSERT
//      demanding a feedback EP when EP_OUT is enabled, and the closest shipped
//      duplex example (examples/device/uac2_headset) ships exactly this way --
//      both directions, no feedback macro at all. See research.md R13.6 for
//      the file:line evidence, and tusb_config.h's
//      CFG_TUD_AUDIO_ENABLE_FEEDBACK_EP 0 for the matching configuration.
//
// If host-side glitching under HIL ever suggests adding one, that is a
// question for the operator that also changes D20, FR-024 and FR-027 -- not a
// local descriptor edit. Adding a feedback endpoint here without changing
// CFG_TUD_AUDIO_ENABLE_FEEDBACK_EP would produce a descriptor whose feedback
// endpoint the driver never opens, which the host would wait on forever.
// ***********************************************************************

inline constexpr std::uint16_t kAudioFunctionLen =
    TUD_AUDIO20_DESC_IAD_LEN +      // 8
    TUD_AUDIO20_DESC_STD_AC_LEN +   // 9
    TUD_AUDIO20_DESC_CS_AC_LEN +    // 9
    kAudioControlEntitiesLen +      // 66
    2 * kAudioStreamingItfLen;      // 110  (OUT + IN)
static_assert(kAudioFunctionLen == 202);

// TUD_MIDI_DESCRIPTOR (usbd.h:419-424) emits an AudioControl interface and a
// MIDIStreaming interface but NO IAD, so this file supplies one -- otherwise a
// composite host may treat MIDI's two interfaces as two separate functions.
inline constexpr std::uint16_t kMidiIadLen = 8;
inline constexpr std::uint16_t kMidiFunctionLen = kMidiIadLen + TUD_MIDI_DESC_LEN;

// TUD_CDC_DESCRIPTOR (usbd.h:262-283) emits its own IAD, and it ALWAYS emits
// the interrupt notification endpoint with bNumEndpoints hard-coded to 1.
// CFG_TUD_CDC_NOTIFY 0 in tusb_config.h gates only the optional notify API,
// NOT this endpoint -- cdcd_open opens whatever the descriptor declares
// (cdc_device.c:318-325). The budget accommodates it (R14.5: it costs 2 FIFO
// words and one IN slot of five), so the shipped template is used unchanged.
inline constexpr std::uint16_t kCdcFunctionLen = TUD_CDC_DESC_LEN;  // 66

inline constexpr std::uint16_t kConfigTotalLen =
    TUD_CONFIG_DESC_LEN + kAudioFunctionLen + kMidiFunctionLen + kCdcFunctionLen;

// ---------------------------------------------------------------------------
// The emitted configuration descriptor
// ---------------------------------------------------------------------------
//
// Deliberately EXTERNALLY VISIBLE rather than file-static. These bytes are the
// single hardest thing in this feature to verify without a host, and exposing
// the array lets a host-side harness compile this same source and walk the
// real emitted bytes -- interface contiguity, IAD coverage, string-index
// resolution, endpoint numbers -- instead of a transcribed replica that can
// drift. tud_descriptor_configuration_cb() returns exactly this array.
extern const std::uint8_t kConfigurationDescriptor[kConfigTotalLen];

}  // namespace acfx::nucleo
