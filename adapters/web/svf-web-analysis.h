#pragma once

#include "svf-web-abi.h" // SvfHandle (opaque)

#ifdef __cplusplus
extern "C" {
#endif

// Analysis capability (Phase 5, visualizer-coupled). Every result is computed
// from the REAL compiled acfx::SvfEffect — never a TS/textbook re-derivation
// (FR-002/003, Principle VII). These functions may allocate scratch (they are
// NOT the audio hot path); svf_process stays allocation-free.

// Run a unit impulse through the real SvfEffect::process() (a fresh at-rest
// effect with the handle's prepared sample rate + published params) and write
// the impulse response. Trivially authoritative.
void svf_render_impulse(SvfHandle* h, float* out, int numSamples);

// MEASURED magnitude response: render the real impulse response, then evaluate
// |H(e^jw)| at each requested frequency by direct DFT of that impulse response
// (same measurement approach as tools/lesson-assets/dft.h). magsOut receives
// LINEAR magnitudes (not dB). Authoritative by measurement.
void svf_get_frequency_response(SvfHandle* h, const float* freqsHz, float* magsOut, int n);

// Poles/zeros of the 2nd-order LTI system the SVF realizes for the current
// cutoff/resonance/mode, derived from DaisySP's ACTUAL Svf difference equations
// (see svf-web-analysis.cpp). Layout:
//   polesOut : interleaved [re0, im0, re1, im1]      (countsOut[0] complex poles)
//   zerosOut : interleaved [re0, im0, re1, im1]      (countsOut[1] complex zeros)
//   gainOut  : leading numerator coefficient b0, so
//              H(z) = gain * prod(z - zero_i) / prod(z - pole_i)
//   countsOut: [numPoles, numZeros, modeIndex]
// polesOut/zerosOut must hold >= 4 floats; countsOut >= 3 ints.
void svf_get_pole_zero(SvfHandle* h, float* polesOut, float* zerosOut, float* gainOut, int* countsOut);

#ifdef __cplusplus
}
#endif
