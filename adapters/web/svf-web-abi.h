#pragma once
#ifdef __cplusplus
extern "C" {
#endif

typedef struct SvfHandle SvfHandle;

SvfHandle* svf_create(void);
void       svf_destroy(SvfHandle* h);
void       svf_prepare(SvfHandle* h, double sampleRate, int maxBlockSize, int numChannels);
void       svf_set_param(SvfHandle* h, unsigned char paramId, float normalized);
void       svf_process(SvfHandle* h, float* samples, int numSamples);

#ifdef __cplusplus
}
#endif
