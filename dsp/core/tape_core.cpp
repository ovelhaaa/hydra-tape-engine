#include "tape_core.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>

namespace hydra::dsp {
namespace {
constexpr float PI = 3.14159265359f;
constexpr float TWO_PI = 6.28318530718f;
constexpr int TAPE_BUFFER_GUARD = 4;
inline float clampf(float v, float lo, float hi) { return std::max(lo, std::min(v, hi)); }
inline float fast_sin(float x) {
  x -= TWO_PI * std::floor((x + PI) / TWO_PI);
  const float B = 4.0f / PI;
  const float C = -4.0f / (PI * PI);
  float y = B * x + C * x * std::fabs(x);
  const float P = 0.225f;
  return P * (y * std::fabs(y) - y) + y;
}

struct FastTanhLUT {
  static constexpr int N = 1024;
  float table[N + 1]{};

  void init() {
    for (int i = 0; i <= N; ++i) {
      float x = -4.0f + 8.0f * (float(i) / float(N));
      table[i] = std::tanh(x);
    }
  }

  inline float process(float x) const {
    x = clampf(x, -4.0f, 4.0f);
    float u = (x + 4.0f) * (float(N) * 0.125f);
    int i = static_cast<int>(u);
    if (i >= N) return table[N];
    float f = u - float(i);
    return table[i] + f * (table[i + 1] - table[i]);
  }
};

struct TapeMagnetics {
  const FastTanhLUT* lut = nullptr;
  float fs = 48000.0f;
  float m = 0.0f;
  float biasPhase = 0.0f;

  float drive = 1.0f;
  float biasAmount = 0.08f;
  float biasHz = 18000.0f;
  float coercivity = 0.12f;
  float remanence = 0.985f;
  float satNorm = 1.0f;

  void updateParams(float sampleRate, const TapeParams& params) {
    fs = std::max(1.0f, sampleRate);
    float driveNorm = clampf(params.drive * 0.01f, 0.0f, 1.0f);
    float ageNorm = clampf(params.tapeAge * 0.01f, 0.0f, 1.0f);
    float speedNorm = clampf(params.tapeSpeed * 0.01f, 0.0f, 1.0f);

    drive = 0.9f + driveNorm * 0.35f;
    biasAmount = 0.035f + (1.0f - speedNorm) * 0.035f + driveNorm * 0.035f;
    biasHz = std::min(18000.0f, fs * 0.45f);
    coercivity = 0.07f + ageNorm * 0.10f;
    remanence = 0.970f + ageNorm * 0.024f;
    satNorm = 1.0f / std::max(0.55f, 0.82f + 0.18f * remanence);
  }

  inline float process(float x) {
    if (!lut) return x;
    biasPhase += biasHz / fs;
    biasPhase -= std::floor(biasPhase);
    float tri = 4.0f * std::fabs(biasPhase - 0.5f) - 1.0f;

    float biasGate = (std::fabs(x) > 1e-7f) ? 1.0f : 0.0f;
    float xb = x * drive + (biasAmount * biasGate * tri);
    float h = xb + coercivity * m;
    float y = lut->process(h);

    m = remanence * m + (1.0f - remanence) * y;
    if (std::fabs(m) < 1e-20f) m = 0.0f;

    return satNorm * (0.82f * y + 0.18f * m);
  }

  inline void reset() {
    m = 0.0f;
    biasPhase = 0.0f;
  }
};

class DCBlocker { public: float process(float input){ float out=input-x1+R*y1; x1=input; y1=out; if (std::fabs(y1)<1e-20f) y1=0; return out;} void clear(){x1=y1=0;} private: float x1=0,y1=0,R=0.995f; };
class BiquadFilter {
public:
  void reset(){z1=z2=0;}
  void setLowShelf(float fs,float freq,float Q,float gainDB){ float A=std::pow(10.f,gainDB/40.f),w0=2*PI*freq/fs,cosw0=std::cos(w0),sinw0=std::sin(w0),alpha=sinw0/(2*Q),sqA=std::sqrt(A),ap1=A+1,am1=A-1,tw=2*sqA*alpha,a0=ap1+am1*cosw0+tw; b0=(A*(ap1-am1*cosw0+tw))/a0; b1=(2*A*(am1-ap1*cosw0))/a0; b2=(A*(ap1-am1*cosw0-tw))/a0; a1=(-2*(am1+ap1*cosw0))/a0; a2=(ap1+am1*cosw0-tw)/a0; }
  void setPeak(float fs,float freq,float Q,float gainDB){ freq=std::fmin(freq,0.45f*fs); Q=std::fmax(Q,0.001f); float A=std::pow(10.f,gainDB*0.025f),w0=TWO_PI*freq/fs,s=std::sin(w0),c=std::cos(w0),alpha=s/(2.f*Q),a0=1.f+alpha/A; b0=(1.f+alpha*A)/a0; b1=(-2.f*c)/a0; b2=(1.f-alpha*A)/a0; a1=(-2.f*c)/a0; a2=(1.f-alpha/A)/a0; }
  void setHighShelf(float fs,float freq,float Q,float gainDB){ float A=std::pow(10.f,gainDB/40.f),w0=2*PI*freq/fs,cosw0=std::cos(w0),sinw0=std::sin(w0),alpha=sinw0/(2*Q),sqA=std::sqrt(A),ap1=A+1,am1=A-1,tw=2*sqA*alpha,a0=ap1-am1*cosw0+tw; b0=(A*(ap1+am1*cosw0+tw))/a0; b1=(-2*A*(am1+ap1*cosw0))/a0; b2=(A*(ap1+am1*cosw0-tw))/a0; a1=(2*(am1-ap1*cosw0))/a0; a2=(ap1-am1*cosw0-tw)/a0; }
  void setLowpass(float fs,float freq,float Q){ float w0=2*PI*freq/fs,cosw0=std::cos(w0),sinw0=std::sin(w0),alpha=sinw0/(2*Q),a0=1+alpha; b0=((1-cosw0)/2)/a0; b1=(1-cosw0)/a0; b2=b0; a1=(-2*cosw0)/a0; a2=(1-alpha)/a0; }
  void setHighpass(float fs,float freq,float Q){ float w0=2*PI*freq/fs,cosw0=std::cos(w0),sinw0=std::sin(w0),alpha=sinw0/(2*Q),a0=1+alpha; b0=((1+cosw0)/2)/a0; b1=-(1+cosw0)/a0; b2=b0; a1=(-2*cosw0)/a0; a2=(1-alpha)/a0; }
  float process(float in){ float out=b0*in+z1; z1=b1*in-a1*out+z2; z2=b2*in-a2*out; if (std::fabs(z1)<1e-20f) z1=0; if (std::fabs(z2)<1e-20f) z2=0; return out; }
private: float b0=1,b1=0,b2=0,a1=0,a2=0,z1=0,z2=0; };
class OnePoleLP { public: void reset(){z=0;} void setCutoff(float fs,float fc){ fc=clampf(fc,20.f,0.45f*fs); float x=std::exp(-TWO_PI*fc/fs); a=1.f-x; } float process(float in){ z += a*(in-z); if(std::fabs(z)<1e-20f) z=0; return z; } private: float a=0.1f,z=0; };
class AllpassFilter { public: void setCoeff(float c){a1=clampf(c,-0.99f,0.99f);} void reset(){z1=0;} float process(float in){ float out=a1*in+z1; z1=in-a1*out; return out;} private: float a1=0,z1=0; };
class DropoutGenerator { public: void reset(){smoothedLevel=targetLevel=1; samplesUntilNext=dropoutDuration=0; seed=987654321u;} void setSeverity(float sev){severity=clampf(sev,0,1);} float process(){ if(samplesUntilNext<=0){ if(dropoutDuration<=0){ float chance=severity*0.0005f; if((fast_rand()&0xFFFF)<(chance*65535.f)){dropoutDuration=100+(fast_rand()%2000); targetLevel=0.1f+((fast_rand()&0xFF)/255.f)*0.4f; samplesUntilNext=dropoutDuration;} else {targetLevel=1; samplesUntilNext=1000+(fast_rand()%5000);} } else {dropoutDuration--; samplesUntilNext=1;} } samplesUntilNext--; float c=(targetLevel<smoothedLevel)?0.0005f:0.002f; smoothedLevel += c*(targetLevel-smoothedLevel); return smoothedLevel; } private: uint32_t fast_rand(){seed=seed*1664525u+1013904223u; return seed;} float smoothedLevel=1,targetLevel=1; int samplesUntilNext=0,dropoutDuration=0; float severity=0.5f; uint32_t seed=987654321u; };
class TapeNoiseGenerator { public: explicit TapeNoiseGenerator(float fs, uint32_t seedVal=123456789u){reset(fs,seedVal);} void reset(float fs, uint32_t seedVal=123456789u){state[0]=state[1]=state[2]=0; seed=seedVal; hissShaper.setHighShelf(fs,3000,0.7f,6);} float next(){ uint32_t r=fast_rand(); if(r&1)state[0]=white(); else if(r&2)state[1]=white(); else state[2]=white(); return hissShaper.process((state[0]+state[1]+state[2])*0.33f);} private: uint32_t fast_rand(){seed=seed*1664525u+1013904223u; return seed;} float white(){uint32_t r=fast_rand(); return ((float)(r&0xFFFF)/32768.f)-1.f;} float state[3]{}; uint32_t seed=1; BiquadFilter hissShaper; };

struct MechNoise {
  uint32_t s = 22222u;
  float z1 = 0.0f;
  float z2 = 0.0f;
  explicit MechNoise(uint32_t seed = 22222u) : s(seed) {}
  uint32_t randu(){ s = s * 1664525u + 1013904223u; return s; }
  float white(){ return float((randu() >> 9) & 0x7FFFFF) * (1.0f / 4194304.0f) - 1.0f; }
  float lowRate(float aSlow,float aFast){ float w=white(); z1 += aSlow*(w-z1); z2 += aFast*(z1-z2); return z2; }
};
class DelayAllpass { public: ~DelayAllpass(){delete[] buffer;} void init(int len){delete[] buffer; size=len; buffer=new(std::nothrow) float[size]{}; idx=0;} void clear(){if(buffer&&size>0) std::memset(buffer,0,sizeof(float)*size); idx=0;} void setCoeff(float f){feedback=f;} float process(float in){ if(!buffer) return in; float bo=buffer[idx]; float node=in+feedback*bo; if(std::fabs(node)<1e-15f) node=0; float out=bo-feedback*node; buffer[idx]=node; idx=(idx+1)%size; return out;} private: float* buffer=nullptr; int size=0,idx=0; float feedback=0.5f; };
}

struct TapeCore::Impl {
  explicit Impl(float fs,float maxDelayMs):sampleRate(fs),noiseGen(fs,123456789u),noiseGenR(fs,987654321u){ tanhLUT.init(); magneticsL.lut=&tanhLUT; magneticsR.lut=&tanhLUT; magneticsL.updateParams(fs,currentParams); magneticsR.updateParams(fs,currentParams); bufferSize=(int32_t)(fs*(maxDelayMs/1000.f)); bufferCapacity=bufferSize+TAPE_BUFFER_GUARD; delayLine=new(std::nothrow) float[bufferCapacity]{}; delayLineR=new(std::nothrow) float[bufferCapacity]{}; if(!delayLine||!delayLineR){delete[]delayLine;delete[]delayLineR;delayLine=delayLineR=nullptr;bufferSize=0;return;} static const float sc[6]={0.7f,0.65f,0.6f,0.6f,0.5f,0.5f}; static const int st[6]={223,367,491,647,821,1039}; for(int i=0;i<6;i++){springAP_L[i].init(st[i]);springAP_R[i].init(st[i]+23);springAP_L[i].setCoeff(sc[i]);springAP_R[i].setCoeff(sc[i]);springLPF_L[i].setLowpass(fs,2500,0.5f);springLPF_R[i].setLowpass(fs,2500,0.5f);} static const float rc[4]={0.6f,0.55f,0.5f,0.45f}; static const int rt[4]={151,313,569,797}; for(int i=0;i<4;i++){reverseAP_L[i].init(rt[i]);reverseAP_R[i].init(rt[i]+17);reverseAP_L[i].setCoeff(rc[i]);reverseAP_R[i].setCoeff(rc[i]);} updateFilters(); flutterLPF.setLowpass(fs,15,0.707f); flutterLPFR.setLowpass(fs,15,0.707f);}
  ~Impl(){delete[]delayLine;delete[]delayLineR;}
  bool valid() const {return delayLine&&delayLineR&&bufferSize>0;}
  float feedbackCompressor(float x){ const float t=0.6f,r=1.5f,k=0.2f; float a=std::fabs(x); if(a<=t-k*0.5f)return x; if(a>=t+k*0.5f)return std::copysign(t+(a-t)/r,x); float kx=(a-(t-k*0.5f))/k; float ratio=1.f+(1.f/r-1.f)*kx*kx; return std::copysign(a*ratio,x);}
  float outputLimiter(float x){ if(x>0.9f){float e=x-0.9f;x=0.9f+e*0.1f;} else if(x<-0.9f){float e=x+0.9f;x=-0.9f+e*0.1f;} return clampf(x,-0.99f,0.99f);}
  float hermite4(float ym1,float y0,float y1,float y2,float f){ float c0=y0,c1=0.5f*(y1-ym1),c2=ym1-2.5f*y0+2.f*y1-0.5f*y2,c3=0.5f*(y2-ym1)+1.5f*(y0-y1); return ((c3*f+c2)*f+c1)*f+c0; }
  void mirrorDelayGuard(float* b){ if(!b||bufferSize<=0)return; for(int i=0;i<TAPE_BUFFER_GUARD;i++) b[bufferSize+i]=b[i]; }
  float mechanicalMod(float scrape,BiquadFilter& flutterFilter){ float capstan=fast_sin(wowPhase)+0.18f*fast_sin(2.f*wowPhase+0.7f); float flutter=flutterFilter.process(fast_sin(flutterPhase)+0.35f*scrape); float flutterAmp=smoothedDelaySamples*(currentParams.flutterDepth*0.001f),wowAmp=smoothedDelaySamples*(currentParams.wowDepth*0.001f); return wowAmp*capstan+flutterAmp*flutter+0.00015f*smoothedDelaySamples*scrape; }
  float readTapeAt(float d,float* b){ if(!b||bufferSize==0) return 0; d=clampf(d,2,(float)bufferSize-4); float rp=(float)writeHead-d; if(rp<0)rp+=bufferSize; int32_t r=(int32_t)rp; float f=rp-r; int32_t prev=(r>0)?r-1:bufferSize-1; return hermite4(b[prev],b[r],b[r+1],b[r+2],f); }
  float readTapeReverse(float d,float* b){ if(!b||bufferSize<=0)return 0; d=clampf(d,2,(float)bufferSize-4); int32_t di=(int32_t)d; if(std::abs(di-reverseWindowSize)>1000){reverseCounter=0;reverseWindowSize=di;} reverseCounter++; if(reverseCounter>=std::max(1,di))reverseCounter=0; float rp=(float)writeHead-d+(float)reverseCounter; int32_t r=((int32_t)rp%bufferSize+bufferSize)%bufferSize; float f=rp-std::floor(rp); int32_t i1=r,i0=(r>0)?r-1:bufferSize-1,i2=(r<bufferSize-1)?r+1:0,i3=(i2<bufferSize-1)?i2+1:0; float d1=b[i1],d0=b[i0],d2=b[i2],d3=b[i3]; float c0=d1,c1=0.5f*(d0-d2),c2=d2-2.5f*d1+2.f*d0-0.5f*d3,c3=0.5f*(d3-d1)+1.5f*(d1-d0); return ((c3*f+c2)*f+c1)*f+c0; }
  void updateFilters();
  FastTanhLUT tanhLUT; TapeMagnetics magneticsL,magneticsR; float sampleRate; TapeParams currentParams{}; float flutterPhase=0,wowPhase=0,azimuthPhase=0; float flutterInc=0,wowInc=0,azimuthInc=0,delaySmoothAlpha=0,delayRampInc=0; BiquadFilter flutterLPF,flutterLPFR; MechNoise mechNoiseL{22222u},mechNoiseR{33333u}; DropoutGenerator dropout; TapeNoiseGenerator noiseGen, noiseGenR; BiquadFilter reproHeadBump,tapeRolloff,gapLossLPF; AllpassFilter azimuthFilter; DCBlocker dcBlocker; BiquadFilter inputHPF,inputLPF; BiquadFilter reproHeadBumpR,tapeRolloffR,gapLossLPFR; AllpassFilter azimuthFilterR; DCBlocker dcBlockerR; BiquadFilter inputHPFR,inputLPFR; OnePoleLP feedbackAgingFilter,feedbackAgingFilterR; BiquadFilter feedbackHPF,feedbackHPFR,feedbackBump,feedbackBumpR; AllpassFilter feedbackAllpass,feedbackAllpassR; DelayAllpass springAP_L[6],springAP_R[6],reverseAP_L[4],reverseAP_R[4]; BiquadFilter springLPF_L[6],springLPF_R[6]; float springFB_L=0,springFB_R=0; float freezeFade=0; float delayEnableRamp=0,smoothedDelaySamples=0,smoothedAzCoeff=0; float* delayLine=nullptr; float* delayLineR=nullptr; int32_t bufferSize=0,bufferCapacity=0,writeHead=0,reverseCounter=0,reverseWindowSize=0;
};

void TapeCore::Impl::updateFilters(){
  magneticsL.updateParams(sampleRate,currentParams); magneticsR.updateParams(sampleRate,currentParams);
  flutterInc = TWO_PI * currentParams.flutterRate / sampleRate;
  wowInc = TWO_PI * currentParams.wowRate / sampleRate;
  azimuthInc = 0.2f / sampleRate;
  delaySmoothAlpha = 1.f - std::exp(-1.f / (0.200f * sampleRate));
  delayRampInc = 1.f / (0.250f * sampleRate);
  float speedMod=currentParams.tapeSpeed*0.01f,ageMod=currentParams.tapeAge*0.01f,toneMod=currentParams.tone*0.01f;
  if(currentParams.guitarFocus){inputHPF.setHighpass(sampleRate,150.f,0.707f);inputHPFR.setHighpass(sampleRate,150.f,0.707f);inputLPF.setLowpass(sampleRate,5000.f,0.707f);inputLPFR.setLowpass(sampleRate,5000.f,0.707f);}else{inputHPF.setHighpass(sampleRate,20.f,0.707f);inputHPFR.setHighpass(sampleRate,20.f,0.707f);inputLPF.setLowpass(sampleRate,20000.f,0.707f);inputLPFR.setLowpass(sampleRate,20000.f,0.707f);}
  float bumpFreq=60.f+(speedMod*140.f);
  float reproBumpGain=clampf(currentParams.headBumpAmount*0.06f,0.f,6.f);
  float feedbackBumpGain=clampf(currentParams.headBumpAmount*0.02f,0.f,2.f);
  reproHeadBump.setPeak(sampleRate,bumpFreq,0.9f,reproBumpGain); reproHeadBumpR.setPeak(sampleRate,bumpFreq,0.9f,reproBumpGain);
  feedbackBump.setPeak(sampleRate,bumpFreq,0.8f,feedbackBumpGain); feedbackBumpR.setPeak(sampleRate,bumpFreq,0.8f,feedbackBumpGain);
  float baseFreq=6000+(speedMod*10000),ageFactor=1-(ageMod*0.90f),toneFactor=(toneMod-0.5f)*2; if(toneFactor>0){ageFactor+=toneFactor*0.5f;if(ageFactor>1)ageFactor=1;} else ageFactor*=1+(toneFactor*0.5f); float cut=std::max(400.f,baseFreq*ageFactor);
  tapeRolloff.setHighShelf(sampleRate,cut*2.f,0.5f,-12.f); tapeRolloffR.setHighShelf(sampleRate,cut*2.f,0.5f,-12.f); gapLossLPF.setLowpass(sampleRate,cut,0.707f); gapLossLPFR.setLowpass(sampleRate,cut,0.707f);
  float fb=1500+(speedMod*10500); feedbackAgingFilter.setCutoff(sampleRate,fb); feedbackAgingFilterR.setCutoff(sampleRate,fb); feedbackHPF.setHighpass(sampleRate,300,0.5f); feedbackHPFR.setHighpass(sampleRate,300,0.5f);
  float ap=0.3f+ageMod*0.4f; feedbackAllpass.setCoeff(ap); feedbackAllpassR.setCoeff(ap); float flutterCutoff=clampf(currentParams.flutterRate*1.6f,4.f,12.f); flutterLPF.setLowpass(sampleRate,flutterCutoff,0.707f); flutterLPFR.setLowpass(sampleRate,flutterCutoff,0.707f);
  float springDecayMod=currentParams.springDecay*0.01f,springDampMod=currentParams.springDamping*0.01f,springCoeff=0.4f+springDecayMod*0.45f,dampFreq=1500+springDampMod*3000;
  for(int i=0;i<6;i++){springAP_L[i].setCoeff(springCoeff);springAP_R[i].setCoeff(springCoeff);springLPF_L[i].setLowpass(sampleRate,dampFreq,0.5f);springLPF_R[i].setLowpass(sampleRate,dampFreq,0.5f);}
}

TapeCore::TapeCore(float sampleRate,float maxDelayMs):impl_(new(std::nothrow) Impl(sampleRate,maxDelayMs)){}
TapeCore::~TapeCore(){ delete impl_; }
bool TapeCore::isValid() const { return impl_ && impl_->valid(); }
const TapeParams& TapeCore::params() const { return impl_->currentParams; }

void TapeCore::reset(){
  if(!isValid()) return;
  std::memset(impl_->delayLine,0,sizeof(float)*impl_->bufferCapacity);
  std::memset(impl_->delayLineR,0,sizeof(float)*impl_->bufferCapacity);
  impl_->writeHead=0; impl_->reverseCounter=0; impl_->reverseWindowSize=0;
  impl_->flutterPhase=impl_->wowPhase=impl_->azimuthPhase=0;
  impl_->freezeFade=0; impl_->delayEnableRamp=0; impl_->smoothedDelaySamples=0; impl_->smoothedAzCoeff=0; impl_->springFB_L=impl_->springFB_R=0;
  impl_->dcBlocker.clear(); impl_->dcBlockerR.clear(); impl_->magneticsL.reset(); impl_->magneticsR.reset(); impl_->dropout.reset(); impl_->noiseGen.reset(impl_->sampleRate, 123456789u); impl_->noiseGenR.reset(impl_->sampleRate, 987654321u);
  impl_->flutterLPF.reset(); impl_->flutterLPFR.reset(); impl_->reproHeadBump.reset(); impl_->reproHeadBumpR.reset(); impl_->tapeRolloff.reset(); impl_->tapeRolloffR.reset(); impl_->gapLossLPF.reset(); impl_->gapLossLPFR.reset();
  impl_->inputHPF.reset(); impl_->inputHPFR.reset(); impl_->inputLPF.reset(); impl_->inputLPFR.reset(); impl_->feedbackAgingFilter.reset(); impl_->feedbackAgingFilterR.reset(); impl_->feedbackBump.reset(); impl_->feedbackBumpR.reset(); impl_->feedbackHPF.reset(); impl_->feedbackHPFR.reset();
  impl_->azimuthFilter.reset(); impl_->azimuthFilterR.reset(); impl_->feedbackAllpass.reset(); impl_->feedbackAllpassR.reset();
  for(int i=0;i<6;i++){ impl_->springAP_L[i].clear(); impl_->springAP_R[i].clear(); impl_->springLPF_L[i].reset(); impl_->springLPF_R[i].reset(); }
  for(int i=0;i<4;i++){ impl_->reverseAP_L[i].clear(); impl_->reverseAP_R[i].clear(); }
  impl_->updateFilters();
}

void TapeCore::updateParams(const TapeParams& newParams){
  if(!isValid()) return;
  if(!impl_->currentParams.delayActive && newParams.delayActive){
    impl_->delayEnableRamp=0; impl_->dcBlocker.clear(); impl_->dcBlockerR.clear(); impl_->magneticsL.reset(); impl_->magneticsR.reset();
    std::memset(impl_->delayLine,0,sizeof(float)*impl_->bufferCapacity); std::memset(impl_->delayLineR,0,sizeof(float)*impl_->bufferCapacity);
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

  float targetDelay=p->delayTimeMs*impl_->sampleRate*0.001f;
  impl_->smoothedDelaySamples += impl_->delaySmoothAlpha*(targetDelay-impl_->smoothedDelaySamples);

  float scrapeL=impl_->mechNoiseL.lowRate(0.00008f,0.0025f);
  float scrapeR=impl_->mechNoiseR.lowRate(0.00008f,0.0025f);
  float modL=impl_->mechanicalMod(scrapeL,impl_->flutterLPF);
  float modR=impl_->mechanicalMod(scrapeR,impl_->flutterLPFR);

  impl_->azimuthPhase += impl_->azimuthInc; if(impl_->azimuthPhase>1) impl_->azimuthPhase=0;
  float tri=(impl_->azimuthPhase<0.5f)?(impl_->azimuthPhase*2):(2-impl_->azimuthPhase*2); float azimuthMod=0.5f+(tri*1.5f);
  bool useAzimuth=(p->azimuthError>0.01f);
  if(useAzimuth){
    float targetAz=-0.90f*(p->azimuthError*0.01f)*azimuthMod;
    impl_->smoothedAzCoeff += 0.001f * (targetAz - impl_->smoothedAzCoeff);
    impl_->azimuthFilter.setCoeff(impl_->smoothedAzCoeff);
    impl_->azimuthFilterR.setCoeff(impl_->smoothedAzCoeff);
  }

  float dropoutGain=impl_->dropout.process();
  float hissL=0, hissR=0;
  if(p->noise>0.001f) {
    float noiseMult = (p->noise*0.001f)*(1+(2*(1-dropoutGain)));
    hissL=impl_->noiseGen.next()*noiseMult;
    hissR=impl_->noiseGenR.next()*noiseMult;
  }
  if(p->delayActive){impl_->delayEnableRamp+=impl_->delayRampInc;if(impl_->delayEnableRamp>1)impl_->delayEnableRamp=1;} else impl_->delayEnableRamp=0;

  auto processCh=[&](float input,float* buffer,BiquadFilter& reproHeadBumpFilter,BiquadFilter& tr,BiquadFilter& gapLossFilter,AllpassFilter& az,DCBlocker& dc,TapeMagnetics& mag,BiquadFilter&iHP,BiquadFilter&iLP,OnePoleLP&feedbackAgingFilt,BiquadFilter&fbHPF,BiquadFilter&feedbackBumpFilter,AllpassFilter&fbAP,float mod){
    float cond=iLP.process(iHP.process(input)); float tapeSig=0,headGainSum=0,base=impl_->smoothedDelaySamples;
    float d1,d2,d3; if(p->headsMusical){ float beatMs=60000.f/p->bpm; d1=beatMs*0.333f*impl_->sampleRate*0.001f; d2=beatMs*0.75f*impl_->sampleRate*0.001f; d3=beatMs*1.0f*impl_->sampleRate*0.001f;} else {d1=base*0.33f; d2=base*0.66f; d3=base;}
    d1 += mod * 0.33f; d2 += mod * 0.66f; d3 += mod;
    if(!p->delayActive){ tapeSig = impl_->readTapeAt(200.f+mod,buffer);} else if(p->reverse){ tapeSig = impl_->readTapeReverse(d3,buffer); headGainSum=1.f;} else {
      if(p->activeHeads & 1){ tapeSig += impl_->readTapeAt(d1,buffer)*1.0f; headGainSum += 1.0f; }
      if(p->activeHeads & 2){ tapeSig += impl_->readTapeAt(d2,buffer)*0.75f; headGainSum += 0.75f; }
      if(p->activeHeads & 4){ tapeSig += impl_->readTapeAt(d3,buffer)*0.55f; headGainSum += 0.55f; }
      if(headGainSum>0) tapeSig/=headGainSum;
    }
    tapeSig *= dropoutGain; if(useAzimuth) tapeSig=az.process(tapeSig);
    tapeSig=gapLossFilter.process(tapeSig);
    float feedSig=0; if(p->delayActive){ feedSig=feedbackAgingFilt.process(tapeSig); feedSig=fbAP.process(fbHPF.process(feedbackBumpFilter.process(feedSig))); feedSig=impl_->tanhLUT.process(feedSig*1.3f)/1.3f; float safe=p->feedback*0.01f; if(safe>0.88f)safe=0.88f; feedSig *= safe; feedSig = impl_->feedbackCompressor(feedSig); feedSig *= impl_->delayEnableRamp; }
    tapeSig=reproHeadBumpFilter.process(tr.process(tapeSig));
    float recSig=dc.process((cond*(p->drive*0.05f))+feedSig); recSig=clampf(recSig,-4,4); if(!p->freeze) buffer[impl_->writeHead]=mag.process(recSig);
    float mix=p->dryWet*0.01f; return impl_->outputLimiter((input*(1-mix))+(tapeSig*mix));
  };

  *outL=processCh(inL,impl_->delayLine,impl_->reproHeadBump,impl_->tapeRolloff,impl_->gapLossLPF,impl_->azimuthFilter,impl_->dcBlocker,impl_->magneticsL,impl_->inputHPF,impl_->inputLPF,impl_->feedbackAgingFilter,impl_->feedbackHPF,impl_->feedbackBump,impl_->feedbackAllpass,modL);
  *outR=processCh(inR,impl_->delayLineR,impl_->reproHeadBumpR,impl_->tapeRolloffR,impl_->gapLossLPFR,impl_->azimuthFilterR,impl_->dcBlockerR,impl_->magneticsR,impl_->inputHPFR,impl_->inputLPFR,impl_->feedbackAgingFilterR,impl_->feedbackHPFR,impl_->feedbackBumpR,impl_->feedbackAllpassR,modR);
  if(p->noise>0.001f){*outL += hissL*0.5f; *outR += hissR*0.5f;}
  if(p->reverseSmear && p->reverse){ for(int i=0;i<4;i++){ *outL=impl_->reverseAP_L[i].process(*outL); *outR=impl_->reverseAP_R[i].process(*outR);} }
  if(p->spring){
    float dryL=*outL,dryR=*outR;
    int stages=3+(int)((p->springDecay*0.01f)*3);
    float inWithFBL = *outL + impl_->springFB_L * (p->springDecay * 0.01f * 0.85f);
    float inWithFBR = *outR + impl_->springFB_R * (p->springDecay * 0.01f * 0.85f);
    float wetL = inWithFBL, wetR = inWithFBR;
    for(int i=0;i<stages&&i<6;i++){ wetL=impl_->springLPF_L[i].process(impl_->springAP_L[i].process(wetL)); wetR=impl_->springLPF_R[i].process(impl_->springAP_R[i].process(wetR)); }
    impl_->springFB_L = wetL; impl_->springFB_R = wetR;
    float wetMix=p->springMix*0.01f;
    *outL=impl_->outputLimiter(dryL*(1-wetMix)+wetL*wetMix); *outR=impl_->outputLimiter(dryR*(1-wetMix)+wetR*wetMix);
  }
  if(p->freeze){impl_->freezeFade+=0.0002f; if(impl_->freezeFade>1)impl_->freezeFade=1;} else {impl_->freezeFade-=0.0008f; if(impl_->freezeFade<0)impl_->freezeFade=0;}
  if(impl_->writeHead<TAPE_BUFFER_GUARD){ impl_->mirrorDelayGuard(impl_->delayLine); impl_->mirrorDelayGuard(impl_->delayLineR); }
  impl_->writeHead++; if(impl_->writeHead>=impl_->bufferSize) impl_->writeHead=0;
}

} // namespace hydra::dsp
