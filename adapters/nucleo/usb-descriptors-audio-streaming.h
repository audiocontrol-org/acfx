#pragma once

// Audio-streaming interface descriptor blocks for the UAC2 function,
// extracted verbatim from usb-descriptors.cpp to keep that file under the
// portability gate's 500-line limit (see ./scripts/check-portability.sh).
//
// THIS IS NOT A STANDALONE TRANSLATION UNIT. It is a textual fragment of the
// kConfigurationDescriptor{} braced initializer in usb-descriptors.cpp -- a
// comma-separated run of TUD_AUDIO20_DESC_* macro invocations with no
// enclosing braces of its own -- and it is `#include`d directly inside that
// initializer list, immediately after the audio function's entity
// descriptors (Clock Source / Input/Output Terminals) and immediately before
// FUNCTION 2 (USB MIDI). It relies entirely on the macros, constants
// (kItfNumAudioStreamingOut, kEntityOutStreamInputTerminal, kAudioChannels,
// kBytesPerSample[24], kBitsPerSample[24], kEpAudioOut/In, kAudioEpSize,
// kAltZeroBandwidth/kAltStreaming/kAltStreaming24, kStrAudioStreamOut/In,
// etc.) and TinyUSB/UAC2 macros already visible at the point of inclusion --
// all of which come from usb-descriptors.h and tusb.h, both already included
// by usb-descriptors.cpp before this file is pulled in. Do not include this
// file from anywhere else, and do not add a `#include` of usb-descriptors.h
// or tusb.h here: doing so would not change the emitted bytes (this file
// contributes no declarations, only initializer-list elements) but would
// misstate what actually makes this fragment compile.
//
// BYTE-FOR-BYTE CONTRACT. This is the OUT (host -> device, FR-025) and IN
// (device -> host, FR-026) streaming interfaces' alt 0 / alt 1 / alt 2 blocks
// moved out of usb-descriptors.cpp with NO value, macro argument, or ordering
// change. kAudioStreamingItfLen / kAudioFunctionLen / kConfigTotalLen (all in
// usb-descriptors.h) and the TU_VERIFY_STATIC in usb-descriptors.cpp still
// gate the emitted array's real size against this content unchanged.

    // --- Streaming interface 1: OUT, host -> device (FR-025) --------------

    /* Alt 0: zero bandwidth, no endpoint. Mandatory -- this is the setting a
       host selects to CLOSE the stream, and it is what the interface sits at
       between sessions. Without it a host has no way to release the endpoint's
       isochronous bandwidth. */
    TUD_AUDIO20_DESC_STD_AS_INT(/*_itfnum*/ kItfNumAudioStreamingOut,
                                /*_altset*/ kAltZeroBandwidth, /*_nEPs*/ 0x00,
                                /*_stridx*/ kStrAudioStreamOut),

    /* Alt 1: the one and only advertised format. */
    TUD_AUDIO20_DESC_STD_AS_INT(/*_itfnum*/ kItfNumAudioStreamingOut,
                                /*_altset*/ kAltStreaming, /*_nEPs*/ 0x01,
                                /*_stridx*/ kStrAudioStreamOut),

    TUD_AUDIO20_DESC_CS_AS_INT(
        /*_termid*/ kEntityOutStreamInputTerminal, /*_ctrl*/ AUDIO20_CTRL_NONE,
        /*_formattype*/ AUDIO20_FORMAT_TYPE_I,
        /*_formats*/ AUDIO20_DATA_FORMAT_TYPE_I_PCM,
        /*_nchannelsphysical*/ kAudioChannels,
        /*_channelcfg*/
        (AUDIO20_CHANNEL_CONFIG_FRONT_LEFT | AUDIO20_CHANNEL_CONFIG_FRONT_RIGHT),
        /*_stridx*/ 0x00),

    /* 2 bytes per subslot, 16 bits used. UAC2 carries no sample rate here. */
    TUD_AUDIO20_DESC_TYPE_I_FORMAT(/*_subslotsize*/ kBytesPerSample,
                                   /*_bitresolution*/ kBitsPerSample),

    /* SYNCHRONOUS sink (FR-001, research §R2): the device's audio timebase IS
       the USB SOF, so both iso endpoints declare SYNCHRONOUS. This replaces the
       prior ADAPTIVE (0x09) declaration -- ADAPTIVE told the host the device had
       no rate of its own, which made CoreAudio's resampler pitch-shift and add
       noise. SYNCHRONOUS bmAttributes = ISOCHRONOUS(0x01) | SYNCHRONOUS(0x0C) |
       DATA(0x00) = 0x0D. A SYNCHRONOUS OUT endpoint takes NO feedback endpoint
       (per USB 2.0 5.12.4.1, feedback accompanies ASYNCHRONOUS OUT only). */
    TUD_AUDIO20_DESC_STD_AS_ISO_EP(
        /*_ep*/ kEpAudioOut,
        /*_attr*/ (static_cast<std::uint8_t>(TUSB_XFER_ISOCHRONOUS) |
                   static_cast<std::uint8_t>(TUSB_ISO_EP_ATT_SYNCHRONOUS) |
                   static_cast<std::uint8_t>(TUSB_ISO_EP_ATT_DATA)),
        /*_maxEPsize*/ kAudioEpSize, /*_interval*/ 0x01),

    /* NON_MAX_PACKETS_OK is required, not cosmetic: it tells the host that
       short packets are acceptable. FR-028 has this device accepting 0..49
       stereo frames per packet, so declaring MAX_PACKETS_ONLY here would
       contradict the transport contract the OUT path implements. */
    TUD_AUDIO20_DESC_CS_AS_ISO_EP(
        /*_attr*/ AUDIO20_CS_AS_ISO_DATA_EP_ATT_NON_MAX_PACKETS_OK,
        /*_ctrl*/ AUDIO20_CTRL_NONE,
        /*_lockdelayunit*/ AUDIO20_CS_AS_ISO_DATA_EP_LOCK_DELAY_UNIT_MILLISEC,
        /*_lockdelay*/ 0x0001),

    /* Alt 2: packed-24-bit PCM (T015/US3, FR-005/FR-010). Identical to alt 1
       except the Type-I FORMAT declares bSubslotSize=3 / bBitResolution=24, and
       it references the SAME clock, terminal, and physical endpoint (kEpAudioOut)
       -- the only per-alt difference the host sees is the sample format. The iso
       endpoint declares the worst-case wMaxPacketSize (kAudioEpSize) that the
       driver's single FIFO is sized for; that it equals alt 1's here is because
       kAudioEpSize is already the 24-bit worst case (usb-descriptors.h). */
    TUD_AUDIO20_DESC_STD_AS_INT(/*_itfnum*/ kItfNumAudioStreamingOut,
                                /*_altset*/ kAltStreaming24, /*_nEPs*/ 0x01,
                                /*_stridx*/ kStrAudioStreamOut),

    TUD_AUDIO20_DESC_CS_AS_INT(
        /*_termid*/ kEntityOutStreamInputTerminal, /*_ctrl*/ AUDIO20_CTRL_NONE,
        /*_formattype*/ AUDIO20_FORMAT_TYPE_I,
        /*_formats*/ AUDIO20_DATA_FORMAT_TYPE_I_PCM,
        /*_nchannelsphysical*/ kAudioChannels,
        /*_channelcfg*/
        (AUDIO20_CHANNEL_CONFIG_FRONT_LEFT | AUDIO20_CHANNEL_CONFIG_FRONT_RIGHT),
        /*_stridx*/ 0x00),

    /* 3 bytes per subslot, 24 bits used. UAC2 carries no sample rate here. */
    TUD_AUDIO20_DESC_TYPE_I_FORMAT(/*_subslotsize*/ kBytesPerSample24,
                                   /*_bitresolution*/ kBitsPerSample24),

    TUD_AUDIO20_DESC_STD_AS_ISO_EP(
        /*_ep*/ kEpAudioOut,
        /*_attr*/ (static_cast<std::uint8_t>(TUSB_XFER_ISOCHRONOUS) |
                   static_cast<std::uint8_t>(TUSB_ISO_EP_ATT_SYNCHRONOUS) |
                   static_cast<std::uint8_t>(TUSB_ISO_EP_ATT_DATA)),
        /*_maxEPsize*/ kAudioEpSize, /*_interval*/ 0x01),

    TUD_AUDIO20_DESC_CS_AS_ISO_EP(
        /*_attr*/ AUDIO20_CS_AS_ISO_DATA_EP_ATT_NON_MAX_PACKETS_OK,
        /*_ctrl*/ AUDIO20_CTRL_NONE,
        /*_lockdelayunit*/ AUDIO20_CS_AS_ISO_DATA_EP_LOCK_DELAY_UNIT_MILLISEC,
        /*_lockdelay*/ 0x0001),

    // --- Streaming interface 2: IN, device -> host (FR-026) ---------------

    TUD_AUDIO20_DESC_STD_AS_INT(/*_itfnum*/ kItfNumAudioStreamingIn,
                                /*_altset*/ kAltZeroBandwidth, /*_nEPs*/ 0x00,
                                /*_stridx*/ kStrAudioStreamIn),

    TUD_AUDIO20_DESC_STD_AS_INT(/*_itfnum*/ kItfNumAudioStreamingIn,
                                /*_altset*/ kAltStreaming, /*_nEPs*/ 0x01,
                                /*_stridx*/ kStrAudioStreamIn),

    TUD_AUDIO20_DESC_CS_AS_INT(
        /*_termid*/ kEntityInStreamOutputTerminal, /*_ctrl*/ AUDIO20_CTRL_NONE,
        /*_formattype*/ AUDIO20_FORMAT_TYPE_I,
        /*_formats*/ AUDIO20_DATA_FORMAT_TYPE_I_PCM,
        /*_nchannelsphysical*/ kAudioChannels,
        /*_channelcfg*/
        (AUDIO20_CHANNEL_CONFIG_FRONT_LEFT | AUDIO20_CHANNEL_CONFIG_FRONT_RIGHT),
        /*_stridx*/ 0x00),

    TUD_AUDIO20_DESC_TYPE_I_FORMAT(/*_subslotsize*/ kBytesPerSample,
                                   /*_bitresolution*/ kBitsPerSample),

    /* SYNCHRONOUS source (FR-001, research §R2): the IN stream shares the same
       SOF-locked timebase as the OUT sink, so it declares SYNCHRONOUS too. This
       replaces the prior ASYNCHRONOUS (0x05) declaration, which presented the
       device as free-running with no feedback. SYNCHRONOUS bmAttributes =
       ISOCHRONOUS(0x01) | SYNCHRONOUS(0x0C) | DATA(0x00) = 0x0D. An IN endpoint
       never carries an explicit feedback endpoint regardless of sync type. */
    TUD_AUDIO20_DESC_STD_AS_ISO_EP(
        /*_ep*/ kEpAudioIn,
        /*_attr*/ (static_cast<std::uint8_t>(TUSB_XFER_ISOCHRONOUS) |
                   static_cast<std::uint8_t>(TUSB_ISO_EP_ATT_SYNCHRONOUS) |
                   static_cast<std::uint8_t>(TUSB_ISO_EP_ATT_DATA)),
        /*_maxEPsize*/ kAudioEpSize, /*_interval*/ 0x01),

    TUD_AUDIO20_DESC_CS_AS_ISO_EP(
        /*_attr*/ AUDIO20_CS_AS_ISO_DATA_EP_ATT_NON_MAX_PACKETS_OK,
        /*_ctrl*/ AUDIO20_CTRL_NONE,
        /*_lockdelayunit*/ AUDIO20_CS_AS_ISO_DATA_EP_LOCK_DELAY_UNIT_UNDEFINED,
        /*_lockdelay*/ 0x0000),

    /* Alt 2: packed-24-bit PCM (T015/US3, FR-005/FR-010). The IN-side mirror of
       the OUT alt 2 above: same clock/terminal/endpoint (kEpAudioIn), only the
       Type-I FORMAT differs (bSubslotSize=3 / bBitResolution=24). Lock-delay
       fields match this direction's alt 1 (UNDEFINED / 0). */
    TUD_AUDIO20_DESC_STD_AS_INT(/*_itfnum*/ kItfNumAudioStreamingIn,
                                /*_altset*/ kAltStreaming24, /*_nEPs*/ 0x01,
                                /*_stridx*/ kStrAudioStreamIn),

    TUD_AUDIO20_DESC_CS_AS_INT(
        /*_termid*/ kEntityInStreamOutputTerminal, /*_ctrl*/ AUDIO20_CTRL_NONE,
        /*_formattype*/ AUDIO20_FORMAT_TYPE_I,
        /*_formats*/ AUDIO20_DATA_FORMAT_TYPE_I_PCM,
        /*_nchannelsphysical*/ kAudioChannels,
        /*_channelcfg*/
        (AUDIO20_CHANNEL_CONFIG_FRONT_LEFT | AUDIO20_CHANNEL_CONFIG_FRONT_RIGHT),
        /*_stridx*/ 0x00),

    TUD_AUDIO20_DESC_TYPE_I_FORMAT(/*_subslotsize*/ kBytesPerSample24,
                                   /*_bitresolution*/ kBitsPerSample24),

    TUD_AUDIO20_DESC_STD_AS_ISO_EP(
        /*_ep*/ kEpAudioIn,
        /*_attr*/ (static_cast<std::uint8_t>(TUSB_XFER_ISOCHRONOUS) |
                   static_cast<std::uint8_t>(TUSB_ISO_EP_ATT_SYNCHRONOUS) |
                   static_cast<std::uint8_t>(TUSB_ISO_EP_ATT_DATA)),
        /*_maxEPsize*/ kAudioEpSize, /*_interval*/ 0x01),

    TUD_AUDIO20_DESC_CS_AS_ISO_EP(
        /*_attr*/ AUDIO20_CS_AS_ISO_DATA_EP_ATT_NON_MAX_PACKETS_OK,
        /*_ctrl*/ AUDIO20_CTRL_NONE,
        /*_lockdelayunit*/ AUDIO20_CS_AS_ISO_DATA_EP_LOCK_DELAY_UNIT_UNDEFINED,
        /*_lockdelay*/ 0x0000),
