#pragma once

// waveshaper-lut.h
// Fixed-size lookup-table waveshaper with linear interpolation and edge-clamp.
//
// Design summary (research.md Decision 4):
//   The LUT covers the input domain u in [-kDomain, +kDomain] with kTableSize
//   uniformly-spaced sample points.  The step size is:
//
//     h = 2*kDomain / (kTableSize - 1)  [inclusive-endpoint convention]
//
//   so the i-th sample corresponds to u_i = -kDomain + i*h,  i in [0, kTableSize-1].
//
// Build (init-time, NOT RT-safe in spirit):
//   build(fn) fills table_[i] = fn(u_i) for each i.  The storage is a
//   std::array member, so no heap allocation occurs.
//
// Evaluate (RT-safe path):
//   1. Map u to a fractional table index:
//        frac_idx = (u - (-kDomain)) / h  =  (u + kDomain) * (kTableSize-1) / (2*kDomain)
//   2. Floor to integer index i = floor(frac_idx);  t = frac_idx - i  (0 <= t < 1).
//   3. Linear interpolation:  result = table_[i] + t * (table_[i+1] - table_[i]).
//   4. Edge-clamp (defined, bounded policy — NOT a silent fallback):
//        u <= -kDomain → return table_[0]
//        u >=  kDomain → return table_[kTableSize-1]
//      This is explicit in the code and documented here per research.md Decision 4:
//      "Out-of-domain inputs clamp to the table edge (matching closed-form edge
//      behaviour within tolerance) — a defined bounded policy, not a silent fallback."
//
// Interpolation error bound (C2 functions on the domain):
//   |error| <= (h^2 / 8) * max|f''(u)|
//   With h = 8/511 ~ 0.01565 and the worst-case shape (algebraic, max|f''| ~ 0.859):
//   worst-case error ~ 2.63e-5 << kMaxDeviation = 1e-3  (38x safety margin).

#include <array>
#include <cstddef>

namespace acfx {

class WaveshaperLut {
public:
    // Table resolution: kTableSize uniformly-spaced points over the domain.
    static constexpr int   kTableSize    = 512;

    // Input domain: the LUT covers u in [-kDomain, +kDomain].
    static constexpr float kDomain       = 4.0f;

    // Named interpolation-error bound (SC-004, FR-011/012).
    // See derivation in tests/core/waveshaper-lut-test.cpp file header.
    // kMaxDeviation = 1e-3 gives a 38x safety margin over the analytic worst case.
    static constexpr float kMaxDeviation = 1.0e-3f;

    // build(): fill the LUT from a float(float) callable.
    //
    // Samples fn at kTableSize points inclusive of both endpoints:
    //   table_[i] = fn(-kDomain + i * h),   h = 2*kDomain / (kTableSize - 1)
    //
    // NOT RT-safe (called once from init/constructor, never on the audio path).
    // Allocation-free: storage is the std::array member, no heap allocation.
    template <typename Fn>
    void build(Fn fn) noexcept {
        constexpr float h = (2.0f * kDomain) / static_cast<float>(kTableSize - 1);
        for (int i = 0; i < kTableSize; ++i) {
            const float u  = -kDomain + static_cast<float>(i) * h;
            table_[static_cast<std::size_t>(i)] = fn(u);
        }
    }

    // evaluate(): linear interpolation between table points. RT-safe:
    //   noexcept, allocation-free, O(1) bounded work.
    //
    // Out-of-domain policy (research.md Decision 4 — defined bounded policy):
    //   u <= -kDomain → return table_[0]           (left edge clamp)
    //   u >=  kDomain → return table_[kTableSize-1] (right edge clamp)
    //
    // This is an explicit, documented, bounded policy — NOT a silent fallback.
    // The clamped return value equals the shape function evaluated at the domain
    // boundary, sampled directly during build(), so it carries only floating-point
    // rounding error (< 1 ULP), well within kMaxDeviation.
    float evaluate(float u) const noexcept {
        // Edge-clamp: out-of-domain inputs return the boundary table value.
        if (u <= -kDomain) { return table_[0]; }
        if (u >=  kDomain) { return table_[static_cast<std::size_t>(kTableSize - 1)]; }

        // Compute fractional table index.
        // h = 2*kDomain / (kTableSize - 1);  inv_h = 1/h = (kTableSize-1) / (2*kDomain)
        constexpr float inv_h = static_cast<float>(kTableSize - 1) / (2.0f * kDomain);
        const float frac_idx  = (u + kDomain) * inv_h;

        // Split into integer part and fractional part.
        // frac_idx is in [0, kTableSize-1].  The i+1 access is safe because we
        // clamped u <  kDomain above, so frac_idx < kTableSize-1, so i <= kTableSize-2.
        const int   i = static_cast<int>(frac_idx);
        const float t = frac_idx - static_cast<float>(i);

        const auto  si  = static_cast<std::size_t>(i);
        return table_[si] + t * (table_[si + 1] - table_[si]);
    }

private:
    std::array<float, kTableSize> table_{};
};

} // namespace acfx
