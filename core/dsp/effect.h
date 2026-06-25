#pragma once

#include "dsp/audio-block.h"
#include "dsp/param-id.h"
#include "dsp/parameter.h"
#include "dsp/process-context.h"
#include "dsp/span.h"

// The compile-time contract every effect satisfies — no base class, no vtable in
// the audio path (FR-001, contracts/effect-concept.md). On a C++20 toolchain this
// is a named concept; on C++17 (Teensy) the same member signatures are enforced
// by plain template instantiation (duck typing), with a best-effort static trait
// preserved for diagnostics. The effect code is identical either way.

#if defined(__cpp_concepts) && __cpp_concepts >= 201907L

#include <concepts>

namespace acfx {

template <typename T>
concept Effect =
    requires(T fx, const ProcessContext& ctx, AudioBlock& io, ParamId id, float norm) {
        { fx.prepare(ctx) } -> std::same_as<void>;        // set sr, maxBlock, channels
        { fx.process(io) } -> std::same_as<void>;         // in-place; NO heap alloc
        { fx.reset() } -> std::same_as<void>;             // clear internal state
        { T::parameters() } -> std::convertible_to<span<const ParameterDescriptor>>;
        { fx.setParameter(id, norm) } -> std::same_as<void>; // normalized 0..1 in
    };

} // namespace acfx

// Use as: template <ACFX_EFFECT_CONCEPT T> ... — a constrained template here.
#define ACFX_EFFECT_CONCEPT ::acfx::Effect

#else // C++17 degradation: no concepts available.

#include <type_traits>

namespace acfx {

// Best-effort detection trait (diagnostic only; never gates the build on this
// target). Template instantiation still enforces the real member signatures.
template <typename, typename = void>
struct is_effect : std::false_type {};

template <typename T>
struct is_effect<
    T, std::void_t<decltype(std::declval<T&>().prepare(std::declval<const ProcessContext&>())),
                   decltype(std::declval<T&>().process(std::declval<AudioBlock&>())),
                   decltype(std::declval<T&>().reset()),
                   decltype(T::parameters()),
                   decltype(std::declval<T&>().setParameter(std::declval<ParamId>(),
                                                            std::declval<float>()))>>
    : std::true_type {};

template <typename T>
inline constexpr bool is_effect_v = is_effect<T>::value;

} // namespace acfx

// Unconstrained on C++17 — duck typing enforces the same shape.
#define ACFX_EFFECT_CONCEPT typename

#endif
