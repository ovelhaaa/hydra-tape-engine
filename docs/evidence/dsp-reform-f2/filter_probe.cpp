// Test-only access to ACTUAL configured core filters, no copied mappings.
#define private public
#include "../../../dsp/core/tape_core.hpp"
#undef private
#include "../../../dsp/core/tape_core.cpp"
#include <fstream>
#include <string>
using namespace hydra::dsp;
int main(int argc,char**argv){
 if(argc!=2)return 2;
 {
  TapeCore core(48000,20);auto& c=*core.impl_;
  c.framesSinceDelayEnable=100000;
  for(int i=0;i<c.bufferSize;++i)c.delayLine[i]=float((i*17)%127-63)*.0078125f;
  c.mirrorDelayGuard(c.delayLine);
  for(int w=0;w<c.bufferSize;++w)for(float d:{2.f,2.25f,3.5f,955.75f}){
   c.writeHead=w;
   // Independent modular addressing, using the existing Hermite polynomial.
   float rp=float(w)-d;if(rp<0)rp+=c.bufferSizeF;
   int r=int(rp);float f=rp-r;
   auto at=[&](int k){return c.delayLine[(k+c.bufferSize)%c.bufferSize];};
   float expected=c.hermite4(at(r-1),at(r),at(r+1),at(r+2),f);
   if(c.readHistory(d,c.delayLine)!=expected)return 5;
  }
 }
 for(int speed:{0,50,100})for(int age:{0,50,100})
 for(int amount:{0,50,100})for(int tone:{0,50,100}){
  // Full speed/age matrix; bump/tone endpoints at speed50 or age50.
  if((amount!=50||tone!=50)&&speed!=50&&age!=50)continue;
  for(int stage=0;stage<9;++stage){
   TapeCore core(44100); TapeParams p;
   p.tapeSpeed=speed;p.tapeAge=age;p.headBumpAmount=amount;p.tone=tone;
   core.updateParams(p);auto& c=*core.impl_;
   c.feedbackHPF.reset();c.feedbackHeadBump.reset();c.feedbackGapLoss.reset();c.feedbackPhase.reset();
   c.classicReproBumpL.reset();c.classicGapLossL.reset();c.classicRolloffL.reset();
   std::string name=std::string(argv[1])+"/filter_"+std::to_string(speed)+"_"+std::to_string(age)+"_"+std::to_string(amount)+"_"+std::to_string(tone)+"_"+std::to_string(stage)+".f32";
   std::ofstream out(name,std::ios::binary);if(!out)return 3;
   for(int i=0;i<44100;++i){float x=i==0?1.f:0.f,y=x;
    auto playback=[&](float v){return c.classicGapLossL.process(c.classicReproBumpL.process(c.classicRolloffL.process(v)));};
    auto electronics=[&](float v){return c.feedbackPhase.process(c.feedbackGapLoss.process(c.feedbackHeadBump.process(c.feedbackHPF.process(v))));};
    if(stage==0)y=c.feedbackHPF.process(x);
    if(stage==1)y=c.feedbackHeadBump.process(x);
    if(stage==2)y=c.feedbackGapLoss.process(x);
    if(stage==3)y=electronics(x);
    if(stage==4)y=playback(x);
    if(stage==5)y=electronics(playback(x));
    if(stage==6)y=c.classicReproBumpL.process(x);
    if(stage==7)y=c.classicGapLossL.process(x);
    if(stage==8)y=c.classicRolloffL.process(x);
    if(!std::isfinite(y))return 4;
    out.write(reinterpret_cast<char*>(&y),sizeof y);
   }
   if(!out)return 3;
  }
 }
}
