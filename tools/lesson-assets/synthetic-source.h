#pragma once

#include <vector>

// The lesson's synthetic broadband source signal.
//
// Choice (documented per T009): a 50/50 mix of a naive (non-band-limited)
// sawtooth at 110 Hz and seeded white noise. Rationale:
//   - The sawtooth's naive discontinuity gives it energy across the whole
//     audible spectrum (not just the fundamental+a few harmonics the way a
//     band-limited saw would), so an SVF sweep across cutoff/resonance
//     presets audibly and visibly (in the measured frequency response)
//     changes the clip.
//   - White noise adds a second, unpitched broadband source so the resulting
//     clip isn't purely tonal -- useful for hearing resonance/Q shape the
//     noise floor, not just harmonics of one pitch.
//   - Deterministic: the noise generator is seeded with a fixed constant
//     (see kNoiseSeed below), and the sawtooth phase always starts at 0, so
//     re-running the tool byte-for-byte reproduces the same source (and thus
//     the same content hash) given the same acfx::SvfEffect behavior.
//
// This is the RAW excitation signal fed into the real acfx::SvfEffect
// (Principle VII: the filtering itself is never faked/re-derived -- only the
// *test input* is synthetic, same as any DSP measurement rig).

namespace lessonassets {

constexpr unsigned kSyntheticSourceNoiseSeed = 20260714u; // fixed: today's date, this feature
constexpr float kSyntheticSourceSawHz = 110.0f;

// Generates `numSamples` of the documented saw+noise mix at `sampleRate`,
// peak-normalized headroom so the mix never exceeds [-1, 1].
std::vector<float> generateSyntheticSource(int sampleRate, int numSamples);

} // namespace lessonassets
