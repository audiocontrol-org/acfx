#pragma once

// Half-band FIR low-pass coefficients.
//
// Provenance (FR-024)
// --------------------
//   generator   : core/labs/oversampling/tools/gen-halfband.cpp
//   method      : Kaiser-windowed ideal-sinc half-band FIR, Type-I linear
//                 phase (symmetric, odd length)
//   tap count N : 91  ((N-1)/2 = 45, odd => valid half-band)
//   Kaiser beta : 8.408260 (from 85.0 dB stopband target)
//   cutoff      : 0.2500 (normalized, quarter-sample-rate)
//   targets     : stopband >= 80.0 dB, passband ripple <= 0.10 dB
//   achieved    : stopband attenuation ~= 84.438 dB, passband ripple ~= 0.00101 dB
//                 (measured via dense sweep, 20001 points over [0, 0.5])
//
// The table below is copied verbatim from the committed generator artifact
// core/labs/oversampling/tools/halfband-coeffs.generated.txt. It is
// regenerable by re-running gen-halfband.cpp with the same design
// parameters; do not hand-edit the values.
//
// Symmetric (linear-phase); center tap (index 45) is exactly 0.5; every
// other even-offset tap from center is exactly zero (the half-band
// structure exploited by the polyphase decomposition).

namespace acfx {

inline constexpr int kHalfbandTaps = 91;
inline constexpr float kHalfbandCoeffs[kHalfbandTaps] = {
     1.128547137e-05f,   0.000000000e+00f,  -3.823153747e-05f,   0.000000000e+00f,
     8.878311928e-05f,   0.000000000e+00f,  -1.742493769e-04f,   0.000000000e+00f,
     3.089073289e-04f,   0.000000000e+00f,  -5.102804862e-04f,   0.000000000e+00f,
     7.994028856e-04f,   0.000000000e+00f,  -1.201109611e-03f,   0.000000000e+00f,
     1.744432724e-03f,   0.000000000e+00f,  -2.463235520e-03f,   0.000000000e+00f,
     3.397311317e-03f,   0.000000000e+00f,  -4.594318103e-03f,   0.000000000e+00f,
     6.113198120e-03f,   0.000000000e+00f,  -8.030240424e-03f,   0.000000000e+00f,
     1.045000367e-02f,   0.000000000e+00f,  -1.352562942e-02f,   0.000000000e+00f,
     1.749859005e-02f,   0.000000000e+00f,  -2.278244868e-02f,   0.000000000e+00f,
     3.015883453e-02f,   0.000000000e+00f,  -4.130969569e-02f,   0.000000000e+00f,
     6.062671170e-02f,   0.000000000e+00f,  -1.042569950e-01f,   0.000000000e+00f,
     3.176902235e-01f,   5.000000000e-01f,   3.176902235e-01f,   0.000000000e+00f,
    -1.042569950e-01f,   0.000000000e+00f,   6.062671170e-02f,   0.000000000e+00f,
    -4.130969569e-02f,   0.000000000e+00f,   3.015883453e-02f,   0.000000000e+00f,
    -2.278244868e-02f,   0.000000000e+00f,   1.749859005e-02f,   0.000000000e+00f,
    -1.352562942e-02f,   0.000000000e+00f,   1.045000367e-02f,   0.000000000e+00f,
    -8.030240424e-03f,   0.000000000e+00f,   6.113198120e-03f,   0.000000000e+00f,
    -4.594318103e-03f,   0.000000000e+00f,   3.397311317e-03f,   0.000000000e+00f,
    -2.463235520e-03f,   0.000000000e+00f,   1.744432724e-03f,   0.000000000e+00f,
    -1.201109611e-03f,   0.000000000e+00f,   7.994028856e-04f,   0.000000000e+00f,
    -5.102804862e-04f,   0.000000000e+00f,   3.089073289e-04f,   0.000000000e+00f,
    -1.742493769e-04f,   0.000000000e+00f,   8.878311928e-05f,   0.000000000e+00f,
    -3.823153747e-05f,   0.000000000e+00f,   1.128547137e-05f
};

}  // namespace acfx
