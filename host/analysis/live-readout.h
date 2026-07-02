#pragma once

// host/analysis/live-readout.h
//
// The SHARED live readout both host adapters (workbench + plugin) call
// (contracts/analysis-engine-api.md "Live/offline parity"; data-model.md
// "LiveReadout"; research.md Decision 9; FR-014/FR-015/FR-016, US5;
// harmonic-analysis T029, GREEN for T028).
//
// Namespace: acfx::analysis. Host-only, header-only, off the audio thread --
// may allocate at construction (a fixed-size scratch window buffer). MUST
// NOT be #include'd from core/ (Constitution IV; scripts/check-portability.sh
// gate C-AN-DIR) and MUST NOT reach any adapter/JUCE header (this file stays
// pure host C++; the timer/thread cadence that calls update() is each
// adapter's own concern, T030/T031).
//
// Placement deviation from data-model.md: the data model sketches LiveReadout
// living separately inside adapters/workbench/ and adapters/plugin/. This
// header instead lives in host/analysis/ -- the SAME relocation rationale
// research.md Decision 1 already applied to the rest of the engine ("host/
// depends on nothing else in this feature; adapters -> host/analysis, never
// the reverse, never adapters -> each other"). Two adapter-local copies of
// this class would be two engines wired to look alike, which is exactly the
// drift FR-015's one-engine guarantee forbids; one header in host/analysis/,
// included by both adapters, is the literal shared implementation the spec
// requires. The data model's *intent* ("one implementation shared by both
// hosts") is honored; its literal path is not, mirroring Decision 1's own
// flagged deviation for the rest of the engine.
//
// ONE-ENGINE guarantee (FR-015/SC-005): update() calls the SAME
// harmonicSpectrum()/thdPlusN() entry points offline callers use, on samples
// drained verbatim (a bounded float copy, no resampling/reordering/windowing
// of its own) from the RT capture probe. No second engine, no re-derived
// math -- this class is a thin drain-then-call-the-engine adapter.
//
// Threading contract: the CaptureProbeRing is the audio (producer) thread's
// surface; LiveReadout is the analysis-thread (or UI-timer) consumer. The
// caller (an adapter's timer callback, ~15-30 Hz per FR-027) invokes update()
// off the audio thread; this class never touches the ring's push() side.

#include <cstddef>
#include <limits>
#include <stdexcept>  // std::invalid_argument
#include <string>     // std::to_string
#include <vector>

#include "analysis/spectrum.h"  // acfx::analysis::HarmonicSpectrum, harmonicSpectrum -- the ONE engine
#include "analysis/thdn.h"      // acfx::analysis::ThdnResult, thdPlusN -- the ONE engine
#include "dsp/span.h"
#include "primitives/analysis/capture-probe.h"  // acfx::CaptureProbeRing -- the RT probe (core, inward dep)

namespace acfx::analysis {

// Configuration for a live readout instance (data-model.md "LiveReadout").
// `windowSize` defaults to the FR-027 broadband default (8192-pt, ~170 ms
// @ 48 kHz); callers with a different cadence/latency budget may override it.
struct LiveReadoutConfig {
    double      fundamentalHz = 0.0;
    double      sampleRate    = 48000.0;
    int         numHarmonics  = 6;
    std::size_t windowSize    = 8192; // FR-027 default transform size
};

// Drains a CaptureProbeRing<Capacity> and runs the shared host/analysis
// engine on the drained window, exposing the latest spectrum + running THD+N
// for a UI to read (data-model.md "LiveReadout"). Pure host C++, decoupled
// from any GUI/JUCE toolkit -- an adapter's timer/thread owns the cadence
// (~15-30 Hz overlapping windows, FR-027) and simply calls update() on it;
// this class provides only the drain-and-analyze step.
template <std::size_t Capacity>
class LiveReadout {
public:
    // A non-positive fundamentalHz is a caller error (the default-constructed
    // LiveReadoutConfig::fundamentalHz == 0.0 is a "not yet configured"
    // marker, never a usable reference frequency): silently proceeding would
    // analyze the DC bin as the "fundamental," reporting a meaningless-but-
    // plausible spectrum/THD instead of failing loud (Constitution V;
    // code-review finding D6).
    LiveReadout(CaptureProbeRing<Capacity>& probe, LiveReadoutConfig config)
        : probe_(probe), config_(config), window_(config.windowSize, 0.0f) {
        if (!(config_.fundamentalHz > 0.0)) {
            throw std::invalid_argument(
                "acfx::analysis::LiveReadout: config.fundamentalHz must be > 0; got " +
                std::to_string(config_.fundamentalHz));
        }
    }

    // Drains up to ONE analysis window's worth of samples from the probe and,
    // if a full window was available, recomputes the spectrum + running
    // THD+N via the SAME engine functions offline callers use. Leaves any
    // additional queued samples in the ring for the next call (bounded work
    // per call, matching the adapter's fixed-cadence timer).
    //
    // Underrun (FR-013 edge case): fewer than one window is ready -- holds,
    // performs no drain, and leaves the previous result (if any) untouched.
    // Returns true iff a new result was computed this call.
    bool update() {
        if (probe_.available() < config_.windowSize) {
            return false;
        }

        const std::size_t got =
            probe_.drain(acfx::span<float>(window_.data(), window_.size()));
        if (got < config_.windowSize) {
            // The availability check above guards against this under the
            // single-consumer contract, but never fabricate a result on a
            // short drain (Constitution V: no silent partial-data result).
            return false;
        }

        const acfx::span<const float> in(window_.data(), window_.size());
        spectrum_ = harmonicSpectrum(in, config_.fundamentalHz, config_.sampleRate,
                                      config_.numHarmonics);
        thdn_ = thdPlusN(in, config_.fundamentalHz, config_.sampleRate);
        hasResult_ = true;
        return true;
    }

    // True once at least one full-window analysis has completed.
    [[nodiscard]] bool hasResult() const noexcept { return hasResult_; }

    // Latest broadband harmonic spectrum (valid once hasResult() is true;
    // default-constructed/empty before the first successful update()).
    [[nodiscard]] const HarmonicSpectrum& spectrum() const noexcept { return spectrum_; }

    // Latest running THD+N / noise-floor / SNR figure (all-NaN before the
    // first successful update(), matching thdPlusN()'s own unmeasurable
    // convention, FR-008).
    [[nodiscard]] const ThdnResult& thdn() const noexcept { return thdn_; }

    // The configuration this instance was constructed with.
    [[nodiscard]] const LiveReadoutConfig& config() const noexcept { return config_; }

private:
    CaptureProbeRing<Capacity>& probe_;
    LiveReadoutConfig config_;
    std::vector<float> window_; // scratch drain buffer, sized once at construction

    HarmonicSpectrum spectrum_{};
    ThdnResult thdn_{
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::quiet_NaN(),
    };
    bool hasResult_ = false;
};

} // namespace acfx::analysis
