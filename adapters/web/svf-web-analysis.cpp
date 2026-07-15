#include "svf-web-analysis.h"

#include "svf-handle.h"

#include "dsp/audio-block.h"
#include "dsp/param-id.h"
#include "dsp/parameter.h"
#include "dsp/process-context.h"
#include "effects/svf/svf-effect.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

// ---------------------------------------------------------------------------
// How the poles/zeros are derived (authoritatively) from DaisySP's real Svf
// ---------------------------------------------------------------------------
// DaisySP's Svf (external/.cpm-cache/daisysp/.../Source/Filters/svf.cpp) is a
// "double sampled, stable" state-variable filter. Its per-sample Process(in)
// runs TWO identical passes over the state (low_, band_) with the SAME input,
// then AVERAGES the two passes' outputs. Linearising around zero (the cubic
// `drive_*band_^3` term vanishes at the operating point, and acfx never calls
// SetDrive), one pass is the LTI map on state s = [low_; band_]:
//
//   notch = x - d*band                       (d = damp_)
//   low   = low  + f*band                     (f = freq_)
//   high  = notch - low
//   band  = f*high + band
//
//   => A = [[1,      f              ],   B = [0;  f]
//           [-f,  1 - f*(d + f) ]]
//
// Two passes with the same x give the true per-input-sample recursion on the
// START-of-sample state s_k:
//
//   s_{k+1} = A^2 s_k + (A + I) B x_k          (M := A^2, N := (A+I)B)
//
// and the AVERAGED outputs become y_k = C_eff s_k + D_eff x_k, where the C_eff /
// D_eff differ per mode because Low()/High()/Band() read different signals:
//   low  reads state row [1,0]   (no feedthrough)
//   band reads state row [0,1]   (no feedthrough)
//   high = x - low - (d+f)*band  => row [-1, -(d+f)] PLUS an x feedthrough
//
// The z-domain transfer function of (M, N, C_eff, D_eff) is a real biquad
//   H(z) = (b0 z^2 + b1 z + b2) / (z^2 - tr(M) z + det(M))
// whose poles are the roots of the denominator (eigenvalues of M = A^2) and
// whose zeros are the roots of the numerator. These are validated against the
// MEASURED svf_get_frequency_response in the vitest cross-validation test.

namespace {

constexpr double kPi = 3.14159265358979323846;

using acfx::SvfEffect;

struct Applied {
    double cutoffHz;
    double resonance;
    int mode;
};

// Reconstruct the applied (denormalized) parameter values from the handle,
// mirroring exactly what the effect's applyPending() does (same kParams table,
// same denormalize()). Unset params fall back to the descriptor default — which
// is also what the live effect uses when a param was never published.
Applied appliedParams(const SvfHandle* h) {
    const auto& p = SvfEffect::kParams;
    const double cutoff = h->paramSet[0] ? acfx::denormalize(p[0], h->paramNorm[0])
                                         : static_cast<double>(p[0].defaultValue);
    const double res = h->paramSet[1] ? acfx::denormalize(p[1], h->paramNorm[1])
                                      : static_cast<double>(p[1].defaultValue);
    const int mode = h->paramSet[2] ? static_cast<int>(acfx::denormalize(p[2], h->paramNorm[2]))
                                    : static_cast<int>(p[2].defaultValue);
    return {cutoff, res, mode};
}

// Build a fresh, at-rest SvfEffect with the handle's prepared sample rate and
// published params, then run an impulse of amplitude `amp` and length n through
// the REAL SvfEffect::process(). Ground-truth (Principle VII); does not disturb
// the caller's live handle.
//
// DaisySP's Svf is mildly NONLINEAR at resonance (its update carries a
// `- drive_*band_^3` term, drive_ = 0.5*res). A UNIT impulse therefore drives it
// out of its small-signal regime near a sharp resonant peak. The LINEAR transfer
// function (poles/zeros) is the small-signal response, so the measured frequency
// response is taken at a small amplitude (and de-scaled) to compare like with
// like; the visible impulse response keeps the literal unit amplitude.
void realImpulse(const SvfHandle* h, float* out, int n, float amp) {
    if (n <= 0)
        return;
    SvfEffect fx;
    fx.prepare(acfx::ProcessContext{h->sampleRate, n, 1});
    for (int i = 0; i < 3; ++i)
        if (h->paramSet[i])
            fx.setParameter(acfx::ParamId{static_cast<std::uint8_t>(i)}, h->paramNorm[i]);
    for (int i = 0; i < n; ++i)
        out[i] = 0.0f;
    out[0] = amp;
    float* chans[1] = {out};
    acfx::AudioBlock io(chans, 1, n);
    fx.process(io); // applyPending() runs first, so coefficients apply before sample 0
}

// Reconstruct DaisySP's freq_ / damp_ for the applied (clamped) cutoff & res,
// mirroring Svf::SetFreq / Svf::SetRes in FLOAT (so these are the coefficients
// the compiled filter actually runs with) and the acfx clamp in SvfEffect.
struct Coeffs {
    double freq;
    double damp;
};

Coeffs reconstructCoeffs(double sampleRate, double cutoffHz, double resonance) {
    const float sr = static_cast<float>(sampleRate);
    // acfx SvfEffect::clampedCutoff(): [20, sr*0.32]
    float fc = static_cast<float>(cutoffHz);
    const float acfxMax = sr * 0.32f;
    if (fc > acfxMax)
        fc = acfxMax;
    if (fc < 20.0f)
        fc = 20.0f;
    // DaisySP Svf::SetFreq fclamp: [1e-6, sr/3] (no-op given the tighter acfx clamp)
    const float dsyMax = sr / 3.0f;
    if (fc > dsyMax)
        fc = dsyMax;
    if (fc < 1.0e-6f)
        fc = 1.0e-6f;
    float r = static_cast<float>(resonance);
    if (r < 0.0f)
        r = 0.0f;
    if (r > 1.0f)
        r = 1.0f;
    // freq_ = 2*sin(PI * min(0.25, fc/(sr*2)));  damp_ = min(2*(1-res^0.25), min(2, 2/freq - freq/2))
    const float freqf = 2.0f * std::sin(static_cast<float>(kPi) * std::min(0.25f, fc / (sr * 2.0f)));
    const float dampf =
        std::min(2.0f * (1.0f - std::pow(r, 0.25f)), std::min(2.0f, 2.0f / freqf - freqf * 0.5f));
    return {static_cast<double>(freqf), static_cast<double>(dampf)};
}

// Roots of a*z^2 + b*z + c. Returns two complex roots (conjugate pair when the
// discriminant is negative). Degenerate leading coeff => repeated finite root.
void quadraticRoots(double a, double b, double c, std::complex<double>& r0,
                    std::complex<double>& r1) {
    if (std::abs(a) < 1e-30) {
        const double root = (std::abs(b) < 1e-30) ? 0.0 : -c / b;
        r0 = std::complex<double>(root, 0.0);
        r1 = r0;
        return;
    }
    const double disc = b * b - 4.0 * a * c;
    if (disc >= 0.0) {
        const double s = std::sqrt(disc);
        r0 = std::complex<double>((-b + s) / (2.0 * a), 0.0);
        r1 = std::complex<double>((-b - s) / (2.0 * a), 0.0);
    } else {
        const double s = std::sqrt(-disc);
        const double re = -b / (2.0 * a);
        const double im = s / (2.0 * a);
        r0 = std::complex<double>(re, im);
        r1 = std::complex<double>(re, -im);
    }
}

} // namespace

extern "C" {

void svf_render_impulse(SvfHandle* h, float* out, int numSamples) {
    realImpulse(h, out, numSamples, 1.0f); // literal unit impulse response
}

void svf_get_frequency_response(SvfHandle* h, const float* freqsHz, float* magsOut, int n) {
    // Render the (small-signal) impulse response long enough for it to fully
    // decay even at high resonance, then DTFT it directly at each requested
    // frequency. The small amplitude keeps DaisySP's Svf in its linear regime so
    // this measures the true |H(f)| (de-scaled by the amplitude), not resonance
    // distortion.
    constexpr int kImpulseLen = 16384;
    constexpr float kAmp = 1.0e-3f;
    std::vector<float> ir(static_cast<std::size_t>(kImpulseLen));
    realImpulse(h, ir.data(), kImpulseLen, kAmp);
    const double sr = h->sampleRate;
    const double invAmp = 1.0 / static_cast<double>(kAmp);
    for (int k = 0; k < n; ++k) {
        const double w = 2.0 * kPi * static_cast<double>(freqsHz[k]) / sr;
        double re = 0.0;
        double im = 0.0;
        for (int nn = 0; nn < kImpulseLen; ++nn) {
            const double phase = w * static_cast<double>(nn);
            const double hh = static_cast<double>(ir[static_cast<std::size_t>(nn)]);
            re += hh * std::cos(phase);
            im -= hh * std::sin(phase);
        }
        magsOut[k] = static_cast<float>(std::sqrt(re * re + im * im) * invAmp);
    }
}

void svf_get_pole_zero(SvfHandle* h, float* polesOut, float* zerosOut, float* gainOut,
                       int* countsOut) {
    const Applied ap = appliedParams(h);
    const Coeffs co = reconstructCoeffs(h->sampleRate, ap.cutoffHz, ap.resonance);
    const double f = co.freq;
    const double d = co.damp;

    // Single-pass LTI map A, B (see the file-top derivation).
    const double A11 = 1.0;
    const double A12 = f;
    const double A21 = -f;
    const double A22 = 1.0 - f * (d + f);
    const double B1 = 0.0;
    const double B2 = f;

    // M = A^2 (per-input-sample state transition of the double-sampled filter).
    const double M11 = A11 * A11 + A12 * A21;
    const double M12 = A11 * A12 + A12 * A22;
    const double M21 = A21 * A11 + A22 * A21;
    const double M22 = A21 * A12 + A22 * A22;
    const double trM = M11 + M22;
    const double detM = M11 * M22 - M12 * M21;

    // N = (A + I) B.
    const double N1 = (A11 + 1.0) * B1 + A12 * B2;
    const double N2 = A21 * B1 + (A22 + 1.0) * B2;
    // A B (used in the low/band feedthrough term 2B + AB).
    const double AB1 = A11 * B1 + A12 * B2;
    const double AB2 = A21 * B1 + A22 * B2;

    // Per-mode averaged output: y_k = C_eff s_k + D_eff x_k.
    double c1 = 0.0;
    double c2 = 0.0;
    double dEff = 0.0;
    if (ap.mode == 1) { // highpass: high = x - low - (d+f)*band  (row Ch, feedthrough +1)
        const double e = d + f;
        // C_eff = 0.5 * Ch * (I + A), Ch = [-1, -e]; (I+A) = [[2,f],[-f,1+A22]]
        c1 = 0.5 * (-2.0 + e * f);
        c2 = 0.5 * (-f - e * (1.0 + A22));
        const double chB = -e * B2; // Ch . B
        dEff = 0.5 * (2.0 + chB);
    } else if (ap.mode == 2) { // bandpass: reads state row [0,1]
        c1 = 0.5 * (A21 + M21);
        c2 = 0.5 * (A22 + M22);
        dEff = 0.5 * (2.0 * B2 + AB2);
    } else { // lowpass (default): reads state row [1,0]
        c1 = 0.5 * (A11 + M11);
        c2 = 0.5 * (A12 + M12);
        dEff = 0.5 * (2.0 * B1 + AB1);
    }

    // Numerator b0 z^2 + b1 z + b2 = D_eff*det(zI-M) + C_eff*adj(zI-M)*N.
    const double b0 = dEff;
    const double b1 = -dEff * trM + (c1 * N1 + c2 * N2);
    const double b2 =
        dEff * detM + (-c1 * N1 * M22 + c1 * M12 * N2 + c2 * M21 * N1 - c2 * N2 * M11);

    std::complex<double> p0;
    std::complex<double> p1;
    quadraticRoots(1.0, -trM, detM, p0, p1); // poles
    std::complex<double> z0;
    std::complex<double> z1;
    quadraticRoots(b0, b1, b2, z0, z1); // zeros

    polesOut[0] = static_cast<float>(p0.real());
    polesOut[1] = static_cast<float>(p0.imag());
    polesOut[2] = static_cast<float>(p1.real());
    polesOut[3] = static_cast<float>(p1.imag());
    zerosOut[0] = static_cast<float>(z0.real());
    zerosOut[1] = static_cast<float>(z0.imag());
    zerosOut[2] = static_cast<float>(z1.real());
    zerosOut[3] = static_cast<float>(z1.imag());
    *gainOut = static_cast<float>(b0);
    countsOut[0] = 2;
    countsOut[1] = 2;
    countsOut[2] = ap.mode;
}

} // extern "C"
