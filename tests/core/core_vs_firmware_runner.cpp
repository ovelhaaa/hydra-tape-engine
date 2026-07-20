#include "tape_core.hpp"
#include "TapeDelay.h"

#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>

namespace {
struct Options { std::string engine, signal, scenario; float sampleRate = 48000; int frames = 48000; };
Options parse(int argc, char** argv) {
  Options o;
  for (int i=1;i+1<argc;i+=2) { std::string k=argv[i], v=argv[i+1];
    if(k=="--engine") o.engine=v; else if(k=="--signal") o.signal=v; else if(k=="--scenario") o.scenario=v;
    else if(k=="--sample-rate") o.sampleRate=std::stof(v); else if(k=="--frames") o.frames=std::stoi(v);
  }
  if(o.engine.empty()||o.signal.empty()||o.scenario.empty()) { std::cerr << "required: --engine --signal --scenario\n"; std::exit(2); }
  return o;
}
float input(const Options& o, int n) {
  const float t=n/o.sampleRate, duration=o.frames/o.sampleRate;
  if(o.signal=="impulse") return n==0 ? 0.8f : 0.f;
  if(o.signal=="silence") return 0.f;
  if(o.signal=="linear_sweep" || o.signal=="log_sweep") { float f=o.signal=="linear_sweep" ? 20.f+(20000.f-20.f)*t/duration : 20.f*std::pow(1000.f,t/duration); return .35f*std::sin(6.2831853f*f*t); }
  if(o.signal=="white_noise") { uint32_t x=0x13579BDFu+uint32_t(n); x^=x<<13; x^=x>>17; x^=x<<5; return (float(int(x&0xffff)-32768)/32768.f)*.2f; }
  // Deterministic synthetic guitar DI surrogate: pluck envelope, harmonics and pick transient.
  float local=std::fmod(t,.73f), env=std::exp(-local*4.5f); return env*(.38f*std::sin(6.2831853f*110.f*t)+.16f*std::sin(6.2831853f*220.f*t)+.08f*std::sin(6.2831853f*330.f*t))+(local<.002f?.18f:0.f);
}
hydra::dsp::TapeParams coreParams(const std::string& s) { hydra::dsp::TapeParams p; p.noise=0; p.dropoutSeverity=0; p.dryWet=100; p.delayWet=100; p.delayActive=s!="saturator"; p.delayTimeMs=180; p.activeHeads=4; p.headsMusical=false; p.drive=40; if(s=="feedback_20")p.feedback=20; else if(s=="feedback_50")p.feedback=50; else p.feedback=85; if(s=="mod_max"){p.wowDepth=100;p.flutterDepth=100;} else {p.wowDepth=p.flutterDepth=0;} if(s=="reverse"||s=="reverse_smear")p.reverse=true; if(s=="reverse_smear")p.reverseSmear=true; if(s=="spring")p.spring=true; return p; }
TapeParams firmwareParams(const hydra::dsp::TapeParams& p) { TapeParams q{}; q.flutterDepth=p.flutterDepth;q.wowDepth=p.wowDepth;q.dropoutSeverity=p.dropoutSeverity;q.drive=p.drive;q.noise=p.noise;q.tapeSpeed=p.tapeSpeed;q.tapeAge=p.tapeAge;q.headBumpAmount=p.headBumpAmount;q.azimuthError=p.azimuthError;q.flutterRate=p.flutterRate;q.wowRate=p.wowRate;q.delayActive=p.delayActive;q.delayTimeMs=p.delayTimeMs;q.feedback=p.feedback;q.dryWet=p.dryWet;q.activeHeads=p.activeHeads;q.bpm=p.bpm;q.headsMusical=p.headsMusical;q.guitarFocus=p.guitarFocus;q.tone=p.tone;q.pingPong=p.pingPong;q.freeze=p.freeze;q.reverse=p.reverse;q.reverseSmear=p.reverseSmear;q.spring=p.spring;q.springDecay=p.springDecay;q.springDamping=p.springDamping;q.springMix=p.springMix; return q; }
}
int main(int argc,char** argv) { auto o=parse(argc,argv); auto p=coreParams(o.scenario); std::cout<<std::setprecision(9); if(o.engine=="core") { hydra::dsp::TapeCore e(o.sampleRate); e.updateParams(p); for(int n=0;n<o.frames;n++){float l,r; float x=input(o,n);e.processStereo(x,x,&l,&r);std::cout<<l<<','<<r<<'\n';} } else if(o.engine=="firmware") { TapeModel e(o.sampleRate); e.updateParams(firmwareParams(p)); for(int n=0;n<o.frames;n++){float l,r;float x=input(o,n);e.processStereo(x,x,&l,&r);std::cout<<l<<','<<r<<'\n';} } else return 2; }
