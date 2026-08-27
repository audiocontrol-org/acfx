// Offline compile + stability + level check for Breathing Canyon.
#include "effects/spike-breathing-canyon/spike-breathing-canyon.h"
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <vector>
using namespace acfx;

static void run(const char* label, float shimmer, float decay, bool impulseThenSilence) {
    SpikeBreathingCanyon fx;
    ProcessContext ctx{48000.0, 48, 2};
    fx.prepare(ctx);
    fx.setParameter(ParamId{9}, 1.0f);                 // mix fully wet
    fx.setParameter(ParamId{2}, decay);               // Canyon/Decay
    fx.setParameter(ParamId{7}, shimmer);             // Shimmer/Amount
    fx.reset();
    std::vector<float> L(48), R(48); float* ch[2] = {L.data(), R.data()};
    std::uint32_t seed = 12345;
    double sum = 0, n = 0, peak = 0; bool bad = false;
    for (int blk = 0; blk < 6000; ++blk) {             // 6 s
        for (int i = 0; i < 48; ++i) {
            float s;
            if (impulseThenSilence) s = (blk < 40) ? ((seed=seed*1664525u+1013904223u,(int)((seed>>9)&0xFFFF)/32768.0f-1.0f)*0.4f) : 0.0f;
            else { seed=seed*1664525u+1013904223u; s = ((int)((seed>>9)&0xFFFF)/32768.0f-1.0f)*0.3f; }
            L[i]=s; R[i]=s;
        }
        AudioBlock b(ch,2,48); fx.process(b);
        for (int i=0;i<48;++i){ float y=L[i]; if(!std::isfinite(y)) bad=true; if(std::fabs(y)>peak)peak=std::fabs(y);
                                if(blk>=5000){sum+=y*y;++n;} }
    }
    double rms = n>0? std::sqrt(sum/n):0;
    printf("%-28s rms=%.5f peak=%.4f %s\n", label, rms, peak, bad?"*** NONFINITE ***":(peak>2.0?"*** LOUD ***":"ok"));
}

int main() {
    run("noise, shimmer0 decay0.8", 0.0f, 0.8f, false);
    run("noise, shimmer0.5 decay0.8", 0.5f, 0.8f, false);
    run("noise, shimmer1.0 decay1.0 (worst)", 1.0f, 1.0f, false);
    run("impulse->silence decay1.0 shim1", 1.0f, 1.0f, true);  // does the tail settle or run away?
    return 0;
}
