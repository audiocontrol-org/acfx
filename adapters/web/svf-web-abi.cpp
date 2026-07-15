#include "svf-web-abi.h"

#include "svf-handle.h"

#include "dsp/audio-block.h"
#include "dsp/param-id.h"
#include "dsp/process-context.h"
#include "effects/svf/svf-effect.h"

// Opaque handle wraps one SvfEffect (layout in svf-handle.h). Allocation happens
// ONLY in svf_create; svf_process is allocation-free (Principle VIII). The handle
// also records the prepared sample rate + published normalized params so the
// analysis ABI (svf-web-analysis.cpp) can reconstruct coefficients / an at-rest
// impulse without touching the effect's private state.

extern "C" {

SvfHandle* svf_create(void) { return new SvfHandle(); }
void svf_destroy(SvfHandle* h) { delete h; }

void svf_prepare(SvfHandle* h, double sampleRate, int maxBlockSize, int numChannels) {
    h->sampleRate = sampleRate;
    h->effect.prepare(acfx::ProcessContext{sampleRate, maxBlockSize, numChannels});
}

void svf_set_param(SvfHandle* h, unsigned char paramId, float normalized) {
    if (paramId < 3) {
        h->paramNorm[paramId] = normalized;
        h->paramSet[paramId] = true;
    }
    h->effect.setParameter(acfx::ParamId{paramId}, normalized);
}

void svf_process(SvfHandle* h, float* samples, int numSamples) {
    float* chans[1] = {samples};
    acfx::AudioBlock io(chans, 1, numSamples);
    h->effect.process(io);
}

} // extern "C"
