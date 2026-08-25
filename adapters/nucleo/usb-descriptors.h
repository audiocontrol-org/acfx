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
// Each streaming interface has THREE alt settings (T015/US3, FR-005): alt 0
// carries no endpoint at all (the mandatory zero-bandwidth setting a host
// selects to close a stream), alt 1 carries the 16-bit PCM format, and alt 2
// carries the packed-24-bit PCM format. Both non-zero alts reference the SAME
// clock and terminals (a single clock domain, FR-016); they differ ONLY in the
// Type-I FORMAT descriptor's bSubslotSize/bBitResolution. This is safe now, and
// was not before US3, because the conversion path (support/sample-format.h)
// implements BOTH formats (int16 and packed-24) and the strong
// tud_audio_set_itf_cb records which one the host selected
// (g_currentAudioFormat) so the poll-loop converters pick the matching one.
inline constexpr std::uint8_t kAltZeroBandwidth = 0;
inline constexpr std::uint8_t kAltStreaming = 1;    // 16-bit PCM (bSubslotSize=2)
inline constexpr std::uint8_t kAltStreaming24 = 2;  // packed-24-bit PCM (bSubslotSize=3)

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
// 0x81, and MIDI 0x02 and 0x82. This design uses 5 IN and 3 OUT non-control
// endpoints of 5 and 5 available; with the feedback IN endpoint (0x85) added,
// the five IN endpoint NUMBERS 1..5 are now ALL in use (EP5 was previously the
// only spare) -- the device is at the OTG-FS IN-endpoint ceiling, so no further
// IN endpoint can be added without freeing one.
inline constexpr std::uint8_t kEpAudioOut = 0x01;   // iso, ASYNC sink (host-paced, feedback-regulated)
inline constexpr std::uint8_t kEpAudioIn = 0x81;    // iso, async source
inline constexpr std::uint8_t kEpMidiOut = 0x02;    // bulk
inline constexpr std::uint8_t kEpMidiIn = 0x82;     // bulk
inline constexpr std::uint8_t kEpCdcNotify = 0x83;  // interrupt
inline constexpr std::uint8_t kEpCdcOut = 0x04;     // bulk
inline constexpr std::uint8_t kEpCdcIn = 0x84;      // bulk
// Explicit-feedback IN endpoint for the ASYNC OUT stream (UAC2 5.12.4.2). It
// reports the device's OUT-FIFO fill back to the host so the host paces its
// OUT delivery to hold the FIFO half-full (AUDIO_FEEDBACK_METHOD_FIFO_COUNT).
// It rides the OUT audio's streaming interface; the driver identifies it by
// its bmAttributes usage=feedback, not by number (audio_device.c:921).
inline constexpr std::uint8_t kEpAudioFeedback = 0x85;  // iso, explicit feedback (IN)

// The one hard silicon limit, asserted rather than trusted.
static_assert((kEpAudioOut & 0x0F) <= 5 && (kEpAudioIn & 0x0F) <= 5 &&
                  (kEpMidiOut & 0x0F) <= 5 && (kEpMidiIn & 0x0F) <= 5 &&
                  (kEpCdcNotify & 0x0F) <= 5 && (kEpCdcOut & 0x0F) <= 5 &&
                  (kEpCdcIn & 0x0F) <= 5 && (kEpAudioFeedback & 0x0F) <= 5,
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
inline constexpr std::uint8_t kBytesPerSample = 2;  // alt 1 subslot size (16-bit)
inline constexpr std::uint8_t kBitsPerSample = 16;  // alt 1 bit resolution
// Packed-24-bit format carried on alt 2 (T015/US3, FR-010). 3-byte subslot,
// 24 bits used -- the signed-LE packed-24 support/sample-format.h converts.
inline constexpr std::uint8_t kBytesPerSample24 = 3;  // alt 2 subslot size (packed-24)
inline constexpr std::uint8_t kBitsPerSample24 = 24;  // alt 2 bit resolution
inline constexpr std::uint8_t kAudioChannels = 2;   // stereo, both directions

// WORST-CASE subslot across the two advertised alts. The endpoint's
// wMaxPacketSize and the driver's FIFO sizing (tusb_config.h's
// ACFX_USB_AUDIO_MAX_SUBSLOT_BYTES, kept in agreement with this by hand) are
// allocated ONCE and must hold the LARGER (24-bit) packet, so both alt
// endpoints declare this worst-case size; the per-alt Type-I FORMAT
// descriptors still declare their own bSubslotSize (2 or 3).
inline constexpr std::uint8_t kMaxSubslotBytes = kBytesPerSample24;  // 3

// wMaxPacketSize for both isochronous endpoints: 49 stereo frames, not 48,
// AT THE WORST-CASE (24-bit) subslot. D21/FR-028 require accepting one extra
// frame per 1 ms packet, because a full-speed host may legitimately deliver 49
// frames in a frame to stay in step with its own clock. Both alt endpoints
// (16-bit alt 1 and packed-24 alt 2) share this ONE physical endpoint and this
// ONE wMaxPacketSize -- it is a MAXIMUM, so declaring the 24-bit worst case on
// the 16-bit alt too is legal (the host reserves iso bandwidth for the larger
// packet) and keeps the driver's single FIFO sized right for either format.
// Cross-checked three ways below so the descriptor, the driver's FIFO sizing,
// and the conversion path cannot disagree.
inline constexpr std::uint16_t kAudioEpSize =
    static_cast<std::uint16_t>(kMaxPacketFrames * kAudioChannels * kMaxSubslotBytes);
static_assert(kAudioEpSize == 294);
static_assert(kAudioChannels == kChannels,
              "Descriptor channel count must match support/sample-format.h");
static_assert(kAudioEpSize == CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX,
              "Descriptor IN packet size must match the driver's IN FIFO sizing");
static_assert(kAudioEpSize == CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX,
              "Descriptor OUT packet size must match the driver's OUT FIFO sizing");
static_assert(kAudioEpSize == TUD_AUDIO_EP_SIZE(/*_is_highspeed*/ 0, kSampleRateHz,
                                                kMaxSubslotBytes, kAudioChannels),
              "Disagrees with TinyUSB's own EP sizing helper (usbd.h:809)");
static_assert(kAudioEpSize <= 1023, "Full-speed isochronous packet ceiling");

// wMaxPacketSize for the explicit-feedback IN endpoint. UAC2 5.12.4.2 makes
// the on-the-wire feedback value 10.14 (3 bytes) for full speed and 16.16 (4
// bytes) for high speed; TinyUSB's driver computes in 16.16 and, for a UAC2
// (bcdADC 0x0200) function, transfers 4 bytes regardless of bus speed
// (audio_device.c:610, uac_version==2 -> 4) and allocates the feedback iso
// endpoint's FIFO at 4 bytes (audio_device.c:965, usbd_edpt_iso_alloc(...,4)).
// So the endpoint is declared at 4 bytes to match what the driver actually
// moves; 4 bytes costs exactly one OTG-FS TX-FIFO word (ceil(4/4)).
inline constexpr std::uint16_t kAudioFeedbackEpSize = 4;

// ---------------------------------------------------------------------------
// FR-014 feasibility gate — OTG-FS device FIFO-RAM budget (US3, T016)
// ---------------------------------------------------------------------------
//
// The STM32F446 OTG-FS core has ONE 320-word (1.25 KB) device FIFO SPRAM
// (dwc2_stm32.h:53, DFIFO_DEPTH_FS = 320) shared across the control EP0 IN
// FIFO, one dedicated TX FIFO per IN endpoint, and one shared RX FIFO for ALL
// OUT endpoints. This is the constrained resource FR-014 gates on. The pinned
// driver (external/.cpm-cache/tinyusb/d34550b3aaa115e7ec09bea0c9e676531bf95dfb/
// src/portable/synopsys/dwc2/dcd_dwc2.c) allocates it in dfifo_alloc() and
// calc_device_grxfsiz():
//   - each IN endpoint's TX FIFO = ceil(wMaxPacketSize / 4) words  (dcd_dwc2.c:210)
//   - the shared RX FIFO (GRXFSIZ) = 13 + 1 + 2*(ceil(largestOut/4)+1)
//                                       + 2*EP_COUNT               (dcd_dwc2.c:197-199)
// EP_COUNT = USB_OTG_FS_MAX_IN_ENDPOINTS = 6 (stm32f446xx.h:15912); buffer-DMA
// is off on OTG-FS (slave mode), so all 320 words are usable (dfifo_top is not
// docked for EPInfo). Overrun is NOT a compile error on silicon — it is
// dfifo_alloc() returning false and ONE interface silently failing to open
// (dcd_dwc2.c:237). This gate makes that overrun fail the BUILD instead.
//
// This is FR-014 re-verified against the ACTUAL post-T015 resized 24-bit
// constants (kAudioEpSize is now 294 B, not the pre-US3 196 B). Every number
// below derives from the real macros — kAudioEpSize ⇐ kMaxPacketFrames ·
// kAudioChannels · kMaxSubslotBytes (packed-24, subslot 3, +1-frame jitter all
// folded in), plus the CDC/MIDI/EP0 packet sizes — never a hard-coded pass, so
// any future resize that overruns 320 words trips the static_assert.
inline constexpr unsigned kOtgFsDfifoWords = 320;  // dwc2_stm32.h:53 DFIFO_DEPTH_FS
inline constexpr unsigned kOtgFsEpCount = 6;       // stm32f446xx.h:15912 USB_OTG_FS_MAX_IN_ENDPOINTS

// A TX FIFO holds whole 32-bit words: ceil(bytes/4) (dcd_dwc2.c:210).
constexpr unsigned OtgFsFifoWords(unsigned bytes) { return (bytes + 3u) / 4u; }

// Shared RX FIFO sizing (calc_device_grxfsiz, dcd_dwc2.c:197-199): every OUT
// endpoint shares ONE FIFO, sized by the LARGEST OUT packet.
constexpr unsigned OtgFsRxFifoWords(unsigned largestOutBytes) {
    return 13u + 1u + 2u * (OtgFsFifoWords(largestOutBytes) + 1u) + 2u * kOtgFsEpCount;
}

// The device's endpoint inventory (the addresses/sizes declared above):
//   IN  TX FIFOs: EP0(64) + audio-IN(kAudioEpSize) + MIDI-IN(kEpMidiSize)
//                 + CDC-notify(kEpCdcNotifySize) + CDC-IN(kEpCdcSize)
//                 + audio-feedback(kAudioFeedbackEpSize)
//   OUT shared RX: EP0(64), audio-OUT(kAudioEpSize), MIDI-OUT(kEpMidiSize),
//                  CDC-OUT(kEpCdcSize) — audio-OUT (kAudioEpSize) is the
//                  largest, so it sizes the RX FIFO. The feedback endpoint is
//                  IN-only, so it adds a TX FIFO word but does NOT enlarge the
//                  shared RX FIFO (it never carries an OUT packet).
inline constexpr unsigned kOtgFsTxFifoWords =
    OtgFsFifoWords(CFG_TUD_ENDPOINT0_SIZE) + OtgFsFifoWords(kAudioEpSize) +
    OtgFsFifoWords(kEpMidiSize) + OtgFsFifoWords(kEpCdcNotifySize) +
    OtgFsFifoWords(kEpCdcSize) + OtgFsFifoWords(kAudioFeedbackEpSize);
inline constexpr unsigned kOtgFsLargestOutBytes =
    kAudioEpSize > kEpMidiSize
        ? (kAudioEpSize > kEpCdcSize ? kAudioEpSize : kEpCdcSize)
        : (kEpMidiSize > kEpCdcSize ? kEpMidiSize : kEpCdcSize);
inline constexpr unsigned kOtgFsRxFifoWords = OtgFsRxFifoWords(kOtgFsLargestOutBytes);
inline constexpr unsigned kOtgFsFifoWordsUsed = kOtgFsTxFifoWords + kOtgFsRxFifoWords;
inline constexpr unsigned kOtgFsFifoWordsFree = kOtgFsDfifoWords - kOtgFsFifoWordsUsed;

// THE GATE. At 48 kHz / packed-24 / subslot 3 / +1-frame jitter (all inside
// kAudioEpSize = 294 B), WITH the explicit-feedback IN endpoint (4 B = 1 word)
// now added by the async rate-matching change: TX = 16 + 74 + 16 + 2 + 16 + 1
// = 125 words, RX = 176 words (feedback is IN-only, so RX is unchanged),
// USED = 301 / 320, FREE = 19 words (5.9%). The feedback endpoint cost exactly
// the one TX-FIFO word its 4-byte packet occupies, and it consumes the last
// free OTG-FS IN endpoint slot (numbers 1..5 now all used). The margin is
// TIGHT: a further resize that overruns must NOT be squeezed in by hand-
// shrinking a buffer — it goes to the operator with the FR-014 fallback table
// (24-bit @ 44.1 kHz only / 16-bit only / a different FIFO split),
// Constitution V.
static_assert(kOtgFsFifoWordsUsed <= kOtgFsDfifoWords,
              "FR-014 OVERRUN: the current 24-bit + feedback-EP config exceeds the "
              "STM32F446 OTG-FS 320-word device FIFO RAM. Do NOT hand-shrink a buffer to "
              "fit — take the FR-014 fallback table (24-bit@44.1k-only / 16-bit-only / "
              "different FIFO split) to the operator (Constitution V).");
// Re-verification tripwire: pins the budget to the re-derived figure WITH the
// feedback endpoint. A change to the EP inventory or packet sizes that still
// FITS but moves this number is caught here so the budget is re-derived, not
// assumed.
static_assert(kOtgFsFifoWordsUsed == 301,
              "OTG-FS FIFO budget drifted from the re-derived 301/320 words (300 pre-"
              "feedback + 1 for the 4-byte feedback IN endpoint); re-derive the budget "
              "before changing EP sizing.");
static_assert(kOtgFsFifoWordsFree == 19,
              "FR-014: expected 19 free words (5.9%) at 48 kHz / 24-bit with the "
              "feedback endpoint.");

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

// One streaming interface's alt 0/1/2 block WITHOUT a feedback endpoint (this
// is the IN direction). alt 0 (zero bandwidth) + alt 1 (16-bit format) + alt 2
// (packed-24-bit format); each non-zero alt carries the same five-descriptor
// block (STD_AS interface, CS_AS general, Type-I format, iso data endpoint, its
// class-specific companion). alt 1 and alt 2 differ only in the Type-I format's
// bSubslotSize/bBitResolution (T015/US3, FR-005).
inline constexpr std::uint16_t kAudioStreamingItfLen =
    TUD_AUDIO20_DESC_STD_AS_LEN +          // alt 0, no endpoint
    TUD_AUDIO20_DESC_STD_AS_LEN +          // alt 1 (16-bit)
    TUD_AUDIO20_DESC_CS_AS_INT_LEN +       //   AS general
    TUD_AUDIO20_DESC_TYPE_I_FORMAT_LEN +   //   Type I format
    TUD_AUDIO20_DESC_STD_AS_ISO_EP_LEN +   //   iso data endpoint
    TUD_AUDIO20_DESC_CS_AS_ISO_EP_LEN +    //   its class-specific companion
    TUD_AUDIO20_DESC_STD_AS_LEN +          // alt 2 (packed-24-bit)
    TUD_AUDIO20_DESC_CS_AS_INT_LEN +       //   AS general
    TUD_AUDIO20_DESC_TYPE_I_FORMAT_LEN +   //   Type I format
    TUD_AUDIO20_DESC_STD_AS_ISO_EP_LEN +   //   iso data endpoint
    TUD_AUDIO20_DESC_CS_AS_ISO_EP_LEN;     //   its class-specific companion
                                           // 9 + (9+16+6+7+8) + (9+16+6+7+8) = 101
static_assert(kAudioStreamingItfLen == 101);

// ***********************************************************************
// EXPLICIT FEEDBACK ENDPOINT ON THE OUT STREAM (async rate matching).
// ***********************************************************************
// The OUT (host -> device) streaming interface is now ASYNCHRONOUS and carries
// an associated explicit-feedback IN endpoint on EACH non-zero alt (alt 1 and
// alt 2). This is the INDUSTRY-STANDARD UAC2 mechanism (USB 2.0 5.12.4.2),
// implemented entirely by TinyUSB's audio driver, that REPLACES the earlier
// hand-rolled "synchronous, no feedback" scheme which drained the OUT FIFO and
// injected silence on silicon. Each non-zero OUT alt therefore adds ONE
// TUD_AUDIO20_DESC_STD_AS_ISO_FB_EP (7 bytes) beyond the IN-direction block
// above, and declares nEPs = 2 (data + feedback) on its STD_AS interface.
//
// The IN (device -> host) direction takes NO feedback endpoint: IN endpoints
// never carry explicit feedback (it accompanies async OUT only). So the two
// streaming interfaces now have DIFFERENT lengths, and kAudioFunctionLen sums
// them separately rather than doubling one.
inline constexpr std::uint16_t kAudioStreamingItfOutLen =
    kAudioStreamingItfLen +
    2 * TUD_AUDIO20_DESC_STD_AS_ISO_FB_EP_LEN;  // 101 + 2*7 = 115
static_assert(kAudioStreamingItfOutLen == 115);
inline constexpr std::uint16_t kAudioStreamingItfInLen = kAudioStreamingItfLen;  // 101

inline constexpr std::uint16_t kAudioFunctionLen =
    TUD_AUDIO20_DESC_IAD_LEN +      // 8
    TUD_AUDIO20_DESC_STD_AC_LEN +   // 9
    TUD_AUDIO20_DESC_CS_AC_LEN +    // 9
    kAudioControlEntitiesLen +      // 66
    kAudioStreamingItfOutLen +      // 115 (OUT: alt1+alt2, each with a feedback EP)
    kAudioStreamingItfInLen;        // 101 (IN: alt1+alt2, no feedback EP)
static_assert(kAudioFunctionLen == 308);

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
