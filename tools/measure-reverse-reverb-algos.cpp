// Offline level measurement for the reverse-reverb algorithms. Feeds each
// algorithm identical noise (mix fully wet) and reports steady-state output RMS,
// so make-up gains can be set from data rather than guessed.
#include "effects/spike-reverse-reverb/spike-reverse-reverb.h"
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace acfx;

int main() {
    const char* names[] = {"room", "hall", "plate", "spring", "gardner", "schroeder", "velvet"};
    ProcessContext ctx{48000.0, 48, 2};
    double roomRms = 0.0;

    for (int a = 0; a < 7; ++a) {
        SpikeReverseReverb fx;
        fx.prepare(ctx);
        fx.setParameter(ParamId{2}, 1.0f);                 // mix = fully wet
        fx.setParameter(ParamId{21}, static_cast<float>(a) / 6.0f); // algorithm select
        fx.reset();

        std::vector<float> L(48), R(48);
        float* ch[2] = {L.data(), R.data()};
        std::uint32_t seed = 22222u + static_cast<std::uint32_t>(a);
        double sum = 0.0; long n = 0;
        double peak = 0.0;
        for (int blk = 0; blk < 4000; ++blk) {             // ~4 s at 48k/48
            for (int i = 0; i < 48; ++i) {
                seed = seed * 1664525u + 1013904223u;
                const float s = (static_cast<float>((seed >> 9) & 0xFFFFu) / 32768.0f - 1.0f) * 0.3f;
                L[i] = s; R[i] = s;
            }
            AudioBlock b(ch, 2, 48);
            fx.process(b);
            if (blk >= 3000) {                             // measure the last ~1 s
                for (int i = 0; i < 48; ++i) {
                    sum += static_cast<double>(L[i]) * L[i];
                    if (std::fabs(L[i]) > peak) peak = std::fabs(L[i]);
                    ++n;
                }
            }
        }
        const double rms = std::sqrt(sum / static_cast<double>(n));
        if (a == 0) roomRms = rms;
        std::printf("%-10s  rms=%.5f  peak=%.4f  makeup_to_room=%.2fx\n",
                    names[a], rms, peak, roomRms > 0 ? roomRms / (rms > 1e-9 ? rms : 1e-9) : 0.0);
    }
    return 0;
}
