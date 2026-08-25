#pragma once

// The single, host-compilable definition of AudioFormat (T018; closes the
// ODR trap TASK-12 names: T015 defined this enum in usb-audio-service.h and
// T017 defined a SECOND, textually-identical copy in format-change.h because
// format-change.h must stay free of tusb.h to compile under the `test`
// preset with no toolchain file (D1, FR-003) — usb-audio-service.h pulls in
// "tusb.h" and cannot be that home. Two definitions of the same enum are
// harmless until the same translation unit sees both (nucleo-main.cpp does,
// once format-change-service.h joins rate-change-service.h's sibling
// pattern), at which point it is an ODR violation / redefinition error.
//
// This header is the fix: ONE definition, in a leaf support/ header with no
// dependency on anything platform-specific, that both usb-audio-service.h
// and support/format-change.h include instead of each defining their own
// copy. No TinyUSB, no CMSIS, no board headers, no <cstdio> — compiles under
// the `test` preset with no toolchain file, same constraint every other
// support/ header states for itself.

#include <cstdint>

namespace acfx::nucleo {

// T015 (US3, FR-005/FR-010): which PCM sample format the host most recently
// selected via SET_INTERFACE alt setting. alt 1 -> Pcm16, alt 2 -> Pcm24.
enum class AudioFormat : std::uint8_t { Pcm16, Pcm24 };

} // namespace acfx::nucleo
