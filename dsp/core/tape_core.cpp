#include "tape_core.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <new>

namespace hydra::dsp {
namespace {
constexpr float PI = 3.14159265359f;
constexpr float TWO_PI = 6.28318530718f;
inline float clampf(float v, float lo, float hi) { return std::max(lo, std::min(v, hi)); }
inline float fast_sin(float x) {
  while (x > PI) x -= TWO_PI;
  while (x < -PI) x += TWO_PI;
  const float B = 4.0f / PI;
  const float C = -4.0f / (PI * PI);
  float y = B * x + C * x * std::fabs(x);
  const float P = 0.225f;
  return P * (y * std::fabs(y) - y) + y;
}

class DCBlocker { public: float process(float input){ float out=input-x1+R*y1; x1=input; y1=out; if (std::fabs(y1)<1e-20f) y1=0; return out;} void clear(){x1=y1=0;} private: float x1=0,y1=0,R=0.995f; };
class BiquadFilter {
public:
  void reset(){z1=z2=0;} 
  void setLowShelf(float fs,float freq,float Q,float gainDB){ float A=std::pow(10.f,gainDB/40.f),w0=2*PI*freq/fs,cosw0=std::cos(w0),sinw0=std::sin(w0),alpha=sinw0/(2*Q),sqA=std::sqrt(A),ap1=A+1,am1=A-1,tw=2*sqA*alpha,a0=ap1+am1*cosw0+tw; b0=(A*(ap1-am1*cosw0+tw))/a0; b1=(2*A*(am1-ap1*cosw0))/a0; b2=(A*(ap1-am1*cosw0-tw))/a0; a1=(-2*(am1+ap1*cosw0))/a0; a2=(ap1+am1*cosw0-tw)/a0; }
  void setHighShelf(float fs,float freq,float Q,float gainDB){ float A=std::pow(10.f,gainDB/40.f),w0=2*PI*freq/fs,cosw0=std::cos(w0),sinw0=std::sin(w0),alpha=sinw0/(2*Q),sqA=std::sqrt(A),ap1=A+1,am1=A-1,tw=2*sqA*alpha,a0=ap1-am1*cosw0+tw; b0=(A*(ap1+am1*cosw0+tw))/a0; b1=(-2*A*(am1+ap1*cosw0))/a0; b2=(A*(ap1+am1*cosw0-tw))/a0; a1=(2*(am1-ap1*cosw0))/a0; a2=(ap1-am1*cosw0-tw)/a0; }
  void setLowpass(float fs,float freq,float Q){ float w0=2*PI*freq/fs,cosw0=std::cos(w0),sinw0=std::sin(w0),alpha=sinw0/(2*Q),a0=1+alpha; b0=((1-cosw0)/2)/a0; b1=(1-cosw0)/a0; b2=b0; a1=(-2*cosw0)/a0; a2=(1-alpha)/a0; }
  void setHighpass(float fs,float freq,float Q){ float w0=2*PI*freq/fs,cosw0=std::cos(w0),sinw0=std::sin(w0),alpha=sinw0/(2*Q),a0=1+alpha; b0=((1+cosw0)/2)/a0; b1=-(1+cosw0)/a0; b2=b0; a1=(-2*cosw0)/a0; a2=(1-alpha)/a0; }
  float process(float in){ float out=b0*in+z1; z1=b1*in-a1*out+z2; z2=b2*in-a2*out; if (std::fabs(z1)<1e-20f) z1=0; if (std::fabs(z2)<1e-20f) z2=0; return out; }
private: float b0=1,b1=0,b2=0,a1=0,a2=0,z1=0,z2=0; };
class AllpassFilter { public: void setCoeff(float c){a1=clampf(c,-0.99f,0.99f);} void reset(){z1=0;} float process(float in){ float out=a1*in+z1; z1=in-a1*out; return out;} private: float a1=0,z1=0; };
class DropoutGenerator { public: void reset(){smoothedLevel=targetLevel=1; samplesUntilNext=dropoutDuration=0; seed=987654321u;} void setSeverity(float sev){severity=clampf(sev,0,1);} float process(){ if(samplesUntilNext<=0){ if(dropoutDuration<=0){ float chance=severity*0.0005f; if((fast_rand()&0xFFFF)<(chance*65535.f)){dropoutDuration=100+(fast_rand()%2000); targetLevel=0.1f+((fast_rand()&0xFF)/255.f)*0.4f; samplesUntilNext=dropoutDuration;} else {targetLevel=1; samplesUntilNext=1000+(fast_rand()%5000);} } else {dropoutDuration--; samplesUntilNext=1;} } samplesUntilNext--; float c=(targetLevel<smoothedLevel)?0.0005f:0.002f; smoothedLevel += c*(targetLevel-smoothedLevel); return smoothedLevel; } private: uint32_t fast_rand(){seed=seed*1664525u+1013904223u; return seed;} float smoothedLevel=1,targetLevel=1; int samplesUntilNext=0,dropoutDuration=0; float severity=0.5f; uint32_t seed=987654321u; };
class TapeNoiseGenerator { public: explicit TapeNoiseGenerator(float fs){reset(fs);} void reset(float fs){state[0]=state[1]=state[2]=0; uint32_t t=(uint32_t)std::chrono::steady_clock::now().time_since_epoch().count(); seed=123456789u+t; hissShaper.setHighShelf(fs,3000,0.7f,6);} float next(){ uint32_t r=fast_rand(); if(r&1)state[0]=white(); else if(r&2)state[1]=white(); else state[2]=white(); return hissShaper.process((state[0]+state[1]+state[2])*0.33f);} private: uint32_t fast_rand(){seed=seed*1664525u+1013904223u; return seed;} float white(){uint32_t r=fast_rand(); return ((float)(r&0xFFFF)/32768.f)-1.f;} float state[3]{}; uint32_t seed=1; BiquadFilter hissShaper; };
class DelayAllpass { public: ~DelayAllpass(){delete[] buffer;} void init(int len){delete[] buffer; size=len; buffer=new(std::nothrow) float[size]{}; idx=0;} void clear(){if(buffer&&size>0) std::memset(buffer,0,sizeof(float)*size); idx=0;} void setCoeff(float f){feedback=f;} float process(float in){ if(!buffer) return in; float bo=buffer[idx]; float node=in+feedback*bo; if(std::fabs(node)<1e-15f) node=0; float out=bo-feedback*node; buffer[idx]=node; idx=(idx+1)%size; return out;} private: float* buffer=nullptr; int size=0,idx=0; float feedback=0.5f; };
}

struct TapeCore::Impl {
  explicit Impl(float fs,float maxDelayMs):sampleRate(fs),noiseGen(fs){ bufferSize=(int32_t)(fs*(maxDelayMs/1000.f)); delayLine=new(std::nothrow) float[bufferSize]{}; delayLineR=new(std::nothrow) float[bufferSize]{}; if(!delayLine||!delayLineR){delete[]delayLine;delete[]delayLineR;delayLine=delayLineR=nullptr;bufferSize=0;return;} static const float sc[6]={0.7f,0.65f,0.6f,0.6f,0.5f,0.5f}; static const int st[6]={223,367,491,647,821,1039}; for(int i=0;i<6;i++){springAP_L[i].init(st[i]);springAP_R[i].init(st[i]+23);springAP_L[i].setCoeff(sc[i]);springAP_R[i].setCoeff(sc[i]);springLPF_L[i].setLowpass(fs,2500,0.5f);springLPF_R[i].setLowpass(fs,2500,0.5f);} static const float rc[4]={0.6f,0.55f,0.5f,0.45f}; static const int rt[4]={151,313,569,797}; for(int i=0;i<4;i++){reverseAP_L[i].init(rt[i]);reverseAP_R[i].init(rt[i]+17);reverseAP_L[i].setCoeff(rc[i]);reverseAP_R[i].setCoeff(rc[i]);} updateFilters(); flutterLPF.setLowpass(fs,15,0.707f);}
  ~Impl(){delete[]delayLine;delete[]delayLineR;}
  bool valid() const {return delayLine&&delayLineR&&bufferSize>0;}
  float saturator(float x){ if(x>0.5f)x=0.5f+(x-0.5f)*0.8f; if(x>1.5f)return 1; if(x<-1.5f)return -1; return x-(0.1f*x*x*x);} 
  float feedbackCompressor(float x){ const float t=0.6f,r=1.5f; float a=std::fabs(x); if(a<=t)return x; return std::copysign(t+(a-t)/r,x);} 
  float outputLimiter(float x){ if(x>0.9f){float e=x-0.9f;x=0.9f+e*0.1f;} else if(x<-0.9f){float e=x+0.9f;x=-0.9f+e*0.1f;} return clampf(x,-0.99f,0.99f);} 
  float readTapeAt(float d,float* b){ if(!b||bufferSize==0) return 0; d=clampf(d,2,(float)bufferSize-4); float rp=(float)writeHead-d; if(rp<0)rp+=bufferSize; int32_t r=(int32_t)rp; float f=rp-r; int32_t i1=r,i2=(r>0)?r-1:bufferSize-1,i0=(r<bufferSize-1)?r+1:0,i3=(i0<bufferSize-1)?i0+1:0; float d1=b[i1],d0=b[i0],d2=b[i2],d3=b[i3]; float c0=d1,c1=0.5f*(d0-d2),c2=d2-2.5f*d1+2.f*d0-0.5f*d3,c3=0.5f*(d3-d1)+1.5f*(d1-d0); return ((c3*f+c2)*f+c1)*f+c0; }
  float readTapeReverse(float d,float* b){ if(!b||bufferSize<=0)return 0; d=clampf(d,2,(float)bufferSize-4); int32_t di=(int32_t)d; if(std::abs(di-reverseWindowSize)>1000){reverseCounter=0;reverseWindowSize=di;} reverseCounter++; if(reverseCounter>=std::max(1,di))reverseCounter=0; float rp=(float)writeHead-d+(float)reverseCounter; int32_t r=((int32_t)rp%bufferSize+bufferSize)%bufferSize; float f=rp-std::floor(rp); int32_t i1=r,i0=(r>0)?r-1:bufferSize-1,i2=(r<bufferSize-1)?r+1:0,i3=(i2<bufferSize-1)?i2+1:0; float d1=b[i1],d0=b[i0],d2=b[i2],d3=b[i3]; float c0=d1,c1=0.5f*(d0-d2),c2=d2-2.5f*d1+2.f*d0-0.5f*d3,c3=0.5f*(d3-d1)+1.5f*(d1-d0); return ((c3*f+c2)*f+c1)*f+c0; }
  void updateFilters();
  float sampleRate; TapeParams currentParams{}; float flutterPhase=0,wowPhase=0,azimuthPhase=0; float flutterInc=0,wowInc=0; BiquadFilter flutterLPF; DropoutGenerator dropout; TapeNoiseGenerator noiseGen; BiquadFilter headBump,tapeRolloff,outputLPF; AllpassFilter azimuthFilter; DCBlocker dcBlocker; BiquadFilter inputHPF,inputLPF; BiquadFilter headBumpR,tapeRolloffR,outputLPFR; AllpassFilter azimuthFilterR; DCBlocker dcBlockerR; BiquadFilter inputHPFR,inputLPFR; BiquadFilter feedbackLPF,feedbackLPFR,feedbackHPF,feedbackHPFR; AllpassFilter feedbackAllpass,feedbackAllpassR; DelayAllpass springAP_L[6],springAP_R[6],reverseAP_L[4],reverseAP_R[4]; BiquadFilter springLPF_L[6],springLPF_R[6]; float freezeFade=0; float delayEnableRamp=0,smoothedDelaySamples=0; float* delayLine=nullptr; float* delayLineR=nullptr; int32_t bufferSize=0,writeHead=0,reverseCounter=0,reverseWindowSize=0;
};

void TapeCore::Impl::updateFilters(){
  flutterInc = TWO_PI * currentParams.flutterRate / sampleRate;
  wowInc = TWO_PI * currentParams.wowRate / sampleRate;
  float speedMod=currentParams.tapeSpeed*0.01f,ageMod=currentParams.tapeAge*0.01f,toneMod=currentParams.tone*0.01f;
  if(currentParams.guitarFocus){inputHPF.setLowShelf(sampleRate,150,0.7f,-30);inputHPFR.setLowShelf(sampleRate,150,0.7f,-30);inputLPF.setLowpass(sampleRate,5000,0.707f);inputLPFR.setLowpass(sampleRate,5000,0.707f);}else{inputHPF.setLowShelf(sampleRate,20,0.7f,0);inputHPFR.setLowShelf(sampleRate,20,0.7f,0);inputLPF.setLowpass(sampleRate,20000,0.707f);inputLPFR.setLowpass(sampleRate,20000,0.707f);}  
  float bumpGain=currentParams.headBumpAmount*0.05f; headBump.setLowShelf(sampleRate,100,0.7f,bumpGain); headBumpR.setLowShelf(sampleRate,100,0.7f,bumpGain);
  float baseFreq=6000+(speedMod*10000),ageFactor=1-(ageMod*0.90f),toneFactor=(toneMod-0.5f)*2; if(toneFactor>0){ageFactor+=toneFactor*0.5f;if(ageFactor>1)ageFactor=1;} else ageFactor*=1+(toneFactor*0.5f); float cut=std::max(400.f,baseFreq*ageFactor);
  tapeRolloff.setHighShelf(sampleRate,cut,0.5f,-50); tapeRolloffR.setHighShelf(sampleRate,cut,0.5f,-50); outputLPF.setLowpass(sampleRate,cut,0.707f); outputLPFR.setLowpass(sampleRate,cut,0.707f);
  float fb=1500+(speedMod*10500); feedbackLPF.setLowpass(sampleRate,fb,0.5f); feedbackLPFR.setLowpass(sampleRate,fb,0.5f); feedbackHPF.setHighpass(sampleRate,300,0.5f); feedbackHPFR.setHighpass(sampleRate,300,0.5f);
  float ap=0.3f+ageMod*0.4f; feedbackAllpass.setCoeff(ap); feedbackAllpassR.setCoeff(ap);
  float springDecayMod=currentParams.springDecay*0.01f,springDampMod=currentParams.springDamping*0.01f,springCoeff=0.4f+springDecayMod*0.45f,dampFreq=1500+springDampMod*3000;
  for(int i=0;i<6;i++){springAP_L[i].setCoeff(springCoeff);springAP_R[i].setCoeff(springCoeff);springLPF_L[i].setLowpass(sampleRate,dampFreq,0.5f);springLPF_R[i].setLowpass(sampleRate,dampFreq,0.5f);}  
}

TapeCore::TapeCore(float sampleRate,float maxDelayMs):impl_(new(std::nothrow) Impl(sampleRate,maxDelayMs)){}
TapeCore::~TapeCore(){ delete impl_; }
bool TapeCore::isValid() const { return impl_ && impl_->valid(); }
const TapeParams& TapeCore::params() const { return impl_->currentParams; }

void TapeCore::reset(){
  if(!isValid()) return;
  std::memset(impl_->delayLine,0,sizeof(float)*impl_->bufferSize);
  std::memset(impl_->delayLineR,0,sizeof(float)*impl_->bufferSize);
  impl_->writeHead=0; impl_->reverseCounter=0; impl_->reverseWindowSize=0;
  impl_->flutterPhase=impl_->wowPhase=impl_->azimuthPhase=0;
  impl_->freezeFade=0; impl_->delayEnableRamp=0; impl_->smoothedDelaySamples=0;
  impl_->dcBlocker.clear(); impl_->dcBlockerR.clear(); impl_->dropout.reset(); impl_->noiseGen.reset(impl_->sampleRate);
  impl_->flutterLPF.reset(); impl_->headBump.reset(); impl_->headBumpR.reset(); impl_->tapeRolloff.reset(); impl_->tapeRolloffR.reset(); impl_->outputLPF.reset(); impl_->outputLPFR.reset();
  impl_->inputHPF.reset(); impl_->inputHPFR.reset(); impl_->inputLPF.reset(); impl_->inputLPFR.reset(); impl_->feedbackLPF.reset(); impl_->feedbackLPFR.reset(); impl_->feedbackHPF.reset(); impl_->feedbackHPFR.reset();
  impl_->azimuthFilter.reset(); impl_->azimuthFilterR.reset(); impl_->feedbackAllpass.reset(); impl_->feedbackAllpassR.reset();
  for(int i=0;i<6;i++){ impl_->springAP_L[i].clear(); impl_->springAP_R[i].clear(); impl_->springLPF_L[i].reset(); impl_->springLPF_R[i].reset(); }
  for(int i=0;i<4;i++){ impl_->reverseAP_L[i].clear(); impl_->reverseAP_R[i].clear(); }
  impl_->updateFilters();
}

void TapeCore::updateParams(const TapeParams& newParams){
  if(!isValid()) return;
  if(!impl_->currentParams.delayActive && newParams.delayActive){
    impl_->delayEnableRamp=0; impl_->dcBlocker.clear(); impl_->dcBlockerR.clear();
    std::memset(impl_->delayLine,0,sizeof(float)*impl_->bufferSize); std::memset(impl_->delayLineR,0,sizeof(float)*impl_->bufferSize);
    impl_->smoothedDelaySamples = newParams.delayTimeMs * impl_->sampleRate * 0.001f;
  }
  impl_->currentParams=newParams;
  impl_->dropout.setSeverity(newParams.dropoutSeverity);
  impl_->updateFilters();
}

float TapeCore::process(float input){ float oL,oR; processStereo(input,input,&oL,&oR); return oL; }

void TapeCore::processStereo(float inL,float inR,float* outL,float* outR){
  if(!isValid()){*outL=inL;*outR=inR;return;} TapeParams* p=&impl_->currentParams;
  impl_->flutterPhase += impl_->flutterInc; if(impl_->flutterPhase>TWO_PI) impl_->flutterPhase-=TWO_PI;
  impl_->wowPhase += impl_->wowInc; if(impl_->wowPhase>TWO_PI) impl_->wowPhase-=TWO_PI;
  float rawMod=(fast_sin(impl_->flutterPhase)*(p->flutterDepth*0.01f)) + (fast_sin(impl_->wowPhase)*(p->wowDepth*0.01f));
  float mod=impl_->flutterLPF.process(rawMod);
  impl_->azimuthPhase += (0.2f/impl_->sampleRate); if(impl_->azimuthPhase>1) impl_->azimuthPhase=0;
  float tri=(impl_->azimuthPhase<0.5f)?(impl_->azimuthPhase*2):(2-impl_->azimuthPhase*2); float azimuthMod=0.5f+(tri*1.5f);
  bool useAzimuth=(p->azimuthError>0.01f); if(useAzimuth){ float azCoeff=-0.90f*(p->azimuthError*0.01f)*azimuthMod; impl_->azimuthFilter.setCoeff(azCoeff); impl_->azimuthFilterR.setCoeff(azCoeff); }
  float dropoutGain=impl_->dropout.process(); float hiss=0; if(p->noise>0.001f) hiss=impl_->noiseGen.next()*(p->noise*0.001f)*(1+(2*(1-dropoutGain)));
  if(p->delayActive){impl_->delayEnableRamp+=0.001f;if(impl_->delayEnableRamp>1)impl_->delayEnableRamp=1;} else impl_->delayEnableRamp=0;
  float targetDelay=p->delayTimeMs*impl_->sampleRate*0.001f; impl_->smoothedDelaySamples += 0.0001f*(targetDelay-impl_->smoothedDelaySamples);

  auto processCh=[&](float input,float* buffer,BiquadFilter& hb,BiquadFilter& tr,BiquadFilter& outLPF,AllpassFilter& az,DCBlocker& dc,BiquadFilter&iHP,BiquadFilter&iLP,BiquadFilter&fbLPF,BiquadFilter&fbHPF,AllpassFilter&fbAP){
    float cond=iLP.process(iHP.process(input)); float tapeSig=0,headGainSum=0,base=impl_->smoothedDelaySamples,modDepth=2.f;
    float d1,d2,d3; if(p->headsMusical){ float beatMs=60000.f/p->bpm; d1=beatMs*0.333f*impl_->sampleRate*0.001f; d2=beatMs*0.75f*impl_->sampleRate*0.001f; d3=beatMs*1.0f*impl_->sampleRate*0.001f;} else {d1=base*0.33f; d2=base*0.66f; d3=base;}
    d1 += mod*40.f*modDepth; d2 += mod*60.f*modDepth; d3 += mod*80.f*modDepth;
    if(!p->delayActive){ tapeSig = impl_->readTapeAt(200.f+mod*40.f*modDepth,buffer);} else if(p->reverse){ tapeSig = impl_->readTapeReverse(d3,buffer); headGainSum=1.f;} else {
      if(p->activeHeads & 1){ tapeSig += impl_->readTapeAt(d1,buffer)*1.0f; headGainSum += 1.0f; }
      if(p->activeHeads & 2){ tapeSig += impl_->readTapeAt(d2,buffer)*0.75f; headGainSum += 0.75f; }
      if(p->activeHeads & 4){ tapeSig += impl_->readTapeAt(d3,buffer)*0.55f; headGainSum += 0.55f; }
      if(headGainSum>0) tapeSig/=headGainSum;
    }
    tapeSig *= dropoutGain; if(useAzimuth) tapeSig=az.process(tapeSig); tapeSig=outLPF.process(tr.process(hb.process(tapeSig)));
    float feedSig=0; if(p->delayActive){ feedSig=fbAP.process(fbHPF.process(fbLPF.process(tapeSig))); feedSig=std::tanh(feedSig*1.3f)/1.3f; float safe=p->feedback*0.01f; if(safe>0.88f)safe=0.88f; feedSig *= safe; feedSig = impl_->feedbackCompressor(feedSig); feedSig *= impl_->delayEnableRamp; }
    float recSig=dc.process((cond*(p->drive*0.05f))+feedSig); recSig=clampf(recSig,-4,4); if(!p->freeze) buffer[impl_->writeHead]=impl_->saturator(recSig);
    float mix=p->dryWet*0.01f; return impl_->outputLimiter((input*(1-mix))+(tapeSig*mix));
  };

  *outL=processCh(inL,impl_->delayLine,impl_->headBump,impl_->tapeRolloff,impl_->outputLPF,impl_->azimuthFilter,impl_->dcBlocker,impl_->inputHPF,impl_->inputLPF,impl_->feedbackLPF,impl_->feedbackHPF,impl_->feedbackAllpass);
  *outR=processCh(inR,impl_->delayLineR,impl_->headBumpR,impl_->tapeRolloffR,impl_->outputLPFR,impl_->azimuthFilterR,impl_->dcBlockerR,impl_->inputHPFR,impl_->inputLPFR,impl_->feedbackLPFR,impl_->feedbackHPFR,impl_->feedbackAllpassR);
  if(p->noise>0.001f){*outL += hiss*0.5f; *outR += hiss*0.5f;}
  if(p->reverseSmear && p->reverse){ for(int i=0;i<4;i++){ *outL=impl_->reverseAP_L[i].process(*outL); *outR=impl_->reverseAP_R[i].process(*outR);} }
  if(p->spring){ float dryL=*outL,dryR=*outR,wetL=*outL,wetR=*outR; int stages=3+(int)((p->springDecay*0.01f)*3); for(int i=0;i<stages&&i<6;i++){ wetL=impl_->springLPF_L[i].process(impl_->springAP_L[i].process(wetL)); wetR=impl_->springLPF_R[i].process(impl_->springAP_R[i].process(wetR)); } float wetMix=p->springMix*0.01f; *outL=impl_->outputLimiter(dryL*(1-wetMix)+wetL*wetMix); *outR=impl_->outputLimiter(dryR*(1-wetMix)+wetR*wetMix);} 
  if(p->freeze){impl_->freezeFade+=0.0002f; if(impl_->freezeFade>1)impl_->freezeFade=1;} else {impl_->freezeFade-=0.0008f; if(impl_->freezeFade<0)impl_->freezeFade=0;}
  impl_->writeHead++; if(impl_->writeHead>=impl_->bufferSize) impl_->writeHead=0;
}

} // namespace hydra::dsp
