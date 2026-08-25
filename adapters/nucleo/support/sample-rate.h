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

// Validate that a sample rate is in the supported set. Derived from
// kSupportedSampleRatesHz so the table stays the single source of truth: true
// iff hz is one of the advertised rates (44100 or 48000), false otherwise.
inline constexpr bool isSupportedSampleRate(uint32_t hz) noexcept {
    for (const uint32_t rate : kSupportedSampleRatesHz) {
        if (rate == hz) {
            return true;
        }
    }
    return false;
}

} // namespace acfx::nucleo
