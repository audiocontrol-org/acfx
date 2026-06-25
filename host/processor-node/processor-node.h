#pragma once

#include "dsp/audio-block.h"
#include "dsp/effect.h"
#include "dsp/param-id.h"
#include "dsp/parameter.h"
#include "dsp/process-context.h"
#include "dsp/span.h"

// The desktop-only host boundary: the single place a virtual call touches the
// audio path — at most once per block (FR-004 / Constitution VI). NEVER compiled
// into the Daisy/Teensy builds. (contracts/processor-node.md)

namespace acfx {

struct ProcessorNode {
    virtual ~ProcessorNode() = default;
    virtual void prepare(const ProcessContext& ctx) = 0;
    virtual void processBlock(AudioBlock& io) = 0; // the one virtual call / block
    virtual void reset() = 0;
    virtual span<const ParameterDescriptor> parameters() const = 0;
    virtual void setParameter(ParamId id, float normalized) = 0;
};

// Stores a concrete Effect by value and forwards each virtual call to the inlined
// concrete method — zero hot-path overhead beyond the single block dispatch.
template <ACFX_EFFECT_CONCEPT T>
class EffectNode final : public ProcessorNode {
public:
    void prepare(const ProcessContext& ctx) override { fx_.prepare(ctx); }
    void processBlock(AudioBlock& io) override { fx_.process(io); } // inlined concrete call
    void reset() override { fx_.reset(); }
    span<const ParameterDescriptor> parameters() const override { return T::parameters(); }
    void setParameter(ParamId id, float n) override { fx_.setParameter(id, n); }

    T& effect() noexcept { return fx_; }
    const T& effect() const noexcept { return fx_; }

private:
    T fx_; // stored by value — no indirection beyond the single virtual dispatch
};

} // namespace acfx
