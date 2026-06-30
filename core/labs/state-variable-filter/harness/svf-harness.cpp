// svf-harness.cpp
// Host-only harness that drives the graduated acfx::SvfPrimitive and prints
// evidence to stdout:
//   (a) Per-mode frequency response — lowpass / highpass / bandpass.
//   (b) High-resonance stability — impulse response at res=0.99, bandpass mode.
//
// Include-root is core/: all acfx headers are relative to that directory.
// Compile with -std=c++20.  May allocate, loop, print.  Never included by
// any portable translation unit (contract C-1).

#include "primitives/filters/svf-primitive.h"

#include <cmath>
#include <cstdio>

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

namespace {

constexpr float kSampleRate    = 48'000.0f;
constexpr float kCutoffHz      = 1'000.0f;   // reference cutoff for all tests
constexpr double kPi           = 3.14159265358979323846;

// Stability verdict bound: peak amplitude after an impulse must stay < this.
constexpr float kBoundLimit    = 100.0f;
// Resonance value used for the stability test (near DaisySP stability limit).
constexpr float kHighRes       = 0.99f;
// Number of samples for the stability ring-down test.
constexpr int   kStabilitySamples = 200'000;

} // namespace

// ---------------------------------------------------------------------------
// Measurement helpers
// ---------------------------------------------------------------------------

// Feed a sine wave at freqHz through svf for warmupSamples (discarded), then
// integrate RMS over measureSamples.  Returns linear magnitude ratio out/in.
static double measureMagnitude(acfx::SvfPrimitive& svf,
                               float freqHz,
                               int warmupSamples  = 4096,
                               int measureSamples = 8192)
{
    const double omega = 2.0 * kPi * static_cast<double>(freqHz)
                         / static_cast<double>(kSampleRate);
    double sumSq = 0.0;

    for (int n = 0; n < warmupSamples + measureSamples; ++n) {
        const float in  = static_cast<float>(std::sin(omega * n));
        const float out = svf.process(in);
        if (n >= warmupSamples) {
            const double d = static_cast<double>(out);
            sumSq += d * d;
        }
    }

    // RMS of a unit-amplitude sine is 1/sqrt(2).
    const double inRms  = 1.0 / std::sqrt(2.0);
    const double outRms = std::sqrt(sumSq / static_cast<double>(measureSamples));
    return outRms / inRms;
}

static double toDb(double linearMag)
{
    return 20.0 * std::log10(linearMag + 1e-12);
}

// ---------------------------------------------------------------------------
// Per-mode frequency response
// ---------------------------------------------------------------------------

struct FreqPoint {
    float       hz;
    const char* label;
};

static constexpr FreqPoint kProbePoints[] = {
    {  100.0f, "  100 Hz" },
    {  500.0f, "  500 Hz" },
    { 1000.0f, " 1000 Hz" },
    { 2000.0f, " 2000 Hz" },
    { 8000.0f, " 8000 Hz" },
};

static void runModeResponse(acfx::SvfMode mode, const char* modeName)
{
    std::printf("\n[%s  cutoff=%g Hz  res=0]\n", modeName,
                static_cast<double>(kCutoffHz));

    for (const auto& pt : kProbePoints) {
        acfx::SvfPrimitive svf;
        svf.init(kSampleRate);
        svf.setFreq(kCutoffHz);
        svf.setRes(0.0f);
        svf.setMode(mode);

        const double mag = measureMagnitude(svf, pt.hz);
        const double db  = toDb(mag);
        std::printf("  %s : %+7.2f dB  (linear %.4f)\n", pt.label, db, mag);
    }
}

// ---------------------------------------------------------------------------
// High-resonance stability
// ---------------------------------------------------------------------------

static bool runStabilityCheck()
{
    std::printf("\n[High-resonance stability  res=%.2f  mode=bandpass  %d samples]\n",
                static_cast<double>(kHighRes), kStabilitySamples);

    acfx::SvfPrimitive svf;
    svf.init(kSampleRate);
    svf.setFreq(kCutoffHz);
    svf.setRes(kHighRes);
    svf.setMode(acfx::SvfMode::bandpass);

    float maxAbs    = 0.0f;
    bool  allFinite = true;

    for (int n = 0; n < kStabilitySamples; ++n) {
        const float in  = (n == 0) ? 1.0f : 0.0f;
        const float out = svf.process(in);

        if (!std::isfinite(out)) {
            allFinite = false;
            std::printf("  ERROR: non-finite output at sample %d  value=%g\n",
                        n, static_cast<double>(out));
            break;
        }

        const float absOut = std::fabs(out);
        if (absOut > maxAbs) {
            maxAbs = absOut;
        }
    }

    // Only meaningful when every sample was finite — a run that broke early on a
    // non-finite output never observed the divergent peak, so do not report it as
    // bounded (that would contradict the "all finite : NO" line above).
    const bool bounded = allFinite && (maxAbs < kBoundLimit);

    std::printf("  peak |output|  : %g\n",     static_cast<double>(maxAbs));
    std::printf("  all finite     : %s\n",      allFinite ? "YES" : "NO");
    std::printf("  bounded < %g : %s\n",
                static_cast<double>(kBoundLimit), bounded ? "YES" : "NO");

    return allFinite && bounded;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main()
{
    std::printf("=== acfx::SvfPrimitive harness ===\n");
    std::printf("sample rate: %g Hz   reference cutoff: %g Hz\n",
                static_cast<double>(kSampleRate),
                static_cast<double>(kCutoffHz));

    runModeResponse(acfx::SvfMode::lowpass,  "lowpass ");
    runModeResponse(acfx::SvfMode::highpass, "highpass");
    runModeResponse(acfx::SvfMode::bandpass, "bandpass");

    const bool stable = runStabilityCheck();

    std::printf("\n=== VERDICT ===\n");
    std::printf("stability: %s\n", stable ? "PASS" : "FAIL");

    return stable ? 0 : 1;
}
