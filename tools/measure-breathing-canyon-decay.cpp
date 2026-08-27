#include "effects/spike-breathing-canyon/spike-breathing-canyon.h"
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <vector>
using namespace acfx;
// Feed 0.3 s noise burst then silence; report RT60 (time to drop 60 dB from peak tail)
// and a graininess proxy: mean |Δenvelope| across 20 ms windows in the tail.
static void run(const char* lbl,float damp,float fb,float size){
  SpikeBreathingCanyon fx; ProcessContext ctx{48000.0,64,2}; fx.prepare(ctx);
  fx.setParameter(ParamId{4},1.0f);            // wet
  fx.setParameter(ParamId{3},fb);              // feedback
  fx.setParameter(ParamId{6},damp);            // damping
  fx.setParameter(ParamId{0},size);            // size
  fx.setParameter(ParamId{10},0.0f);           // shimmer OFF (isolate the tank)
  fx.reset();
  std::vector<float> L(64),R(64); float* ch[2]={L.data(),R.data()};
  std::uint32_t s=7; std::vector<double> env; double win=0; int wc=0;
  const int burst=(int)(0.3*48000);
  for(int i=0;i<(int)(8.0*48000);++i){
    if(i%64==0){ // fill block
      for(int k=0;k<64;++k){ float x=0; if(i+k<burst){s=s*1664525u+1013904223u; x=((int)((s>>9)&0xFFFF)/32768.0f-1.0f)*0.4f;} L[k]=x;R[k]=x; }
      AudioBlock b(ch,2,64); fx.process(b);
      for(int k=0;k<64;++k){ win+=L[k]*L[k]; if(++wc>=960){env.push_back(std::sqrt(win/960));win=0;wc=0;} }
    }
  }
  // peak tail after burst (skip first 0.35s)
  int startIdx=(int)(0.35*50); double pk=0; for(size_t j=startIdx;j<env.size();++j) if(env[j]>pk)pk=env[j];
  double thr=pk*0.001; // -60 dB
  double rt60=-1; for(size_t j=startIdx;j<env.size();++j){ if(env[j]<thr){rt60=j/50.0-0.35;break;} }
  // graininess: mean abs frac change of envelope in tail region [0.4s..2s]
  double rough=0;int rn=0; for(size_t j=startIdx+3;j<env.size()&&j<startIdx+80;++j){ if(env[j-1]>1e-6){rough+=std::fabs(env[j]-env[j-1])/env[j-1];++rn;} }
  printf("%-26s RT60=%5.2fs peakTail=%.4f roughness=%.3f\n",lbl,rt60,pk,rn?rough/rn:0);
}
int main(){
  run("default(damp.5 fb.75 sz.7)",0.5f,0.75f,0.7f);
  run("no-damp(damp0 fb.75)",0.0f,0.75f,0.7f);
  run("no-damp hi-fb(damp0 fb.95)",0.0f,0.95f,0.7f);
  run("big(damp.2 fb.9 sz1)",0.2f,0.9f,1.0f);
  return 0;
}
