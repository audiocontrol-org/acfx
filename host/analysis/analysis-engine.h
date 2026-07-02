#pragma once

// host/analysis/analysis-engine.h
//
// The single entry surface tests + adapters share (contracts/analysis-engine-
// api.md; FR-006/014/015; harmonic-analysis T007). Composes what EXISTS NOW:
//   - window.h    : selectable analysis window (default 4-term Blackman-Harris)
//   - fft.h       : windowed radix-2 FFT, power-of-two only
//   - stimulus.h  : deterministic stimulus generators (Sine/Sweep/Impulse/
//                   Step/WhiteNoise)
//   - analyzers.h : the effect-agnostic capture seam (capture/captureCallable)
//                   plus the retained exact integer-cycle GoertzelAnalyzer,
//                   ImpulseAnalyzer, and CorrelationAnalyzer
//   - aliasing.h  : the inharmonic/aliased-energy measure (AliasingMeasure /
//                   aliasingMeasure)
//
// stimulus.h/analyzers.h/aliasing.h live in namespace acfx::measure (their
// original namespace, preserved across the T007 relocation so every existing
// call site keeps working); this header re-exposes them under acfx::analysis
// via using-declarations so callers of the engine surface do not also need to
// reach into acfx::measure directly.
//
// MINIMAL ON PURPOSE: this establishes the shared seam tests and adapters
// will build on. Later tasks add spectrum.h/thdn.h/imd.h/alias-sweep.h/
// drive-series.h and extend this file to compose them -- this header MUST
// NOT reference those not-yet-authored headers.
//
// Host-only; MUST NOT be #include'd from core/ (Constitution IV;
// scripts/check-portability.sh gate C-AN-DIR).

#include "analysis/aliasing.h"
#include "analysis/analyzers.h"
#include "analysis/fft.h"
#include "analysis/stimulus.h"
#include "analysis/window.h"

namespace acfx::analysis {

// ---------------------------------------------------------------------------
// Capture seam (FR-004/FR-006) -- run an Effect (or a per-sample callable)
// over a stimulus buffer into an output buffer.
// ---------------------------------------------------------------------------
using acfx::measure::capture;
using acfx::measure::captureCallable;

// ---------------------------------------------------------------------------
// Stimulus generators (T002) -- deterministic signal generators used to
// drive the effect/callable under measurement.
// ---------------------------------------------------------------------------
using acfx::measure::ImpulseGenerator;
using acfx::measure::NoiseGenerator;
using acfx::measure::SineGenerator;
using acfx::measure::StepGenerator;
using acfx::measure::SweepGenerator;

// ---------------------------------------------------------------------------
// Analyzers -- the retained exact single-bin Goertzel path (FR-007/010),
// plus the general-purpose impulse/correlation analyzers used for latency
// work.
// ---------------------------------------------------------------------------
using acfx::measure::CorrelationAnalyzer;
using acfx::measure::GoertzelAnalyzer;
using acfx::measure::ImpulseAnalyzer;

// ---------------------------------------------------------------------------
// Inharmonic / aliased-energy measure (research.md Decision 6/8) -- the
// alias-vs-frequency sweep metric (alias-sweep.h, a later task) reuses this
// exact integer-cycle method rather than adding new spectral machinery.
// ---------------------------------------------------------------------------
using acfx::measure::AliasingMeasure;
using acfx::measure::aliasingMeasure;

} // namespace acfx::analysis
