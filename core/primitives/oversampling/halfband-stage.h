#pragma once

#include <array>
#include <cstddef>

#include "primitives/oversampling/halfband-coefficients.h"

// HalfbandUpsampler (1 -> 2) and HalfbandDownsampler (2 -> 1): the two 2x
// polyphase halfband FIR stages the Oversampler cascades (contract
// "HalfbandUpsampler / HalfbandDownsampler"; data-model "Halfband*").
//
// Polyphase structure (why this is cheap and exact)
// -------------------------------------------------
// The shared table `acfx::kHalfbandCoeffs` is a Type-I linear-phase halfband
// FIR of length N = kHalfbandTaps (91): symmetric, center tap h[c] = 0.5 at
// c = (N-1)/2 = 45, and every OTHER even-offset tap from the center is exactly
// zero. Splitting h[n] by the parity of n yields two polyphase sub-filters:
//
//   E0 (even n): h[0], h[2], ... , h[N-1]   -- the dense branch (46 taps here)
//   E1 (odd  n): h[1], h[3], ... , h[N-2]   -- all zero EXCEPT the center tap
//                                              h[c] = 0.5 (c is odd)
//
// So the E1 branch collapses to a single scaled delay tap: one branch is a pure
// (delayed) pass-through, the other is the FIR sum over the non-zero taps
// (data-model). Each sub-filter runs at the LOWER (base) rate, so a 2x stage
// costs ~N/2 mults per base sample, never a length-N convolution at the high
// rate.
//
// Interpolation gain (normalization)
// ----------------------------------
// The full table has unity DC gain (sum(h) = 1: center 0.5 + the even taps sum
// to 0.5). A zero-stuffing 2x upsampler therefore only reaches DC gain 0.5 in
// each output phase, so the upsampler multiplies its outputs by 2 to preserve
// passband level (unity DC through up -> down). The downsampler needs no such
// scale (a plain FIR at unity DC gain).
//
// Latency
// -------
// A symmetric FIR of length N has analytic group delay (N-1)/2 = c samples at
// the rate it operates. Both stages express latency() at the HIGH (oversampled)
// rate, per the contract; the parent Oversampler sums the cascade's per-stage
// latencies and converts to base-rate samples.
//
// RT-safety (Constitution VI): value-typed, fixed-size std::array delay lines
// sized at compile time from the tap count; no heap, no locks, bounded work;
// every process()/init()/reset() is noexcept. Platform-independent: standard
// library only (Constitution IV).

namespace acfx {

namespace halfband_detail {

// N and the center-tap index of the shared linear-phase halfband table.
inline constexpr int kTaps = kHalfbandTaps;                 // 91
inline constexpr int kCenter = (kHalfbandTaps - 1) / 2;     // 45 (odd => in E1)

// Number of even-offset (dense, E0) taps: indices 0, 2, ..., N-1.
inline constexpr int kEvenCount = (kHalfbandTaps + 1) / 2;  // 46

// The single non-zero E1 tap is the center; its low-rate position within the
// odd polyphase branch. Upsampler reads its input history at depth kOddDepthUp;
// the decimator's commutator offset puts it one low-rate step deeper.
inline constexpr int kOddDepthUp = (kCenter - 1) / 2;       // 22
inline constexpr int kOddDepthDown = kOddDepthUp + 1;       // 23
inline constexpr int kOddLineSize = kOddDepthDown + 1;      // 24

// The center tap must be exactly 0.5 for the halfband pass-through branch, and
// the center index must be odd (so it lands in the E1 polyphase branch).
static_assert(kCenter % 2 == 1, "halfband center tap must be at an odd index");
static_assert(kHalfbandCoeffs[kCenter] == 0.5f,
              "halfband center tap must be exactly 0.5");
static_assert(2 * (kEvenCount - 1) == kHalfbandTaps - 1,
              "even polyphase branch must span the full table");

}  // namespace halfband_detail

// One 2x interpolation stage (1 -> 2 samples), linear-phase polyphase halfband
// FIR. See file header for the polyphase decomposition and gain rationale.
class HalfbandUpsampler {
public:
    void init() noexcept { reset(); }

    void reset() noexcept {
        line_.fill(0.0f);
        pos_ = 0;
    }

    // Produce the two interpolated (high-rate) output samples for one base-rate
    // input. out0 is the even output phase (dense E0 branch), out1 the odd phase
    // (the 0.5 center-tap pass-through, scaled by the interpolation gain 2 ->
    // unity). The two outputs are in oversampled-time order.
    void process(float in, float& out0, float& out1) noexcept {
        namespace hb = halfband_detail;
        line_[static_cast<std::size_t>(pos_)] = in;

        // Even phase: y[2p] = 2 * sum_j h[2j] * x[p-j]  (dense branch).
        float even = 0.0f;
        for (int j = 0; j < hb::kEvenCount; ++j) {
            const int idx = wrap(pos_ - j);
            even += kHalfbandCoeffs[2 * j] * line_[static_cast<std::size_t>(idx)];
        }
        out0 = 2.0f * even;

        // Odd phase: y[2p+1] = 2 * h[c] * x[p - kOddDepthUp] = x[p - kOddDepthUp]
        // (2 * 0.5 == 1); a pure delayed pass-through of the halfband.
        const int oddIdx = wrap(pos_ - hb::kOddDepthUp);
        out1 = 2.0f * kHalfbandCoeffs[hb::kCenter] *
               line_[static_cast<std::size_t>(oddIdx)];

        pos_ = wrap(pos_ + 1);
    }

    // Analytic group delay of the halfband, expressed at THIS stage's OUTPUT
    // (high / oversampled) rate.
    static constexpr int latency() noexcept { return halfband_detail::kCenter; }

private:
    static constexpr int kN = halfband_detail::kEvenCount;  // low-rate history

    static int wrap(int i) noexcept {
        // i is within (-kN, 2*kN); one add/sub restores [0, kN).
        if (i < 0) i += kN;
        if (i >= kN) i -= kN;
        return i;
    }

    std::array<float, static_cast<std::size_t>(kN)> line_{};
    int pos_ = 0;
};

// One 2x decimation stage (2 -> 1 samples), linear-phase polyphase halfband FIR
// (the dual of the upsampler). Consumes the two high-rate phases produced by an
// upstream upsampler: in0 the even phase, in1 the odd phase.
class HalfbandDownsampler {
public:
    void init() noexcept { reset(); }

    void reset() noexcept {
        even_.fill(0.0f);
        odd_.fill(0.0f);
        posE_ = 0;
        posO_ = 0;
    }

    // Band-limit and keep one base-rate sample:
    //   y[p] = sum_j h[2j] * a[p-j]  +  h[c] * b[p - kOddDepthDown]
    // where a is the even-phase stream (in0) and b the odd-phase stream (in1).
    // Unity DC gain -> no interpolation scale here.
    float process(float in0, float in1) noexcept {
        namespace hb = halfband_detail;
        even_[static_cast<std::size_t>(posE_)] = in0;
        odd_[static_cast<std::size_t>(posO_)] = in1;

        float acc = 0.0f;
        for (int j = 0; j < hb::kEvenCount; ++j) {
            const int idx = wrapE(posE_ - j);
            acc += kHalfbandCoeffs[2 * j] * even_[static_cast<std::size_t>(idx)];
        }
        const int oddIdx = wrapO(posO_ - hb::kOddDepthDown);
        acc += kHalfbandCoeffs[hb::kCenter] * odd_[static_cast<std::size_t>(oddIdx)];

        posE_ = wrapE(posE_ + 1);
        posO_ = wrapO(posO_ + 1);
        return acc;
    }

    // Analytic group delay of the halfband, expressed at this stage's INPUT
    // (high / oversampled) rate.
    static constexpr int latency() noexcept { return halfband_detail::kCenter; }

private:
    static constexpr int kNe = halfband_detail::kEvenCount;    // even history
    static constexpr int kNo = halfband_detail::kOddLineSize;  // odd history

    static int wrapE(int i) noexcept {
        if (i < 0) i += kNe;
        if (i >= kNe) i -= kNe;
        return i;
    }
    static int wrapO(int i) noexcept {
        if (i < 0) i += kNo;
        if (i >= kNo) i -= kNo;
        return i;
    }

    std::array<float, static_cast<std::size_t>(kNe)> even_{};
    std::array<float, static_cast<std::size_t>(kNo)> odd_{};
    int posE_ = 0;
    int posO_ = 0;
};

}  // namespace acfx
