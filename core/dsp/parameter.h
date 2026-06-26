#pragma once

#include "dsp/param-id.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <string_view>

// The single declared definition of one control, consumed by every adapter
// (FR-003), plus the pure, allocation-free normalized<->plain mapping.
// (contracts/parameter-model.md)

namespace acfx {

struct ParameterDescriptor {
    ParamId id;
    std::string_view name; // static storage (string literal)
    ParamUnit unit;
    float min;
    float max;
    float defaultValue; // plain units, within [min, max]
    ParamSkew skew;
    ParamKind kind;
    std::uint8_t discreteCount; // >= 2 when kind == discrete, else 0
};

// Compile-time descriptor validity: max>min; a logarithmic param needs min>0
// (else denormalize yields NaN); a discrete param needs >=2 buckets. Effects
// static_assert this over their constexpr table so a malformed descriptor is a
// BUILD error, not a release-build NaN (the debug asserts below are defense in
// depth for builds that compile them in).
constexpr bool isValidDescriptor(const ParameterDescriptor& d) noexcept {
    if (!(d.max > d.min))
        return false;
    if (d.skew == ParamSkew::logarithmic && !(d.min > 0.0f))
        return false;
    if (d.kind == ParamKind::discrete && d.discreteCount < 2)
        return false;
    return true;
}

namespace detail {
// Clamp to [0,1] AND neutralize non-finite input: NaN must map to 0, not pass
// through. Written so that NaN (for which every comparison is false) takes the
// final else branch: NaN >= 0 is false -> 0. +inf -> 1, -inf -> 0. A plain
// `x < 0 ? 0 : (x > 1 ? 1 : x)` would return NaN unchanged and poison the filter
// state irrecoverably (a guard that fails open).
constexpr float clamp01(float x) noexcept { return x >= 0.0f ? (x <= 1.0f ? x : 1.0f) : 0.0f; }
} // namespace detail

// norm (0..1) -> plain units. Pure, allocation-free, audio-thread safe.
inline float denormalize(const ParameterDescriptor& d, float norm) noexcept {
    norm = detail::clamp01(norm);
    switch (d.kind) {
    case ParamKind::discrete: {
        // Quantize to a bucket index in [0, discreteCount); the index IS the
        // plain value (the effect maps index -> enum).
        const int count = d.discreteCount < 2 ? 2 : static_cast<int>(d.discreteCount);
        int idx = static_cast<int>(norm * static_cast<float>(count));
        if (idx >= count)
            idx = count - 1;
        return static_cast<float>(idx);
    }
    case ParamKind::continuous:
    default:
        break;
    }

    switch (d.skew) {
    case ParamSkew::logarithmic:
        // Requires 0 < min < max (else 0*pow(inf,n) or log(1)=0 yield NaN). The
        // invariant is enforced in debug builds; it compiles out in release, so
        // the audio path stays branch-bounded and allocation-free.
        assert(d.min > 0.0f && d.max > d.min &&
               "logarithmic parameter requires 0 < min < max");
        return d.min * std::pow(d.max / d.min, norm);
    case ParamSkew::linear:
    default:
        return d.min + norm * (d.max - d.min);
    }
}

// plain units -> norm (0..1). Inverse of denormalize for continuous params; for
// discrete params, plain is the bucket index and the result lands mid-bucket so
// it round-trips back to the same index.
inline float normalize(const ParameterDescriptor& d, float plain) noexcept {
    switch (d.kind) {
    case ParamKind::discrete: {
        const int count = d.discreteCount < 2 ? 2 : static_cast<int>(d.discreteCount);
        int idx = static_cast<int>(plain);
        idx = std::clamp(idx, 0, count - 1);
        return (static_cast<float>(idx) + 0.5f) / static_cast<float>(count);
    }
    case ParamKind::continuous:
    default:
        break;
    }

    plain = std::clamp(plain, d.min, d.max);
    switch (d.skew) {
    case ParamSkew::logarithmic:
        assert(d.min > 0.0f && d.max > d.min &&
               "logarithmic parameter requires 0 < min < max");
        return std::log(plain / d.min) / std::log(d.max / d.min);
    case ParamSkew::linear:
    default:
        return (plain - d.min) / (d.max - d.min);
    }
}

} // namespace acfx
