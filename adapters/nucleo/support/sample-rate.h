#pragma once

// Platform-independent sample-rate selection for the Nucleo USB audio adapter
// (FR-004, US2 / research §R3). No TinyUSB, no CMSIS, no board headers, no <cstdio>
// — this header compiles under the `test` preset with no toolchain file. Anything
// that cannot satisfy that constraint belongs in nucleo-main.cpp instead.

#include <cstdint>

namespace acfx::nucleo {

// Supported sample rates for this feature: 44.1 kHz and 48 kHz only (US2 / FR-004).
// Exposed for use in descriptor RANGE subranges and the supported-rate check.
inline constexpr uint32_t kSupportedSampleRatesHz[] = {44100, 48000};
inline constexpr int kSupportedSampleRatesCount = 2;

// The default/current sample rate (FR-004, R3 decision).
inline constexpr uint32_t kDefaultSampleRateHz = 48000;

// Validate that a sample rate is in the supported set.
// STUB (RED for T008) — T009 replaces with the real supported-set check.
inline constexpr bool isSupportedSampleRate(uint32_t hz) noexcept {
    // DELIBERATELY WRONG: accepts only 48000, rejects 44100 and all others.
    // This stub exists to make the RED test fail as designed.
    return hz == 48000;
}

} // namespace acfx::nucleo
