#include "synthetic-source.h"

#include <random>

namespace lessonassets {

std::vector<float> generateSyntheticSource(int sampleRate, int numSamples) {
    std::vector<float> out(static_cast<std::size_t>(numSamples));

    std::mt19937 rng(kSyntheticSourceNoiseSeed);
    std::uniform_real_distribution<float> noiseDist(-1.0f, 1.0f);

    const float phaseInc = kSyntheticSourceSawHz / static_cast<float>(sampleRate);
    float phase = 0.0f;
    for (int n = 0; n < numSamples; ++n) {
        // Naive sawtooth in [-1, 1): 2*phase - 1, phase in [0,1).
        const float saw = 2.0f * phase - 1.0f;
        const float noise = noiseDist(rng);
        // 0.5/0.5 mix, then 0.5 headroom scale so the [-1,1]+[-1,1] sum never
        // clips (see synthetic-source.h for the source-signal rationale).
        out[static_cast<std::size_t>(n)] = 0.5f * (0.5f * saw + 0.5f * noise);

        phase += phaseInc;
        if (phase >= 1.0f)
            phase -= 1.0f;
    }
    return out;
}

} // namespace lessonassets
