// USB descriptor bytes and descriptor callbacks for the NUCLEO-F446RE
// composite device (FR-018, FR-018a, FR-018b, FR-019, FR-020, FR-021,
// FR-025, FR-026, FR-027; D5, D10, D20).
//
// The numbers this file emits -- interface numbers, endpoint addresses, entity
// IDs, lengths, string indices -- all live in usb-descriptors.h, next to the
// static_asserts that keep them consistent with tusb_config.h and
// support/sample-format.h. This file is the emission and the callbacks.
//
// NOT HERE: the sample rate. UAC2 carries no rate in any descriptor -- it
// lives behind the Clock Source entity's Sampling Frequency Control, answered
// in usb-audio-controls.cpp. Half of "48 kHz / 16-bit / stereo" is in these
// bytes and half is in that file; neither is complete on its own.
//
// LINKAGE. Every function defined here is called from TinyUSB's C sources
// (src/device/usbd.c). They are declared inside tusb.h's `extern "C"` block,
// and each definition below repeats `extern "C"` explicitly so a reader does
// not have to know that. For these three the failure mode is at least loud:
// usbd.c calls them unconditionally, so a C++-mangled definition fails at
// link. (The audio control callback in usb-audio-controls.cpp is the one where
// getting this wrong is silent -- see that file's header.)
//
// FREESTANDING. Built with -fno-exceptions -fno-rtti for a bare-metal target.
// No allocation, no <string>, no iostreams. All descriptor bytes are const and
// live in flash; the only mutable state is the string-conversion scratch
// buffer, which is exactly the shape every TinyUSB example uses.

#include "usb-descriptors.h"

#include <cstddef>

#include "stm32f446xx.h"

namespace acfx::nucleo {
namespace {

// ---------------------------------------------------------------------------
// Device descriptor
// ---------------------------------------------------------------------------
//
// bDeviceClass/SubClass/Protocol MUST be 0xEF / 0x02 / 0x01 (Miscellaneous /
// Common Class / Interface Association Descriptor). This is not decoration: a
// host only looks for IADs when the device declares this triple. Leave it at
// 0x00 and the host parses the configuration as ONE function, binds the first
// interface it recognises, and silently ignores the rest -- the device
// enumerates, and two of the three functions are simply absent.
//
// VID/PID: 0xCAFE is TinyUSB's unregistered development vendor ID and the PID
// is built with the examples' own bitmap so that a device with a different mix
// of functions gets a different PID. Hosts cache driver bindings per VID/PID,
// so reusing one PID across two different interface layouts is a known way to
// produce "it worked yesterday" enumeration faults. THESE ARE DEVELOPMENT IDS
// FOR A BENCH RIG -- they are not registered and must not ship on a product.
constexpr std::uint16_t kVendorId = 0xCAFE;
constexpr std::uint16_t kProductId =
    0x4000 | (CFG_TUD_CDC ? (1u << 0) : 0u) | (CFG_TUD_MSC ? (1u << 1) : 0u) |
    (CFG_TUD_HID ? (1u << 2) : 0u) | (CFG_TUD_MIDI ? (1u << 3) : 0u) |
    (CFG_TUD_AUDIO ? (1u << 4) : 0u) | (CFG_TUD_VENDOR ? (1u << 5) : 0u);

const tusb_desc_device_t kDeviceDescriptor = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,

    // USB 2.0. Full speed is a property of the F446's OTG_FS PHY, not of
    // bcdUSB; a full-speed-only device still declares 0x0200.
    .bcdUSB = 0x0200,

    .bDeviceClass = TUSB_CLASS_MISC,        // 0xEF -- see the note above
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,// 0x02
    .bDeviceProtocol = MISC_PROTOCOL_IAD,   // 0x01

    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor = kVendorId,
    .idProduct = kProductId,
    .bcdDevice = 0x0100,

    .iManufacturer = kStrManufacturer,
    .iProduct = kStrProduct,
    .iSerialNumber = kStrSerial,

    .bNumConfigurations = 0x01,
};

}  // namespace

// ---------------------------------------------------------------------------
// Configuration descriptor
// ---------------------------------------------------------------------------
//
// Three functions, three IADs, seven interfaces, in this fixed order:
//
//   IAD [itf 0..2]  UAC2 audio      itf 0  AudioControl        (no endpoint)
//                                   itf 1  AudioStreaming OUT  alt0 / alt1+EP
//                                   itf 2  AudioStreaming IN   alt0 / alt1+EP
//   IAD [itf 3..4]  USB MIDI        itf 3  AudioControl (MIDI's own)
//                                   itf 4  MIDIStreaming       bulk pair
//   IAD [itf 5..6]  CDC ACM         itf 5  Communications      notify EP
//                                   itf 6  CDC Data            bulk pair
//
// Note that MIDI's first interface is ALSO class Audio / subclass Control.
// TinyUSB copes: audiod_open() rejects it because the interface that follows
// is MIDIStreaming rather than AudioStreaming (audio_device.c:820-902), and
// midid_open() then claims it. This is why the two audio-ish functions can
// coexist, and why the AudioControl -> AudioStreaming adjacency in the audio
// function must not be broken up.
extern const std::uint8_t kConfigurationDescriptor[kConfigTotalLen] = {

    // --- Configuration header ---------------------------------------------
    //
    // Self-powered is the truthful declaration for this rig: the board is
    // powered and clocked over its ST-Link cable, and the USB-C breakout's
    // VBUS is deliberately left unwired (see tusb_config.h's VBUS section --
    // wiring it would put two 5 V supplies in contention). bMaxPower of 100 mA
    // is a conservative request that every host grants, not a measurement of
    // what this board draws from the breakout, which is nothing.
    TUD_CONFIG_DESCRIPTOR(/*config_num*/ 1, /*_itfcount*/ kItfCount,
                          /*_stridx*/ 0, /*_total_len*/ kConfigTotalLen,
                          /*_attribute*/ TUSB_DESC_CONFIG_ATT_SELF_POWERED,
                          /*_power_ma*/ 100),

    // =======================================================================
    // FUNCTION 1 -- UAC2 audio, stereo IN and stereo OUT (FR-020, FR-021)
    // =======================================================================
    //
    // Hand-assembled from the TUD_AUDIO20_DESC_* primitives because no shipped
    // template is duplex stereo (research.md R13.8). The topology is two
    // independent one-hop paths sharing one clock entity:
    //
    //   host --[EP 0x01]--> InputTerm 0x02 (USB) --> OutputTerm 0x03 --> DSP
    //   DSP  -------------> InputTerm 0x04 --> OutputTerm 0x05 (USB) --[0x81]--> host
    //
    // There is no Feature Unit in either path. This device exposes no on-board
    // volume or mute control -- the effect's parameters arrive over MIDI CC,
    // not over UAC2 -- and a Feature Unit we declared but did not implement
    // would leave the host's volume requests unanswered.

    /* Interface Association: interfaces 0..2 are one audio function */
    TUD_AUDIO20_DESC_IAD(/*_firstitf*/ kItfNumAudioControl,
                         /*_nitfs*/ kIadItfCountAudio,
                         /*_stridx*/ kStrAudioFunction),

    /* Standard AC Interface (4.7.1) -- _nEPs 0: no status interrupt endpoint.
       Matching CFG_TUD_AUDIO_ENABLE_INTERRUPT_EP 0; audiod_open() TU_ASSERTs
       that a declared interrupt EP is backed by that macro. */
    TUD_AUDIO20_DESC_STD_AC(/*_itfnum*/ kItfNumAudioControl, /*_nEPs*/ 0x00,
                            /*_stridx*/ kStrAudioFunction),

    /* Class-Specific AC Interface Header (4.7.2). _totallen covers the entity
       descriptors that follow; the macro adds its own 9-byte header. */
    TUD_AUDIO20_DESC_CS_AC(
        /*_bcdADC*/ 0x0200, /*_category*/ AUDIO20_FUNC_IO_BOX,
        /*_totallen*/ kAudioControlEntitiesLen,
        /*_ctrl*/ (AUDIO20_CTRL_NONE << AUDIO20_CS_AS_INTERFACE_CTRL_LATENCY_POS)),

    /* Clock Source (4.7.2.1) -- the ONLY place a UAC2 device states its sample
       rate, and it states it over control requests, not in these bytes.
       _attr = INT_VAR_CLK | CLK_SYC_SOF says what FR-004/US2 now says: a
       VARIABLE-frequency clock (44.1 or 48 kHz, selectable by the host), still
       locked to the host's SOF rather than to any oscillator of ours. The
       frequency control is READ-WRITE (AUDIO20_CTRL_RW) so the host can SELECT
       a rate; the validity control stays READ-ONLY (there is nothing for a host
       to write to it, and this SOF-locked clock is always valid, FR-024).
       usb-audio-controls.cpp answers the reads AND the frequency SET request;
       declaring these controls here without answering them there would stall
       every host that asks. */
    TUD_AUDIO20_DESC_CLK_SRC(
        /*_clkid*/ kEntityClock,
        /*_attr*/ (AUDIO20_CLOCK_SOURCE_ATT_INT_VAR_CLK |
                   AUDIO20_CLOCK_SOURCE_ATT_CLK_SYC_SOF),
        /*_ctrl*/ ((AUDIO20_CTRL_RW << AUDIO20_CLOCK_SOURCE_CTRL_CLK_FRQ_POS) |
                   (AUDIO20_CTRL_R << AUDIO20_CLOCK_SOURCE_CTRL_CLK_VAL_POS)),
        /*_assocTerm*/ 0x00, /*_stridx*/ 0x00),

    /* OUT path (host -> device): USB streaming Input Terminal ... */
    TUD_AUDIO20_DESC_INPUT_TERM(
        /*_termid*/ kEntityOutStreamInputTerminal,
        /*_termtype*/ AUDIO_TERM_TYPE_USB_STREAMING,
        /*_assocTerm*/ kEntityInStreamOutputTerminal,
        /*_clkid*/ kEntityClock, /*_nchannelslogical*/ kAudioChannels,
        /*_channelcfg*/
        (AUDIO20_CHANNEL_CONFIG_FRONT_LEFT | AUDIO20_CHANNEL_CONFIG_FRONT_RIGHT),
        /*_idxchannelnames*/ 0x00, /*_ctrl*/ AUDIO20_CTRL_NONE,
        /*_stridx*/ kStrAudioStreamOut),

    /* ... feeding an Output Terminal. The terminal type describes the
       HOST-FACING ROLE, not literal hardware: from the host's side this is
       where audio it plays out goes, i.e. a speaker. There is no UAC2 terminal
       type for "a DSP block", and inventing an undefined one buys nothing but
       the risk that a host special-cases it. */
    TUD_AUDIO20_DESC_OUTPUT_TERM(
        /*_termid*/ kEntityOutStreamOutputTerminal,
        /*_termtype*/ AUDIO_TERM_TYPE_OUT_GENERIC_SPEAKER, /*_assocTerm*/ 0x00,
        /*_srcid*/ kEntityOutStreamInputTerminal, /*_clkid*/ kEntityClock,
        /*_ctrl*/ 0x0000, /*_stridx*/ 0x00),

    /* IN path (device -> host): a source terminal ... */
    TUD_AUDIO20_DESC_INPUT_TERM(
        /*_termid*/ kEntityInStreamInputTerminal,
        /*_termtype*/ AUDIO_TERM_TYPE_IN_GENERIC_MIC, /*_assocTerm*/ 0x00,
        /*_clkid*/ kEntityClock, /*_nchannelslogical*/ kAudioChannels,
        /*_channelcfg*/
        (AUDIO20_CHANNEL_CONFIG_FRONT_LEFT | AUDIO20_CHANNEL_CONFIG_FRONT_RIGHT),
        /*_idxchannelnames*/ 0x00, /*_ctrl*/ AUDIO20_CTRL_NONE, /*_stridx*/ 0x00),

    /* ... feeding the USB streaming Output Terminal. */
    TUD_AUDIO20_DESC_OUTPUT_TERM(
        /*_termid*/ kEntityInStreamOutputTerminal,
        /*_termtype*/ AUDIO_TERM_TYPE_USB_STREAMING,
        /*_assocTerm*/ kEntityOutStreamInputTerminal,
        /*_srcid*/ kEntityInStreamInputTerminal, /*_clkid*/ kEntityClock,
        /*_ctrl*/ 0x0000, /*_stridx*/ kStrAudioStreamIn),

    // Streaming interfaces 1 (OUT, FR-025) and 2 (IN, FR-026): alt 0 / alt 1
    // (16-bit) / alt 2 (packed-24-bit, T015/US3) for each direction. Extracted
    // verbatim to usb-descriptors-audio-streaming.h to keep this file under
    // the portability gate's 500-line limit -- see that file's header for the
    // inclusion contract (it is a fragment of THIS initializer list, not a
    // standalone unit).
#include "usb-descriptors-audio-streaming.h"

    // =======================================================================
    // FUNCTION 2 -- USB MIDI (FR-018; parameter control per FR-045)
    // =======================================================================
    //
    // TUD_MIDI_DESCRIPTOR emits no IAD of its own, so one is written out here
    // by hand. Its class triple is Audio / AudioControl / undefined, which is
    // the standard grouping for a USB-MIDI function (MIDI 1.0 is built on
    // Audio 1.0). Without it, a host that is parsing IADs -- which this device
    // has told it to do via bDeviceClass 0xEF -- can see MIDI's two interfaces
    // as two unrelated functions.
    /* bLength, bDescriptorType, bFirstInterface, bInterfaceCount, ... */
    kMidiIadLen, TUSB_DESC_INTERFACE_ASSOCIATION, kItfNumMidiControl,
    kIadItfCountMidi, TUSB_CLASS_AUDIO, AUDIO_SUBCLASS_CONTROL,
    AUDIO_FUNC_PROTOCOL_CODE_UNDEF, kStrMidi,

    /* One virtual cable, one bulk pair. */
    TUD_MIDI_DESCRIPTOR(/*_itfnum*/ kItfNumMidiControl, /*_stridx*/ kStrMidi,
                        /*_epout*/ kEpMidiOut, /*_epin*/ kEpMidiIn,
                        /*_epsize*/ kEpMidiSize),

    // =======================================================================
    // FUNCTION 3 -- CDC ACM serial telemetry (FR-018a, FR-018b)
    // =======================================================================
    //
    // Carried by EVERY effect firmware, not just instrumented builds
    // (FR-018b), so the device shape is identical across images and the HIL
    // harness can be pointed at any of them.
    //
    // The shipped template is used unchanged, including its interrupt
    // notification endpoint. That endpoint is not optional here: the template
    // always emits it and hard-codes bNumEndpoints = 1 on the comm interface,
    // and cdcd_open() opens whatever the descriptor declares regardless of
    // CFG_TUD_CDC_NOTIFY (research.md R14.5). It costs one IN endpoint slot of
    // five and 2 words of FIFO RAM, both of which the budget has.
    TUD_CDC_DESCRIPTOR(/*_itfnum*/ kItfNumCdcControl, /*_stridx*/ kStrCdc,
                       /*_ep_notif*/ kEpCdcNotify,
                       /*_ep_notif_size*/ kEpCdcNotifySize,
                       /*_epout*/ kEpCdcOut, /*_epin*/ kEpCdcIn,
                       /*_epsize*/ kEpCdcSize),
};

// THE check that matters. Everything above is arithmetic over macros; this
// compares that arithmetic against the bytes the compiler actually emitted. If
// wTotalLength and the array ever disagree, the build stops here instead of
// the board half-enumerating on someone's desk.
TU_VERIFY_STATIC(sizeof(kConfigurationDescriptor) == kConfigTotalLen,
                 "wTotalLength does not match the emitted descriptor bytes");

namespace {

// ---------------------------------------------------------------------------
// String descriptors
// ---------------------------------------------------------------------------
//
// Index 0 is the supported-language list (0x0409, English (US)); index 3 is
// the serial number, synthesised from silicon; the rest are ASCII literals
// widened to UTF-16LE on demand. kStringCount in the header is asserted
// against this table's real size below, so a new index added to the header
// without a string here is a build error rather than a dangling reference that
// stalls a host mid-enumeration.
const char* const kStringTable[] = {
    "\x09\x04",              // 0: LANGID -- 0x0409, handled specially below
    "acfx",                  // 1: iManufacturer
    "acfx Nucleo F446 Effect",// 2: iProduct
    nullptr,                 // 3: iSerialNumber -- from the STM32 unique ID
    "acfx Audio",            // 4: audio function / AudioControl interface
    "acfx Audio Playback",   // 5: streaming OUT, host -> device
    "acfx Audio Capture",    // 6: streaming IN, device -> host
    "acfx MIDI",             // 7: MIDI function
    "acfx Telemetry",        // 8: CDC serial function
};

static_assert(sizeof(kStringTable) / sizeof(kStringTable[0]) == kStringCount,
              "String table size disagrees with the indices in the header");

// Scratch buffer for the descriptor returned to the host: one uint16_t of
// header plus the characters. 31 characters is comfortably above the longest
// string above and above the 24-character serial. Not const, and deliberately
// a single shared buffer: the stack only ever has one string request in
// flight, which is the same assumption every TinyUSB example makes.
constexpr std::size_t kMaxStringChars = 31;
std::uint16_t sStringBuffer[1 + kMaxStringChars];

// The STM32F446's 96-bit factory-programmed unique device ID, rendered as 24
// uppercase hex characters. Using the real UID rather than a fixed literal
// means two boards on one host get distinct serials -- a host that sees two
// devices claiming the same VID/PID/serial can bind them to one another's
// cached settings. UID_BASE is ST's own constant (stm32f446xx.h:1058); this is
// silicon, so there is nothing to fall back to and no error case to handle.
std::size_t writeSerialString(std::uint16_t* out, std::size_t maxChars) {
    constexpr char kHex[] = "0123456789ABCDEF";
    constexpr std::size_t kUidWords = 3;
    constexpr std::size_t kCharsPerWord = 8;

    std::size_t count = 0;
    for (std::size_t word = 0; word < kUidWords; ++word) {
        const std::uint32_t value =
            reinterpret_cast<const volatile std::uint32_t*>(UID_BASE)[word];
        for (std::size_t nibble = 0; nibble < kCharsPerWord; ++nibble) {
            if (count >= maxChars) {
                return count;
            }
            const unsigned shift =
                static_cast<unsigned>(4 * (kCharsPerWord - 1 - nibble));
            out[count++] = static_cast<std::uint16_t>(
                kHex[(value >> shift) & 0xFu]);
        }
    }
    return count;
}

}  // namespace
}  // namespace acfx::nucleo

// ---------------------------------------------------------------------------
// TinyUSB descriptor callbacks
// ---------------------------------------------------------------------------

// Invoked on GET DEVICE DESCRIPTOR.
extern "C" std::uint8_t const* tud_descriptor_device_cb(void) {
    return reinterpret_cast<std::uint8_t const*>(&acfx::nucleo::kDeviceDescriptor);
}

// Invoked on GET CONFIGURATION DESCRIPTOR. One configuration only, so the
// index is ignored -- bNumConfigurations is 1 in the device descriptor, and a
// host will never ask for another.
extern "C" std::uint8_t const* tud_descriptor_configuration_cb(std::uint8_t index) {
    (void) index;
    return acfx::nucleo::kConfigurationDescriptor;
}

// Invoked on GET STRING DESCRIPTOR. The returned buffer must stay valid until
// the control transfer completes, which is why it is static rather than
// automatic.
//
// Returning nullptr for an unknown index makes the stack STALL the request,
// which is the correct response and is what a host expects for, e.g., the
// 0xEE Microsoft OS descriptor index it may probe for. It is only the indices
// this device's own descriptors ADVERTISE that must resolve; those are
// guarded by the static_assert on kStringTable above.
extern "C" std::uint16_t const* tud_descriptor_string_cb(std::uint8_t index,
                                                         std::uint16_t langid) {
    using namespace acfx::nucleo;
    (void) langid;

    std::size_t charCount = 0;

    if (index == kStrLangId) {
        // The language ID is raw bytes, not text: 0x0409 little-endian.
        sStringBuffer[1] = 0x0409;
        charCount = 1;
    } else if (index == kStrSerial) {
        charCount = writeSerialString(&sStringBuffer[1], kMaxStringChars);
    } else {
        if (index >= kStringCount) {
            return nullptr;
        }
        const char* text = kStringTable[index];
        if (text == nullptr) {
            return nullptr;
        }
        while (charCount < kMaxStringChars && text[charCount] != '\0') {
            sStringBuffer[1 + charCount] =
                static_cast<std::uint16_t>(static_cast<unsigned char>(text[charCount]));
            ++charCount;
        }
    }

    // bLength counts the two header bytes as well as the UTF-16 payload.
    sStringBuffer[0] = static_cast<std::uint16_t>((TUSB_DESC_STRING << 8) |
                                                  (2 * charCount + 2));
    return sStringBuffer;
}
