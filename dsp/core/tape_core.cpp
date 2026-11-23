#include "tape_core.hpp"

#if defined(__GNUC__) || defined(__clang__)
#define HYDRA_AUDIO_INLINE __attribute__((always_inline)) inline
#define HYDRA_AUDIO_NOINLINE __attribute__((noinline))
#define HYDRA_LAMBDA_INLINE __attribute__((always_inline))
#else
#define HYDRA_AUDIO_INLINE inline
#define HYDRA_AUDIO_NOINLINE
#define HYDRA_LAMBDA_INLINE
#endif

#include "hydra_mech_noise.hpp"
#include "tape_integrity.hpp"
#include "realtime_profile.hpp"
// The B and C listening variants are deliberately separate binaries.
#if defined(HYDRA_PHASE72_EXPERIMENTAL_FEEDBACK_ROUTING) && HYDRA_PHASE72_EXPERIMENTAL_FEEDBACK_ROUTING && \
    defined(HYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING) && HYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING
#error "Phase 7.2 experiments B and C must not be enabled together"
#endif
#if (defined(HYDRA_PHASE72_EXPERIMENTAL_FEEDBACK_ROUTING) && HYDRA_PHASE72_EXPERIMENTAL_FEEDBACK_ROUTING) || \
    (defined(HYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING) && HYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING)
#include "feedback_routing_experiment.hpp"
#endif
#include "reverse_windows.hpp"
#include "tape_motion_engine.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>

#if defined(ARDUINO_ARCH_ESP32) || defined(ESP_PLATFORM)
#include "esp_attr.h"
#include "esp_heap_caps.h"
#define HYDRA_TAPE_CORE_USE_PSRAM 1
#define HYDRA_DSP_IRAM IRAM_ATTR
#else
#define HYDRA_DSP_IRAM
#endif

namespace hydra::dsp {
namespace {
constexpr float PI = 3.14159265359f;
constexpr float TWO_PI = 6.28318530718f;
constexpr int TAPE_BUFFER_GUARD = 4;
constexpr float kHissDuckAttackTimeSeconds = 0.002f;
constexpr float kHissDuckReleaseTimeSeconds = 0.080f;
constexpr float kHissNoiseMinDuckGain = 0.22f;
HYDRA_AUDIO_INLINE float clampf(float v, float lo, float hi) { return std::max(lo, std::min(v, hi)); }
HYDRA_AUDIO_INLINE float fastRecip(float d) {
#if defined(__XTENSA__) && defined(__XCHAL_HAVE_FP_RECIP) && (__XCHAL_HAVE_FP_RECIP == 1)
  float y, e;
  __asm__("recip0.s %0, %2\n\t"
          "const.s  %1, 1\n\t"
          "msub.s   %1, %2, %0\n\t"
          "madd.s   %0, %0, %1\n\t"
          "const.s  %1, 1\n\t"
          "msub.s   %1, %2, %0\n\t"
          "madd.s   %0, %0, %1\n\t"
          : "=&f"(y), "=&f"(e)
          : "f"(d));
  return y;
#else
  return 1.0f / d;
#endif
}
HYDRA_AUDIO_INLINE float fastSatRational(float x) {
  x = clampf(x, -4.0f, 4.0f);
  float ax = std::fabs(x);
  return clampf(x * (27.0f + ax * ax) * fastRecip(27.0f + 9.0f * ax * ax), -1.0f, 1.0f);
}
HYDRA_AUDIO_INLINE float fast_sin(float x) {
  while (x > PI) x -= TWO_PI;
  while (x < -PI) x += TWO_PI;
  const float B = 4.0f / PI;
  const float C = -4.0f / (PI * PI);
  float y = B * x + C * x * std::fabs(x);
  const float P = 0.225f;
  return P * (y * std::fabs(y) - y) + y;
}

inline float* allocateDelayBuffer(int32_t capacity) {
  if(capacity<=0) return nullptr;
  size_t bytes=sizeof(float)*static_cast<size_t>(capacity);
#if defined(HYDRA_TAPE_CORE_USE_PSRAM)
  void* ptr=heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  if(!ptr) ptr=heap_caps_malloc(bytes,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
  if(ptr) std::memset(ptr,0,bytes);
  return static_cast<float*>(ptr);
#else
  return new(std::nothrow) float[capacity]{};
#endif
}
inline void freeDelayBuffer(float* buffer) {
#if defined(HYDRA_TAPE_CORE_USE_PSRAM)
  if(buffer) heap_caps_free(buffer);
#else
  delete[] buffer;
#endif
}

// The table constants are committed, not evaluated at run time. std::tanh is a
// platform libm call: glibc, the Windows UCRT and musl/Emscripten disagree on
// the last bit of some results, and after quantisation to float the old
// runtime table differed in 84 of 1025 entries between Native and WASM. That
// was the first stage where the two builds diverged (issue #48). The mapping,
// interpolation and clamping below are unchanged.
#include "fast_tanh_table.inc"

struct FastTanhLUT {
  static constexpr int N = kFastTanhTableSize;

  HYDRA_AUDIO_INLINE float process(float x) const {
    x = clampf(x, -4.0f, 4.0f);
    float u = (x + 4.0f) * (float(N) * 0.125f);
    int i = static_cast<int>(u);
    if (i >= N) return kFastTanhTable[N];
    float f = u - float(i);
    return kFastTanhTable[i] + f * (kFastTanhTable[i + 1] - kFastTanhTable[i]);
  }
};

struct TapeMagnetics {
  const FastTanhLUT* lut = nullptr;
  float fs = 48000.0f;
  float m = 0.0f;
  float biasPhase = 0.0f;
  float biasGain = 0.0f;

  float drive = 1.0f;
  float biasAmount = 0.08f;
  float biasHz = 18000.0f;
  float biasPhaseInc = 18000.0f / 48000.0f;
  float biasGainStep = 1.0f / 24.0f;
  float coercivity = 0.12f;
  float remanence = 0.985f;
  float satNorm = 1.0f;

  HYDRA_DSP_IRAM void updateParams(float sampleRate, const TapeParams& params) {
    fs = std::max(1.0f, sampleRate);
    float driveNorm = clampf(params.drive * 0.01f, 0.0f, 1.0f);
    float ageNorm = clampf(params.tapeAge * 0.01f, 0.0f, 1.0f);
    float speedNorm = clampf(params.tapeSpeed * 0.01f, 0.0f, 1.0f);

    drive = 0.9f + driveNorm * 0.35f;
    biasAmount = 0.035f + (1.0f - speedNorm) * 0.035f + driveNorm * 0.035f;
    biasHz = std::min(18000.0f, fs * 0.45f);
    biasPhaseInc = biasHz / fs;
    // Linear full-scale transition. Clamping reaches exact zero in silence.
    biasGainStep = 1.0f / std::max(1.0f, std::round(fs * 0.0005f));
    coercivity = 0.07f + ageNorm * 0.10f;
    remanence = 0.970f + ageNorm * 0.024f;
    satNorm = 1.0f / std::max(0.55f, 0.82f + 0.18f * remanence);
  }

  HYDRA_DSP_IRAM HYDRA_AUDIO_INLINE float process(float x, bool allowBias) {
    if (!lut) return x;

    const bool biasRequested = allowBias && std::fabs(x) > 1e-7f;
    biasGain = clampf(biasGain + (biasRequested ? biasGainStep : -biasGainStep), 0.0f, 1.0f);
    float xb = x * drive;
    float h = xb + coercivity * m;
    // Phase-average of the original triangle's uniform amplitude distribution.
    // Two-point Gauss-Legendre: exact moments through order three.
    // Keep the original coercivity, remanence, normalization and gain ramp.
    const float b = biasAmount * biasGain * 0.5773502691896258f;
    float y = 0.5f * (lut->process(h - b) + lut->process(h + b));

    m = remanence * m + (1.0f - remanence) * y;

    return satNorm * (0.82f * y + 0.18f * m);
  }

  inline void flushDenormals() {
    if (std::fabs(m) < 1e-20f) m = 0.0f;
  }

  inline void reset() {
    m = 0.0f;
    biasPhase = 0.0f;
    biasGain = 0.0f;
  }
};

class DCBlocker { public: HYDRA_AUDIO_INLINE float process(float input){ float out=input-x1+R*y1; x1=input; y1=out; return out;} void flushDenormals(){ if (std::fabs(y1)<1e-20f) y1=0.0f; } void clear(){x1=y1=0;} private: float x1=0,y1=0,R=0.995f; };
class BiquadFilter {
public:
  void reset(){z1=z2=0;}
  void flushDenormals(){ if (std::fabs(z1)<1e-20f) z1=0.0f; if (std::fabs(z2)<1e-20f) z2=0.0f; }
  HYDRA_DSP_IRAM void setLowShelf(float fs,float freq,float Q,float gainDB){ freq=detail::boundedBiquadFrequency(fs,freq); Q=detail::boundedBiquadQ(Q); float A=std::pow(10.f,gainDB/40.f),w0=2*PI*freq/fs,cosw0=std::cos(w0),sinw0=std::sin(w0),alpha=sinw0/(2*Q),sqA=std::sqrt(A),ap1=A+1,am1=A-1,tw=2*sqA*alpha,a0=ap1+am1*cosw0+tw; b0=(A*(ap1-am1*cosw0+tw))/a0; b1=(2*A*(am1-ap1*cosw0))/a0; b2=(A*(ap1-am1*cosw0-tw))/a0; a1=(-2*(am1+ap1*cosw0))/a0; a2=(ap1+am1*cosw0-tw)/a0; }
  HYDRA_DSP_IRAM void setPeak(float fs,float freq,float Q,float gainDB){ freq=detail::boundedBiquadFrequency(fs,freq); Q=detail::boundedBiquadQ(Q); float A=std::pow(10.f,gainDB*0.025f),w0=TWO_PI*freq/fs,s=std::sin(w0),c=std::cos(w0),alpha=s/(2.f*Q),a0=1.f+alpha/A; b0=(1.f+alpha*A)/a0; b1=(-2.f*c)/a0; b2=(1.f-alpha*A)/a0; a1=(-2.f*c)/a0; a2=(1.f-alpha/A)/a0; }
  HYDRA_DSP_IRAM void setHighShelf(float fs,float freq,float Q,float gainDB){ freq=detail::boundedBiquadFrequency(fs,freq); Q=detail::boundedBiquadQ(Q); float A=std::pow(10.f,gainDB/40.f),w0=2*PI*freq/fs,cosw0=std::cos(w0),sinw0=std::sin(w0),alpha=sinw0/(2*Q),sqA=std::sqrt(A),ap1=A+1,am1=A-1,tw=2*sqA*alpha,a0=ap1-am1*cosw0+tw; b0=(A*(ap1+am1*cosw0+tw))/a0; b1=(-2*A*(am1+ap1*cosw0))/a0; b2=(A*(ap1+am1*cosw0-tw))/a0; a1=(2*(am1-ap1*cosw0))/a0; a2=(ap1-am1*cosw0-tw)/a0; }
  HYDRA_DSP_IRAM void setLowpass(float fs,float freq,float Q){ freq=detail::boundedBiquadFrequency(fs,freq); Q=detail::boundedBiquadQ(Q); float w0=2*PI*freq/fs,cosw0=std::cos(w0),sinw0=std::sin(w0),alpha=sinw0/(2*Q),a0=1+alpha; b0=((1-cosw0)/2)/a0; b1=(1-cosw0)/a0; b2=b0; a1=(-2*cosw0)/a0; a2=(1-alpha)/a0; }
  HYDRA_DSP_IRAM void setHighpass(float fs,float freq,float Q){ freq=detail::boundedBiquadFrequency(fs,freq); Q=detail::boundedBiquadQ(Q); float w0=2*PI*freq/fs,cosw0=std::cos(w0),sinw0=std::sin(w0),alpha=sinw0/(2*Q),a0=1+alpha; b0=((1+cosw0)/2)/a0; b1=-(1+cosw0)/a0; b2=b0; a1=(-2*cosw0)/a0; a2=(1-alpha)/a0; }
  inline void copyCoeffsFrom(const BiquadFilter& other){ b0=other.b0; b1=other.b1; b2=other.b2; a1=other.a1; a2=other.a2; }
  HYDRA_AUDIO_INLINE float process(float in){ float out=b0*in+z1; z1=b1*in-a1*out+z2; z2=b2*in-a2*out; return out; }
private: float b0=1,b1=0,b2=0,a1=0,a2=0,z1=0,z2=0; };
class OnePoleLP { public: void reset(){z=0;} void flushDenormals(){ if(std::fabs(z)<1e-20f) z=0.0f; } void setCutoff(float fs,float fc){ fc=clampf(fc,20.f,0.45f*fs); float x=std::exp(-TWO_PI*fc/fs); a=1.f-x; } inline void copyCoeffFrom(const OnePoleLP& other){ a=other.a; } HYDRA_AUDIO_INLINE void setCutoffFast(float fs,float fc){ fc=clampf(fc,20.f,0.45f*fs); float x=TWO_PI*fc/fs; a=clampf(x/(1.f+x),0.f,1.f); } HYDRA_AUDIO_INLINE void setCutoffFastInv(float maxFc,float invFs,float fc){ fc=clampf(fc,20.f,maxFc); float x=TWO_PI*fc*invFs; a=clampf(x*fastRecip(1.f+x),0.f,1.f); } HYDRA_AUDIO_INLINE float process(float in){ z += a*(in-z); return z; } private: float a=0.1f,z=0; };
class AllpassFilter { public: void setCoeff(float c){a1=clampf(c,-0.99f,0.99f);} void reset(){z1=0;} void flushDenormals(){ if(std::fabs(z1)<1e-20f) z1=0.0f; } HYDRA_AUDIO_INLINE float process(float in){ float out=a1*in+z1; z1=in-a1*out; return out;} private: float a1=0,z1=0; };
struct DropoutFrame { float ampGain=1.0f; float hfLoss=1.0f; };
class DropoutGenerator {
public:
  void reset(){common.reset(987654321u); local[0].reset(2246822519u); local[1].reset(3266489917u); severity=0.5f; frame={1.0f,1.0f};}
  void setSampleRate(float fs){common.setSampleRate(fs); local[0].setSampleRate(fs); local[1].setSampleRate(fs);}
  void setSeverity(float sev){severity=clampf(sev,0,1); common.setSeverity(severity); local[0].setSeverity(severity*0.45f); local[1].setSeverity(severity*0.45f);}
  HYDRA_DSP_IRAM inline void beginFrame(float fs){common.process(fs); frame.ampGain=common.amp; frame.hfLoss=common.hf;}
  HYDRA_DSP_IRAM inline DropoutFrame value(int channel,float fs){int ch=(channel!=0)?1:0; local[ch].process(fs); DropoutFrame out; out.ampGain=clampf(frame.ampGain*(0.75f+0.25f*local[ch].amp),0.02f,1.0f); out.hfLoss=clampf(frame.hfLoss*(0.70f+0.30f*local[ch].hf),0.02f,1.0f); return out;}
private:
  struct State {
    float amp=1.0f, ampTarget=1.0f, hf=1.0f, hfTarget=1.0f, severity=0.5f; int remain=0; uint32_t s=987654321u;
    float cachedFs=0.0f, srScale=1.0f, trigProb=0.00006f, downAmp=0.0042f, upAmp=0.00025f, downHf=0.00756f, upHf=0.000175f;
    void setSampleRate(float fs){if(fs!=cachedFs){cachedFs=fs; srScale=48000.f/std::max(1.f,fs); updateRates();}}
    void updateRates(){ trigProb=(severity*0.00012f)*srScale; downAmp=(0.0012f+0.006f*severity)*srScale; upAmp=0.00025f*srScale; downHf=downAmp*1.8f; upHf=upAmp*0.7f; }
    void setSeverity(float sev){ severity=sev; updateRates(); }
    void reset(uint32_t seed){amp=ampTarget=hf=hfTarget=1.0f; remain=0; s=seed; updateRates();}
    HYDRA_AUDIO_INLINE uint32_t randu(){s=s*1664525u+1013904223u; return s;}
    HYDRA_AUDIO_INLINE float rand01(){return float(randu()>>8)*(1.0f/16777216.0f);}
    HYDRA_DSP_IRAM inline void maybeTrigger(float fs){if(rand01()<trigProb){float r=rand01(); if(r<0.72f){remain=int((0.001f+0.019f*rand01())*fs); ampTarget=0.65f+0.30f*rand01(); hfTarget=0.35f+0.45f*rand01();} else if(r<0.96f){remain=int((0.020f+0.130f*rand01())*fs); ampTarget=0.18f+0.55f*rand01(); hfTarget=0.12f+0.35f*rand01();} else {remain=int((0.150f+0.600f*rand01())*fs); ampTarget=0.05f+0.35f*rand01(); hfTarget=0.05f+0.20f*rand01();}}}
    HYDRA_DSP_IRAM inline void process(float fs){if(remain<=0){ampTarget=1.0f; hfTarget=1.0f; maybeTrigger(fs);} else --remain; float ca=(ampTarget<amp)?downAmp:upAmp, ch=(hfTarget<hf)?downHf:upHf; amp += ca*(ampTarget-amp); hf += ch*(hfTarget-hf);}
  } common, local[2];
  float severity=0.5f; DropoutFrame frame;
};
class TapeNoiseGenerator {
public:
  explicit TapeNoiseGenerator(float fs, uint32_t seedVal=123456789u){reset(fs,seedVal);} 
  void reset(float fs, uint32_t seedVal=123456789u){
    pink1=pink2=pink3=0.0f; seed=seedVal;
    hissHPF.reset(); hissLPF.reset(); hissTilt.reset();
    hissHPF.setHighpass(fs,130.0f,0.707f);
    hissLPF.setLowpass(fs,9000.0f,0.707f);
    hissTilt.setHighShelf(fs,2400.0f,0.65f,3.0f);
  }
  HYDRA_DSP_IRAM inline float next(){
    const float w=white();
    pink1 += 0.0211f*(w-pink1);
    pink2 += 0.0717f*(w-pink2);
    pink3 += 0.1836f*(w-pink3);
    float pink=(0.55f*pink1)+(0.30f*pink2)+(0.15f*pink3);
    float band=hissTilt.process(hissLPF.process(hissHPF.process(pink)));
    return fastSatRational(band*0.9f);
  }
  void flushDenormals(){
    hissHPF.flushDenormals(); hissLPF.flushDenormals(); hissTilt.flushDenormals();
    if(std::fabs(pink1)<1e-20f) pink1=0.0f;
    if(std::fabs(pink2)<1e-20f) pink2=0.0f;
    if(std::fabs(pink3)<1e-20f) pink3=0.0f;
  }
private:
  HYDRA_AUDIO_INLINE uint32_t fast_rand(){seed=seed*1664525u+1013904223u; return seed;}
  HYDRA_AUDIO_INLINE float white(){uint32_t r=fast_rand(); return ((float)(r&0xFFFF)*(1.0f/32768.0f))-1.f;}
  float pink1=0.0f,pink2=0.0f,pink3=0.0f;
  uint32_t seed=1;
  BiquadFilter hissHPF,hissLPF,hissTilt;
};

class DelayAllpass { public: ~DelayAllpass(){freeDelayBuffer(buffer);} void init(int len){freeDelayBuffer(buffer); size=len; buffer=allocateDelayBuffer(size); idx=0;} void clear(){if(buffer&&size>0) std::memset(buffer,0,sizeof(float)*size); idx=0;} void setCoeff(float f){feedback=f;} HYDRA_AUDIO_INLINE float process(float in){ if(!buffer) return in; float bo=buffer[idx]; float node=in+feedback*bo; if(std::fabs(node)<1e-15f) node=0; float out=bo-feedback*node; buffer[idx]=node; idx=(idx+1)%size; return out;} private: float* buffer=nullptr; int size=0,idx=0; float feedback=0.5f; };
}

struct TapeCore::Impl {
#if defined(HYDRA_TAPE_CORE_USE_PSRAM)
  static void* operator new(size_t size, const std::nothrow_t&) noexcept {
    void* p = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p;
  }
  static void operator delete(void* ptr) noexcept {
    if (ptr) heap_caps_free(ptr);
  }
  static void operator delete(void* ptr, const std::nothrow_t&) noexcept {
    if (ptr) heap_caps_free(ptr);
  }
#endif
  explicit Impl(float fs,float maxDelayMs,float historyMs):sampleRate(fs),noiseGen(fs,123456789u),noiseGenR(fs,987654321u){ magneticsL.lut=&tanhLUT; magneticsR.lut=&tanhLUT; updateSampleRateConstants(); dropout.setSampleRate(fs); magneticsL.updateParams(fs,currentParams); magneticsR.updateParams(fs,currentParams); normalBufferSize=(int32_t)(fs*(maxDelayMs/1000.f)); bufferSize=(int32_t)(fs*(historyMs/1000.f)); bufferSizeF=static_cast<float>(bufferSize); maxReadDelayF=static_cast<float>(normalBufferSize)-4.0f; bufferCapacity=bufferSize+TAPE_BUFFER_GUARD; delayLine=allocateDelayBuffer(bufferCapacity); delayLineR=allocateDelayBuffer(bufferCapacity); if(!delayLine||!delayLineR){freeDelayBuffer(delayLine);freeDelayBuffer(delayLineR);delayLine=delayLineR=nullptr;bufferSize=0;return;} static const float sc[6]={0.7f,0.65f,0.6f,0.6f,0.5f,0.5f}; static const int st[6]={223,367,491,647,821,1039}; for(int i=0;i<6;i++){springAP_L[i].init(st[i]);springAP_R[i].init(st[i]+23);springAP_L[i].setCoeff(sc[i]);springAP_R[i].setCoeff(sc[i]);springLPF_L[i].setLowpass(fs,2500,0.5f);springLPF_R[i].setLowpass(fs,2500,0.5f);} static const float rc[4]={0.6f,0.55f,0.5f,0.45f}; static const int rt[4]={151,313,569,797}; for(int i=0;i<4;i++){reverseAP_L[i].init(rt[i]);reverseAP_R[i].init(rt[i]+17);reverseAP_L[i].setCoeff(rc[i]);reverseAP_R[i].setCoeff(rc[i]);} updateFilters(); flutterLPF.setLowpass(fs,15,0.707f); flutterLPFR.setLowpass(fs,15,0.707f); cachedFlutterCutoff=-1.0f; warpHPF_L.setHighpass(fs,120.0f,0.707f); warpHPF_R.setHighpass(fs,120.0f,0.707f); warpLPF_L.setLowpass(fs,2200.0f,0.707f); warpLPF_R.setLowpass(fs,2200.0f,0.707f); smoothedGrainSamples=std::max(64.0f,80.0f*0.001f*fs); transportClock.prepare(fs); transportClock.setBpm(120.0f); tapTempo.prepare(fs); tapTempo.setAppliedBpm(120.0f); midiClockSync.prepare(fs); midiClockSync.setAppliedBpm(120.0f);}
  ~Impl(){freeDelayBuffer(delayLine);freeDelayBuffer(delayLineR);}
  bool valid() const {return delayLine&&delayLineR&&bufferSize>0;}
  HYDRA_DSP_IRAM HYDRA_AUDIO_INLINE float feedbackCompressor(float x){ return detail::continuousFeedbackSoftKnee(x); }
  HYDRA_AUDIO_INLINE float outputLimiter(float x){ if(x>0.9f){float e=x-0.9f;x=0.9f+e*0.1f;} else if(x<-0.9f){float e=x+0.9f;x=-0.9f+e*0.1f;} return clampf(x,-0.99f,0.99f);}
  HYDRA_AUDIO_INLINE float hermite4(float ym1,float y0,float y1,float y2,float f){ float c0=y0,c1=0.5f*(y1-ym1),c2=ym1-2.5f*y0+2.f*y1-0.5f*y2,c3=0.5f*(y2-ym1)+1.5f*(y0-y1); return ((c3*f+c2)*f+c1)*f+c0; }
  void mirrorDelayGuard(float* b){ if(!b||bufferSize<=0)return; for(int i=0;i<TAPE_BUFFER_GUARD;i++) b[bufferSize+i]=b[i]; }
  HYDRA_AUDIO_INLINE uint32_t nextLCG(uint32_t& seed){ seed=seed*1664525u+1013904223u; return seed; }
  HYDRA_AUDIO_INLINE float randSignedLCG(uint32_t& seed){ return (float((nextLCG(seed)>>8)&0x00FFFFFFu)*(1.0f/8388607.5f))-1.0f; }
  struct MechanicalState {
    float wowDrift=0,wowState=0;
    float flutterJitter=0,flutterHpState=0;
    uint32_t wowSeed=0x13579BDFu,flutterSeed=0x10293847u;
  };
  HYDRA_DSP_IRAM inline float mechanicalMod(float scrape,BiquadFilter& flutterFilter,MechanicalState& state,float residual){
    state.wowDrift=wowLeakCoeff*state.wowDrift+wowStepCoeff*randSignedLCG(state.wowSeed);
    state.wowDrift=clampf(state.wowDrift,-0.9f,0.9f);
    state.wowState += wowLpAlpha*(state.wowDrift-state.wowState);
    float wow=clampf(state.wowState-(0.16f*state.wowState*state.wowState*state.wowState),-1.0f,1.0f);

    float white=randSignedLCG(state.flutterSeed);
    state.flutterHpState += flutterHpAlpha*(white-state.flutterHpState);
    float hp=white-state.flutterHpState;
    state.flutterJitter += flutterLpAlpha*(hp-state.flutterJitter);
    float flutter=clampf(flutterFilter.process(state.flutterJitter),-1.0f,1.0f);

    const float textureBaseSamples = sampleRate * 0.012f;
    const float modBaseSamples = delayActive ? smoothedDelaySamples
                                             : std::max(smoothedDelaySamples, textureBaseSamples);
    float mod=(wowDepthScale*modBaseSamples)*wow+(flutterDepthScale*modBaseSamples)*flutter+(scrapeDepthScale*modBaseSamples)*scrape+residual;
    return clampf(mod,-0.25f*modBaseSamples,0.25f*modBaseSamples);
  }
  HYDRA_DSP_IRAM HYDRA_AUDIO_INLINE float readTapeAt(float d,const float* __restrict__ b){ d=clampf(d,2.0f,maxReadDelayF); return readHistory(d, b); }
  HYDRA_DSP_IRAM HYDRA_AUDIO_INLINE float readHistory(float d,const float* __restrict__ b){
    const uint32_t epoch = framesSinceDelayEnable;
    const bool checkEpoch = (epoch != 0xFFFFFFFFu);
    float epochF = 0.0f;
    if(checkEpoch){ epochF = static_cast<float>(epoch); if(d > epochF) return 0.0f; }
    float rp=static_cast<float>(writeHead)-d; if(rp<0.0f)rp+=bufferSizeF; int32_t r=static_cast<int32_t>(rp); float f=rp-static_cast<float>(r);
    int32_t prev=(r>0)?r-1:bufferSize-1;
    if(checkEpoch && d+1.0f > epochF) prev=r;
    return hermite4(b[prev],b[r],b[r+1],b[r+2],f);
  }
  HYDRA_DSP_IRAM HYDRA_AUDIO_INLINE float readTapeInt(float d,const float* __restrict__ b){ d=clampf(d,2,(float)normalBufferSize-4); if(d>static_cast<float>(framesSinceDelayEnable))return 0.0f; float rp=(float)writeHead-d; while(rp<0)rp+=bufferSize; while(rp>=(float)bufferSize)rp-=bufferSize; int32_t r=(int32_t)rp; return b[r]; }
  HYDRA_AUDIO_NOINLINE float readTapeReverse(float d,float* b){ if(!b||bufferSize<=0)return 0; d=clampf(d,2,(float)normalBufferSize-4); if(d>static_cast<float>(framesSinceDelayEnable))return 0.0f; int32_t di=(int32_t)d; if(std::abs(di-reverseWindowSize)>1000){reverseCounter=0;reverseWindowSize=di;} reverseCounter++; if(reverseCounter>=std::max<int32_t>(1,di))reverseCounter=0; float rp=(float)writeHead-d+(float)reverseCounter; int32_t r=((int32_t)rp%bufferSize+bufferSize)%bufferSize; float f=rp-std::floor(rp); int32_t i1=r,i0=(r>0)?r-1:bufferSize-1,i2=(r<bufferSize-1)?r+1:0,i3=(i2<bufferSize-1)?i2+1:0; float d1=b[i1],d0=b[i0],d2=b[i2],d3=b[i3]; float c0=d1,c1=0.5f*(d0-d2),c2=d2-2.5f*d1+2.f*d0-0.5f*d3,c3=0.5f*(d3-d1)+1.5f*(d1-d0); return ((c3*f+c2)*f+c1)*f+c0; }
  HYDRA_DSP_IRAM HYDRA_AUDIO_INLINE float readReverseWindow(
      const ReverseWindows::Frame& frame, float mod, const float* buffer) {
    const float limit = reverseWindows.modulationLimit();
    const float offset = clampf(mod * (currentParams.reverseMode == 1 ? 1.f : 0.25f),
                                -limit, limit);
    if (frame.incoming == 1.f) return readHistory(frame.delay + offset, buffer);
    const float old = readHistory(frame.previousDelay + offset, buffer);
    if (frame.incoming == 0.f) return old;
    return old + frame.incoming * (readHistory(frame.delay + offset, buffer) - old);
  }
  void resetReverse() {
    reverseWindows.reset();
    reverseBlend = 0.f;
    reverseSmearBlend = 0.f;
  }
  float requestedReverseMs() const {
    constexpr double beats[] = {1., 4., 2., .5, .25, 1.5, .75, 2./3., 1./3.};
    if (currentParams.reverseMode == 1) {
      return currentParams.delayTimeMs;
    }
    const int subdiv = std::clamp(currentParams.reverseDivision, 0, 8);
    const double bpm = std::max(1.0f, currentParams.bpm);
    return static_cast<float>((60.0 * beats[subdiv] / bpm) * 1000.0);
  }
  float maxHistoryMs() const {
    return (sampleRate > 0.f) ? (static_cast<float>(bufferSize) / sampleRate * 1000.f) : 0.f;
  }
  void updateSampleRateConstants(){ const float fs=std::max(1.0f,sampleRate); invSampleRate=1.0f/fs; reverseFadeStep=1.f/std::max(1.f,0.010f*fs); reverseControlAlpha=1.f-std::exp(-1.f/(0.015f*fs)); maxDropoutFc=0.45f*fs; delaySmoothAlpha=1.f-std::exp(-1.f/(0.200f*fs)); delayRampInc=1.f/(0.250f*fs); azimuthInc=0.2f*invSampleRate; mechNoiseSlowAlpha=mechNoiseOnePoleAlpha(fs,kMechNoiseSlowTimeSeconds); mechNoiseFastAlpha=mechNoiseOnePoleAlpha(fs,kMechNoiseFastTimeSeconds); duckAttackCoeff=1.f-std::exp(-1.f/(kHissDuckAttackTimeSeconds*fs)); duckReleaseCoeff=1.f-std::exp(-1.f/(kHissDuckReleaseTimeSeconds*fs)); }
  void updateCachedParams();
  void updateFilters();
  void updatePlaybackFilters();
  void flushDenormals();
  FastTanhLUT tanhLUT; TapeMagnetics magneticsL,magneticsR; float sampleRate; float invSampleRate=1.0f/48000.0f,maxDropoutFc=21600.0f,invHeadGainSum=1.0f; TapeParams currentParams{}; float flutterPhase=0,wowPhase=0,azimuthPhase=0; float flutterInc=0,wowInc=0,azimuthInc=0,delaySmoothAlpha=0,delayRampInc=0,mechNoiseSlowAlpha=0,mechNoiseFastAlpha=0;
  float signalPathBlend=0.f, studioWetCurrent=1.f;
  BiquadFilter studioBump[2], studioRolloff[2], studioGap[2];
  OnePoleLP studioDropout[2];
  AllpassFilter studioAzimuth[2];
  float driveGain=0,dryGain=0.5f,textureWetGain=0.5f,delayWetGain=0.5f,safeFeedback=0,feedbackWearAmount=0;
  MechanicalState mechStateL{};
  MechanicalState mechStateR{0,0,0,0,0x2468ACE1u,0x56473829u};
  float wowLeakCoeff=0.9996f,wowStepCoeff=0.00003f,wowLpAlpha=0.00008f;
  float flutterHpAlpha=0.0025f,flutterLpAlpha=0.015f;
  float wowDepthScale=0,flutterDepthScale=0,scrapeDepthScale=0;
  TapeMotionCoeffs motionCoeffs;
  TapeMotionState motionState;

#if defined(__GNUC__) || defined(__clang__)
  __attribute__((noinline))
#endif
  void computeUnifiedMotion(float scrapeL,
                            float scrapeR,
                            float& outModL,
                            float& outModR) {
    const float textureBaseSamples = sampleRate * 0.012f;
    const float modBaseSamples = delayActive ? smoothedDelaySamples
                                             : std::max(smoothedDelaySamples, textureBaseSamples);
    const float maxModOut = 0.25f * modBaseSamples;
    const int active = motionState.activeMode;

    motionState.stepControlSmoothing(motionCoeffs);

    if (!motionState.transitioning) {
      float rawL = 0.0f, rawR = 0.0f;
      if (active == 0) {
        const float residual = 0.03f * fast_sin(wowPhase) + 0.01f * fast_sin(flutterPhase + 0.31f);
        rawL = mechanicalMod(scrapeL, flutterLPF, mechStateL, residual);
        rawR = mechanicalMod(scrapeR, flutterLPFR, mechStateR, residual);
      } else {
        motionState.evalOrganicModeRaw(active, motionCoeffs, modBaseSamples,
                                       scrapeL, scrapeR, true, true, true,
                                       rawL, rawR);
      }
      motionState.lastRawOutL = rawL;
      motionState.lastRawOutR = rawR;
      motionState.applyTrajectoryControls(rawL, rawR, maxModOut, outModL, outModR);
      motionState.lastOutL = outModL;
      motionState.lastOutR = outModR;
      return;
    }

    // Clickless modulation-layer transition between prevMode and activeMode
    const int prev = motionState.prevMode;
    const bool needM0 = (prev == 0 || active == 0);
    const bool needM1 = (prev == 1 || active == 1 || prev == 3 || active == 3);
    const bool needM2 = (prev == 2 || active == 2 || prev == 3 || active == 3);
    const bool needM3 = (prev == 3 || active == 3);

    float m0L = 0.0f, m0R = 0.0f;
    if (needM0) {
      const float residual = 0.03f * fast_sin(wowPhase) + 0.01f * fast_sin(flutterPhase + 0.31f);
      m0L = mechanicalMod(scrapeL, flutterLPF, mechStateL, residual);
      m0R = mechanicalMod(scrapeR, flutterLPFR, mechStateR, residual);
    }
    if (needM1) motionState.stepM1(motionCoeffs);
    if (needM2) motionState.stepM2(motionCoeffs);
    if (needM3) motionState.stepM3(motionCoeffs, false);

    float rawPrevL = 0.0f, rawPrevR = 0.0f;
    if (prev == 0) {
      rawPrevL = m0L;
      rawPrevR = m0R;
    } else {
      motionState.evalOrganicModeRaw(prev, motionCoeffs, modBaseSamples,
                                     scrapeL, scrapeR, false, false, false,
                                     rawPrevL, rawPrevR);
    }

    float rawNextL = 0.0f, rawNextR = 0.0f;
    if (active == 0) {
      rawNextL = m0L;
      rawNextR = m0R;
    } else {
      motionState.evalOrganicModeRaw(active, motionCoeffs, modBaseSamples,
                                     scrapeL, scrapeR, false, false, false,
                                     rawNextL, rawNextR);
    }

    if (motionState.pendingSnapHold) {
      motionState.holdOffsetL = motionState.lastRawOutL - rawPrevL;
      motionState.holdOffsetR = motionState.lastRawOutR - rawPrevR;
      motionState.pendingSnapHold = false;
    }

    const float t = motionState.transitionPos;
    const float s = t * t * (3.0f - 2.0f * t);
    const float rawOutL = clampf((1.0f - s) * (rawPrevL + motionState.holdOffsetL) + s * rawNextL,
                                 -maxModOut, maxModOut);
    const float rawOutR = clampf((1.0f - s) * (rawPrevR + motionState.holdOffsetR) + s * rawNextR,
                                 -maxModOut, maxModOut);

    motionState.lastRawOutL = rawOutL;
    motionState.lastRawOutR = rawOutR;
    motionState.applyTrajectoryControls(rawOutL, rawOutR, maxModOut, outModL, outModR);

    motionState.transitionPos += motionCoeffs.transitionStep;
    if (motionState.transitionPos >= 1.0f) {
      motionState.transitionPos = 1.0f;
      motionState.transitioning = false;
      motionState.prevMode = motionState.activeMode;
      motionState.holdOffsetL = 0.0f;
      motionState.holdOffsetR = 0.0f;
    }

    motionState.lastOutL = outModL;
    motionState.lastOutR = outModR;
  }
  float inEnvL=0,inEnvR=0,duckAttackCoeff=0,duckReleaseCoeff=0,noiseBaseGain=0,noiseMinDuckGain=kHissNoiseMinDuckGain,noiseDuckStrength=10.0f;
  float tapeSpeedNorm=0.5f,tapeAgeNorm=0.4f,toneNorm=0.5f,azimuthErrorNorm=0;
  float feedbackPreEmph=1.12f,feedbackDeEmph=1.0f/1.12f;
  // Playback-filter coefficient transition state. Coeffs are derived from tape
  // speed, age, tone and head bump; changing them while the biquad state is
  // retained clicks, so post-start changes ramp per block (see updateFilters).
  float pbSpeed=0,pbAge=0,pbTone=0,pbBump=0;
  float pbSpeedTarget=0,pbAgeTarget=0,pbToneTarget=0,pbBumpTarget=0;
  bool pbPrimed=false,pbTransition=false,hasProcessed=false;
  bool filtersPrimed=false,springFiltersPrimed=false,cachedGuitarFocus=false;
  float cachedFbBumpFreq=-1.0f,cachedFbBumpGain=-1.0f,cachedFbGapCut=-1.0f,cachedFlutterCutoff=-1.0f,cachedSpringDampFreq=-1.0f;
  float musicalHeadDelay1=0,musicalHeadDelay2=0,musicalHeadDelay3=0,springFeedbackGain=0,springWetGain=0.5f;
  int springStages=3;
  uint8_t activeHeadMask=4;
  // Frames written since the delay was last enabled. Reads older than this
  // epoch are logically silent without erasing the 8 s PSRAM history. The
  // first activation starts from zero-initialized buffers and needs no mask.
  uint32_t framesSinceDelayEnable=0xFFFFFFFFu;
  bool delayEverEnabled=false;
  ReverseWindows reverseWindows;
  GlobalTransportClock transportClock;
  TapTempo tapTempo;
  MidiClockSync midiClockSync;
  TempoSource tempoSource = TempoSource::Internal;
  float manualBpm = 120.0f;
  double tapEstimatedBpm = 120.0;
  double midiEstimatedBpm = 120.0;
  float lastConfiguredReverseBpm = 120.0f;

  void applyEffectiveBpm(double bpm) {
    bpm = std::clamp(bpm, 30.0, 300.0);
    transportClock.setBpm(bpm);
    currentParams.bpm = static_cast<float>(bpm);

    // Reverse V2 stability: avoid micro-reconfigurations of windows unless difference is significant
    if (currentParams.reverse && currentParams.reverseMode == 0) {
      if (std::fabs(lastConfiguredReverseBpm - static_cast<float>(bpm)) >= 0.1f) {
        lastConfiguredReverseBpm = static_cast<float>(bpm);
        reverseWindows.configure(sampleRate, bufferSize, static_cast<float>(bpm),
                                 currentParams.reverseDivision, false, currentParams.delayTimeMs);
      }
    }
  }
  float reverseBlend=0.f, reverseSmearBlend=0.f, reverseFadeStep=0.f;
  float reverseFeedback=0.f, smoothFeedback=0.f;
  float smoothTexture=0.f, smoothWet=0.f, reverseControlAlpha=0.f;
  bool delayActive=false,headsMusical=false,pingPong=false,reverse=false,freeze=false,reverseSmear=false,spring=false,useAzimuth=false,warpActive=false;
  float warpPhase=0.0f, smoothedGrainSamples=3840.0f;
  float warpSpeedClamped=1.0f, warpTargetGrainSamples=3840.0f, warpStereoOffset=0.125f, warpFbGain=0.0f;
  BiquadFilter warpHPF_L,warpHPF_R,warpLPF_L,warpLPF_R;
  BiquadFilter flutterLPF,flutterLPFR; MechNoise mechNoiseL{22222u},mechNoiseR{33333u}; DropoutGenerator dropout; TapeNoiseGenerator noiseGen, noiseGenR; BiquadFilter reproHeadBump,tapeRolloff,gapLossLPF; OnePoleLP dropoutLPF,dropoutLPFR; AllpassFilter azimuthFilter; DCBlocker dcBlocker; BiquadFilter inputHPF,inputLPF; BiquadFilter reproHeadBumpR,tapeRolloffR,gapLossLPFR; AllpassFilter azimuthFilterR; DCBlocker dcBlockerR; BiquadFilter inputHPFR,inputLPFR; OnePoleLP feedbackGapLoss,feedbackGapLossR; BiquadFilter feedbackHPF,feedbackHPFR,feedbackHeadBump,feedbackHeadBumpR; AllpassFilter feedbackPhase,feedbackPhaseR;
#if defined(HYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING) && HYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING
  // Dedicated reproduction EQ memory: never advance monitor filters twice.
  BiquadFilter classicRolloffL,classicRolloffR,classicReproBumpL,classicReproBumpR,classicGapLossL,classicGapLossR;
  OnePoleLP classicDropoutL,classicDropoutR;
  AllpassFilter classicAzimuthL,classicAzimuthR;
#endif
  DelayAllpass springAP_L[6],springAP_R[6],reverseAP_L[4],reverseAP_R[4]; BiquadFilter springLPF_L[6],springLPF_R[6]; float springFB_L=0,springFB_R=0; float freezeFade=0; float delayEnableRamp=0,smoothedDelaySamples=0,smoothedAzCoeff=0; float* delayLine=nullptr; float* delayLineR=nullptr; int32_t normalBufferSize=0,bufferSize=0,bufferCapacity=0,writeHead=0,reverseCounter=0,reverseWindowSize=0; float bufferSizeF=0.0f,maxReadDelayF=2.0f;
};

HYDRA_DSP_IRAM void TapeCore::Impl::updateCachedParams(){
  driveGain=currentParams.drive*0.05f;
  textureWetGain=clampf(currentParams.dryWet*0.01f,0.f,1.f);
  delayWetGain=clampf(currentParams.delayWet*0.01f,0.f,1.f);
  dryGain=1.f-textureWetGain;
  safeFeedback=std::min(currentParams.feedback*0.01f,0.88f);
  reverseFeedback=0.83f*clampf(currentParams.feedback*0.01f,0.f,0.95f);
  if(reverseBlend==0.f){
    smoothFeedback=safeFeedback;
    smoothTexture=textureWetGain;
    smoothWet=delayWetGain;
  }
  feedbackWearAmount=currentParams.tapeAge*0.01f;
  tapeSpeedNorm=currentParams.tapeSpeed*0.01f;
  tapeAgeNorm=feedbackWearAmount;
  toneNorm=currentParams.tone*0.01f;
  azimuthErrorNorm=currentParams.azimuthError*0.01f;
  float beatMs=60000.f/std::max(1.f,currentParams.bpm);
  musicalHeadDelay1=beatMs*0.333f*sampleRate*0.001f;
  musicalHeadDelay2=beatMs*0.75f*sampleRate*0.001f;
  musicalHeadDelay3=beatMs*sampleRate*0.001f;
  float springDecayNorm=currentParams.springDecay*0.01f;
  springStages=3+static_cast<int>(springDecayNorm*3.f);
  springFeedbackGain=springDecayNorm*0.85f;
  springWetGain=clampf(currentParams.springMix*0.01f,0.f,1.f);
  activeHeadMask=static_cast<uint8_t>(currentParams.activeHeads)&0x07u;
  float hSum=0.0f;
  if(activeHeadMask&1) hSum+=1.0f;
  if(activeHeadMask&2) hSum+=0.75f;
  if(activeHeadMask&4) hSum+=0.55f;
  invHeadGainSum=(hSum>0.0f)?(1.0f/hSum):0.0f;
  delayActive=currentParams.delayActive;
  headsMusical=currentParams.headsMusical;
  pingPong=currentParams.pingPong;
  reverse=currentParams.reverse;
  freeze=currentParams.freeze;
  reverseSmear=currentParams.reverseSmear;
  spring=currentParams.spring;
  warpActive=currentParams.warpActive;
  useAzimuth=azimuthErrorNorm>0.0001f;
  warpSpeedClamped=clampf(currentParams.warpSpeed,0.0f,4.0f);
  warpTargetGrainSamples=std::max(64.0f,currentParams.warpGrainMs*0.001f*sampleRate);
  warpStereoOffset=clampf(currentParams.warpStereo*0.25f,0.0f,0.25f);
  warpFbGain=clampf(currentParams.warpFeedback*0.01f,0.0f,0.95f);
  noiseBaseGain=currentParams.noise*0.001f;
  noiseMinDuckGain=kHissNoiseMinDuckGain;
  noiseDuckStrength=10.0f;
}

HYDRA_DSP_IRAM void TapeCore::Impl::updateFilters(){
  updateCachedParams();
  dropout.setSampleRate(sampleRate);
  magneticsL.updateParams(sampleRate,currentParams); magneticsR.updateParams(sampleRate,currentParams);
  flutterInc = TWO_PI * currentParams.flutterRate / sampleRate;
  wowInc = TWO_PI * currentParams.wowRate / sampleRate;
  float srScale=48000.0f/std::max(1.0f,sampleRate);
  float wowRateNorm=clampf((currentParams.wowRate-0.1f)/(5.0f-0.1f),0.0f,1.0f);
  float flutterRateNorm=clampf((currentParams.flutterRate-0.1f)/(20.0f-0.1f),0.0f,1.0f);
  wowLeakCoeff=clampf(1.0f-((0.0004f-0.00035f*wowRateNorm)*srScale),0.0f,0.999999f);
  wowStepCoeff=(0.00003f+0.00018f*wowRateNorm)*srScale;
  wowLpAlpha=clampf((0.00008f+0.0012f*wowRateNorm)*srScale,0.0f,1.0f);
  flutterHpAlpha=clampf((0.0025f+0.02f*flutterRateNorm)*srScale,0.0f,1.0f);
  flutterLpAlpha=clampf((0.015f+0.11f*flutterRateNorm)*srScale,0.0f,1.0f);
  wowDepthScale=0.00025f*clampf(currentParams.wowDepth,0.0f,100.0f);
  flutterDepthScale=0.00010f*clampf(currentParams.flutterDepth,0.0f,100.0f);
  scrapeDepthScale=kScrapeModAmount*0.18f;
  {
    TapeMotionParams mp;
    mp.sampleRate = sampleRate;
    mp.wowRate = currentParams.wowRate;
    mp.wowDepth = currentParams.wowDepth;
    mp.flutterRate = currentParams.flutterRate;
    mp.flutterDepth = currentParams.flutterDepth;
    mp.tapeAge = currentParams.tapeAge;
    mp.scrapeAmount = scrapeDepthScale;
    mp.motionMode = currentParams.motionMode;
    mp.motionAmount = currentParams.motionAmount;
    mp.stereoMotion = currentParams.stereoMotion;
    mp.inertia = currentParams.motionInertia;
    mp.driftChar = currentParams.motionDriftChar;
    mp.capstanTexture = currentParams.motionTexture;
    mp.capstanIrregularity = currentParams.motionIrregularity;
    mp.wornInstability = currentParams.motionInstability;
    mp.tensionRecovery = currentParams.motionTensionRec;
    motionCoeffs.update(mp);
    if (!hasProcessed) {
      motionState.activeMode = std::max(0, std::min(3, currentParams.motionMode));
      motionState.prevMode = motionState.activeMode;
      motionState.transitioning = false;
      motionState.pendingSnapHold = false;
      motionState.transitionPos = 1.0f;
      motionState.holdOffsetL = 0.0f;
      motionState.holdOffsetR = 0.0f;
      motionState.smoothedAmount = motionCoeffs.amountNorm;
      motionState.smoothedStereo = motionCoeffs.stereoNorm;
    } else {
      motionState.setTargetMode(currentParams.motionMode);
    }
  }
  float speedMod=tapeSpeedNorm,ageMod=tapeAgeNorm,toneMod=toneNorm;
  feedbackPreEmph=1.0f+(0.06f+0.10f*(1.0f-speedMod)+0.06f*ageMod);
  feedbackDeEmph=1.0f/feedbackPreEmph;
  if(!filtersPrimed || cachedGuitarFocus != currentParams.guitarFocus){
    cachedGuitarFocus = currentParams.guitarFocus;
    if(currentParams.guitarFocus){
      inputHPF.setHighpass(sampleRate,150.f,0.707f);
      inputLPF.setLowpass(sampleRate,5000.f,0.707f);
    } else {
      inputHPF.setHighpass(sampleRate,20.f,0.707f);
      inputLPF.setLowpass(sampleRate,20000.f,0.707f);
    }
    inputHPFR.copyCoeffsFrom(inputHPF);
    inputLPFR.copyCoeffsFrom(inputLPF);
  }
  float bumpFreq=60.f+(speedMod*140.f);
  float reproBumpGain=clampf(currentParams.headBumpAmount*0.06f,0.f,6.f);
  float feedbackBumpGain=clampf(currentParams.headBumpAmount*0.02f,0.f,2.f);
  if(!filtersPrimed || cachedFbBumpFreq != bumpFreq || cachedFbBumpGain != feedbackBumpGain){
    cachedFbBumpFreq = bumpFreq;
    cachedFbBumpGain = feedbackBumpGain;
    feedbackHeadBump.setPeak(sampleRate,bumpFreq,0.8f,feedbackBumpGain);
    feedbackHeadBumpR.copyCoeffsFrom(feedbackHeadBump);
  }
  // The playback head bump, rolloff shelf and gap-loss lowpass are derived from
  // tape speed, age, tone and head bump. Re-deriving them in one step while the
  // biquad state is retained produced a one-sample discontinuity at the block
  // where the parameter was applied (0.0495 at 44.1 kHz, Phase 6.4). The first
  // configuration is applied at once so offline renders are unchanged; later
  // changes ramp per block in processStereoBlock.
  pbSpeedTarget=speedMod; pbAgeTarget=ageMod; pbToneTarget=toneMod; pbBumpTarget=reproBumpGain;
  if(!pbPrimed||!hasProcessed){
    pbSpeed=pbSpeedTarget; pbAge=pbAgeTarget; pbTone=pbToneTarget; pbBump=pbBumpTarget;
    pbPrimed=true; pbTransition=false; updatePlaybackFilters();
  } else if(pbSpeed!=pbSpeedTarget||pbAge!=pbAgeTarget||pbTone!=pbToneTarget||pbBump!=pbBumpTarget){
    pbTransition=true;
  }
  float feedbackWearAmount=ageMod;
  float fb=(9000+(speedMod*7000))*(1-(feedbackWearAmount*0.65f));
  fb=std::max(1600.f,fb);
  if(!filtersPrimed || cachedFbGapCut != fb){
    cachedFbGapCut = fb;
    feedbackGapLoss.setCutoff(sampleRate,fb);
    feedbackGapLossR.copyCoeffFrom(feedbackGapLoss);
  }
  if(!filtersPrimed){
    feedbackHPF.setHighpass(sampleRate,300,0.5f);
    feedbackHPFR.copyCoeffsFrom(feedbackHPF);
  }
  float ap=0.3f+feedbackWearAmount*0.4f;
  feedbackPhase.setCoeff(ap); feedbackPhaseR.setCoeff(ap);
  float flutterCutoff=clampf(currentParams.flutterRate*1.6f,4.f,12.f);
  if(!filtersPrimed || cachedFlutterCutoff != flutterCutoff){
    cachedFlutterCutoff = flutterCutoff;
    flutterLPF.setLowpass(sampleRate,flutterCutoff,0.707f);
    flutterLPFR.copyCoeffsFrom(flutterLPF);
  }
  float springDecayMod=currentParams.springDecay*0.01f,springDampMod=currentParams.springDamping*0.01f,springCoeff=0.4f+springDecayMod*0.45f,dampFreq=1500+springDampMod*3000;
  if(currentParams.spring){
    for(int i=0;i<6;i++){springAP_L[i].setCoeff(springCoeff);springAP_R[i].setCoeff(springCoeff);}
    if(!springFiltersPrimed || cachedSpringDampFreq != dampFreq){
      cachedSpringDampFreq = dampFreq;
      springFiltersPrimed = true;
      springLPF_L[0].setLowpass(sampleRate,dampFreq,0.5f);
      springLPF_R[0].copyCoeffsFrom(springLPF_L[0]);
      for(int i=1;i<6;i++){
        springLPF_L[i].copyCoeffsFrom(springLPF_L[0]);
        springLPF_R[i].copyCoeffsFrom(springLPF_L[0]);
      }
    }
  }
  filtersPrimed = true;
}

HYDRA_DSP_IRAM void TapeCore::Impl::updatePlaybackFilters(){
  const float bumpFreq=60.f+(pbSpeed*140.f);
  reproHeadBump.setPeak(sampleRate,bumpFreq,0.9f,pbBump);
  reproHeadBumpR.copyCoeffsFrom(reproHeadBump);
  float baseFreq=6000+(pbSpeed*10000),ageFactor=1-(pbAge*0.90f),toneFactor=(pbTone-0.5f)*2; if(toneFactor>0){ageFactor+=toneFactor*0.5f;if(ageFactor>1)ageFactor=1;} else ageFactor*=1+(toneFactor*0.5f); float cut=std::max(400.f,baseFreq*ageFactor);
  tapeRolloff.setHighShelf(sampleRate,cut*2.f,0.5f,-12.f);
  tapeRolloffR.copyCoeffsFrom(tapeRolloff);
  gapLossLPF.setLowpass(sampleRate,cut,0.707f);
  gapLossLPFR.copyCoeffsFrom(gapLossLPF);
  for(int c=0;c<2;++c){
    studioBump[c].copyCoeffsFrom(reproHeadBump);
    studioRolloff[c].copyCoeffsFrom(tapeRolloff);
    studioGap[c].copyCoeffsFrom(gapLossLPF);
  }
#if defined(HYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING) && HYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING
  // Same playback EQ coefficients and 20 ms coefficient interpolation.
  classicReproBumpL.copyCoeffsFrom(reproHeadBump);
  classicReproBumpR.copyCoeffsFrom(reproHeadBump);
  classicRolloffL.copyCoeffsFrom(tapeRolloff);
  classicRolloffR.copyCoeffsFrom(tapeRolloff);
  classicGapLossL.copyCoeffsFrom(gapLossLPF);
  classicGapLossR.copyCoeffsFrom(gapLossLPF);
#endif
}

TapeCore::TapeCore(float sampleRate,float maxDelayMs)
    : TapeCore(sampleRate,maxDelayMs,maxDelayMs) {}
TapeCore::TapeCore(float sampleRate,float maxDelayMs,float historyMs)
    : impl_(new(std::nothrow) Impl(sampleRate,maxDelayMs,std::max(maxDelayMs,historyMs))) {}
TapeCore::~TapeCore(){ delete impl_; }
bool TapeCore::isValid() const { return impl_ && impl_->valid(); }
const TapeParams& TapeCore::params() const { return impl_->currentParams; }
bool TapeCore::isReverseClamped() const { return impl_ ? impl_->reverseWindows.isClamped() : false; }
float TapeCore::effectiveReverseMs() const { return impl_ ? impl_->reverseWindows.effectiveMs() : 0.0f; }
float TapeCore::requestedReverseMs() const { return impl_ ? impl_->requestedReverseMs() : 0.0f; }
float TapeCore::maxHistoryMs() const { return impl_ ? impl_->maxHistoryMs() : 0.0f; }
GlobalTransportClock& TapeCore::transport() { return impl_->transportClock; }
const GlobalTransportClock& TapeCore::transport() const { return impl_->transportClock; }
TapTempo& TapeCore::tapTempo() { return impl_->tapTempo; }
const TapTempo& TapeCore::tapTempo() const { return impl_->tapTempo; }
MidiClockSync& TapeCore::midiClock() { return impl_->midiClockSync; }
const MidiClockSync& TapeCore::midiClock() const { return impl_->midiClockSync; }
TempoSource TapeCore::tempoSource() const { return impl_ ? impl_->tempoSource : TempoSource::Internal; }
float TapeCore::manualBpm() const { return impl_ ? impl_->manualBpm : 120.0f; }
void TapeCore::setManualBpm(float bpm) {
  if (!impl_) return;
  impl_->manualBpm = std::clamp(bpm, static_cast<float>(MidiClockSync::kMinBpm), static_cast<float>(MidiClockSync::kMaxBpm));
  if (impl_->tempoSource == TempoSource::Internal) {
    impl_->applyEffectiveBpm(impl_->manualBpm);
    impl_->tapTempo.setAppliedBpm(impl_->manualBpm);
    impl_->midiClockSync.setAppliedBpm(impl_->manualBpm);
  }
}
double TapeCore::effectiveBpm() const { return impl_ ? impl_->transportClock.bpm() : 120.0; }
double TapeCore::tapEstimatedBpm() const { return impl_ ? impl_->tapEstimatedBpm : 120.0; }
double TapeCore::midiEstimatedBpm() const { return impl_ ? impl_->midiEstimatedBpm : 120.0; }

int TapeCore::setTempoSource(TempoSource source) {
  if (!impl_) return -1;
  impl_->tempoSource = source;
  switch (source) {
    case TempoSource::Internal: {
      impl_->applyEffectiveBpm(impl_->manualBpm);
      impl_->tapTempo.setAppliedBpm(impl_->manualBpm);
      impl_->midiClockSync.setAppliedBpm(impl_->manualBpm);
      break;
    }
    case TempoSource::Tap: {
      const double tapBpm = (impl_->tapEstimatedBpm >= 30.0 && impl_->tapEstimatedBpm <= 300.0)
                                ? impl_->tapEstimatedBpm
                                : static_cast<double>(impl_->manualBpm);
      impl_->applyEffectiveBpm(tapBpm);
      impl_->midiClockSync.setAppliedBpm(tapBpm);
      break;
    }
    case TempoSource::MidiExternal: {
      if (impl_->midiClockSync.status() == MidiClockSyncStatus::Locked ||
          impl_->midiClockSync.status() == MidiClockSyncStatus::Holdover) {
        const double midiBpm = impl_->midiClockSync.appliedBpm();
        impl_->applyEffectiveBpm(midiBpm);
        impl_->tapTempo.setAppliedBpm(midiBpm);
      }
      break;
    }
  }
  return 0;
}

int TapeCore::tapTempoEvent(uint64_t timestamp_frames) {
  if (!impl_) return -1;
  const uint64_t currentSamplePos = impl_->transportClock.absoluteSamplePosition();
  const uint64_t ts = (timestamp_frames == kTapTempoUseCurrentTime || (timestamp_frames == 0 && currentSamplePos > 0))
                          ? currentSamplePos
                          : timestamp_frames;

  const uint64_t maxFutureFrames = static_cast<uint64_t>(impl_->sampleRate * 2.0);
  if (currentSamplePos > 0 && ts > currentSamplePos + maxFutureFrames) {
    return -1;
  }

  const TapResult result = impl_->tapTempo.recordTap(ts);
  if (result.accepted) {
    impl_->tapEstimatedBpm = result.newBpm;
  }

  if (impl_->tempoSource == TempoSource::Tap) {
    if (result.tempoUpdated) {
      impl_->applyEffectiveBpm(result.newBpm);
    }
  } else if (impl_->tempoSource == TempoSource::Internal) {
    if (result.tempoUpdated) {
      impl_->manualBpm = static_cast<float>(result.newBpm);
      impl_->applyEffectiveBpm(result.newBpm);
    }
  }
  // When in MidiExternal, taps are tracked in tapEstimatedBpm without assuming authority
  return result.accepted ? 0 : -1;
}

int TapeCore::tapTempoEventNow() {
  return tapTempoEvent(kTapTempoUseCurrentTime);
}

int TapeCore::tapTempoReset() {
  if (!impl_) return -1;
  impl_->tapTempo.reset();
  return 0;
}

int TapeCore::midiClockEvent(uint8_t statusByte, uint64_t timestamp_frames) {
  if (!impl_) return -1;
  const uint64_t currentSamplePos = impl_->transportClock.absoluteSamplePosition();
  const uint64_t ts = (timestamp_frames == kTapTempoUseCurrentTime || (timestamp_frames == 0 && currentSamplePos > 0))
                          ? currentSamplePos
                          : timestamp_frames;
  const double currentMusicalPos = impl_->transportClock.musicalPosition();
  const MidiClockEventResult result = impl_->midiClockSync.processEvent(statusByte, ts, currentMusicalPos);

  if (result.accepted) {
    impl_->midiEstimatedBpm = result.estimatedBpm;
  }

  if (impl_->tempoSource == TempoSource::MidiExternal) {
    if (result.transportAction == 1) { // Start (0xFA)
      impl_->transportClock.locate(0.0);
      impl_->transportClock.play();
    } else if (result.transportAction == 2) { // Continue (0xFB)
      impl_->transportClock.play();
    } else if (result.transportAction == 3) { // Stop (0xFC)
      impl_->transportClock.pause();
    } else if (result.transportAction == 4) { // Large Phase Recovery Realignment
      impl_->transportClock.locate(result.realignMusicalPos, impl_->transportClock.transportSamplePosition());
    }

    if (result.tempoUpdated) {
      impl_->applyEffectiveBpm(result.appliedBpm);
    }
  }
  return result.accepted ? 0 : -1;
}

int TapeCore::midiClockReset() {
  if (!impl_) return -1;
  impl_->midiClockSync.reset();
  return 0;
}

void TapeCore::reset(){
  if(!isValid()) return;
  // Explicit destructive reset. Not part of routine parameter automation: the
  // caller must schedule it outside the audio callback. The hot-path delay
  // re-activation no longer performs this wipe (see updateParams).
  std::memset(impl_->delayLine,0,sizeof(float)*impl_->bufferCapacity);
  std::memset(impl_->delayLineR,0,sizeof(float)*impl_->bufferCapacity);
  impl_->writeHead=0; impl_->reverseCounter=0; impl_->reverseWindowSize=0; impl_->resetReverse(); impl_->warpPhase=0.0f; impl_->framesSinceDelayEnable=0xFFFFFFFFu; impl_->delayEverEnabled=false; impl_->inEnvL=0; impl_->inEnvR=0;
  impl_->flutterPhase=impl_->wowPhase=impl_->azimuthPhase=0;
  impl_->mechStateL=Impl::MechanicalState{};
  impl_->mechStateR=Impl::MechanicalState{0,0,0,0,0x2468ACE1u,0x56473829u};
  impl_->motionState.activeMode = std::max(0, std::min(3, impl_->currentParams.motionMode));
  impl_->signalPathBlend = impl_->currentParams.signalPathMode == 1 ? 1.f : 0.f;
  impl_->studioWetCurrent = clampf(impl_->currentParams.studioWetLevel * .01f, 0.f, 2.f);
  for(int c=0;c<2;++c){ impl_->studioBump[c].reset(); impl_->studioRolloff[c].reset(); impl_->studioGap[c].reset(); impl_->studioDropout[c].reset(); impl_->studioAzimuth[c].reset(); }
  impl_->motionState.reset();
  // Explicit reset starts at the requested controls; do not record a spurious
  // stereo trajectory for 30 ms before the control slew reaches Stereo=0.
  impl_->motionState.smoothedAmount = impl_->motionCoeffs.amountNorm;
  impl_->motionState.smoothedStereo = impl_->motionCoeffs.stereoNorm;
  impl_->freezeFade=0; impl_->delayEnableRamp=0; impl_->smoothedDelaySamples=0; impl_->smoothedAzCoeff=0; impl_->springFB_L=impl_->springFB_R=0;
  impl_->dcBlocker.clear(); impl_->dcBlockerR.clear(); impl_->magneticsL.reset(); impl_->magneticsR.reset(); impl_->dropout.reset(); impl_->dropout.setSampleRate(impl_->sampleRate); impl_->noiseGen.reset(impl_->sampleRate, 123456789u); impl_->noiseGenR.reset(impl_->sampleRate, 987654321u); impl_->mechNoiseL.reset(); impl_->mechNoiseR.reset();
  impl_->flutterLPF.reset(); impl_->flutterLPFR.reset(); impl_->warpHPF_L.reset(); impl_->warpHPF_R.reset(); impl_->warpLPF_L.reset(); impl_->warpLPF_R.reset(); impl_->reproHeadBump.reset(); impl_->reproHeadBumpR.reset(); impl_->tapeRolloff.reset(); impl_->tapeRolloffR.reset(); impl_->gapLossLPF.reset(); impl_->gapLossLPFR.reset(); impl_->dropoutLPF.reset(); impl_->dropoutLPFR.reset();
  impl_->inputHPF.reset(); impl_->inputHPFR.reset(); impl_->inputLPF.reset(); impl_->inputLPFR.reset(); impl_->feedbackGapLoss.reset(); impl_->feedbackGapLossR.reset(); impl_->feedbackHeadBump.reset(); impl_->feedbackHeadBumpR.reset(); impl_->feedbackHPF.reset(); impl_->feedbackHPFR.reset();
  impl_->azimuthFilter.reset(); impl_->azimuthFilterR.reset(); impl_->feedbackPhase.reset(); impl_->feedbackPhaseR.reset();
#if defined(HYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING) && HYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING
  impl_->classicRolloffL.reset(); impl_->classicRolloffR.reset();
  impl_->classicReproBumpL.reset(); impl_->classicReproBumpR.reset();
  impl_->classicGapLossL.reset(); impl_->classicGapLossR.reset();
  impl_->classicDropoutL.reset(); impl_->classicDropoutR.reset();
  impl_->classicAzimuthL.reset(); impl_->classicAzimuthR.reset();
#endif
  for(int i=0;i<6;i++){ impl_->springAP_L[i].clear(); impl_->springAP_R[i].clear(); impl_->springLPF_L[i].reset(); impl_->springLPF_R[i].reset(); }
  for(int i=0;i<4;i++){ impl_->reverseAP_L[i].clear(); impl_->reverseAP_R[i].clear(); }
  impl_->tapTempo.reset();
  impl_->midiClockSync.reset();
  impl_->tempoSource = TempoSource::Internal;
  impl_->manualBpm = 120.0f;
  impl_->tapEstimatedBpm = 120.0;
  impl_->midiEstimatedBpm = 120.0;
  impl_->applyEffectiveBpm(impl_->manualBpm);
  // A destructive reset is a defined state boundary: snap the playback-filter
  // transition state instead of ramping from whatever was configured before.
  impl_->pbPrimed=false;
  impl_->updateFilters();
}

HYDRA_DSP_IRAM void TapeCore::updateParams(const TapeParams& newParams){
  if(!isValid()) return;
  if(!impl_->currentParams.delayActive && newParams.delayActive){
    impl_->delayEnableRamp=0; impl_->dcBlocker.clear(); impl_->dcBlockerR.clear(); impl_->magneticsL.reset(); impl_->magneticsR.reset();
    impl_->reproHeadBump.reset(); impl_->reproHeadBumpR.reset(); impl_->tapeRolloff.reset(); impl_->tapeRolloffR.reset();
    impl_->gapLossLPF.reset(); impl_->gapLossLPFR.reset(); impl_->dropoutLPF.reset(); impl_->dropoutLPFR.reset();
    impl_->azimuthFilter.reset(); impl_->azimuthFilterR.reset(); impl_->feedbackGapLoss.reset(); impl_->feedbackGapLossR.reset();
#if defined(HYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING) && HYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING
    impl_->classicRolloffL.reset(); impl_->classicRolloffR.reset();
    impl_->classicReproBumpL.reset(); impl_->classicReproBumpR.reset();
    impl_->classicGapLossL.reset(); impl_->classicGapLossR.reset();
    impl_->classicDropoutL.reset(); impl_->classicDropoutR.reset();
    impl_->classicAzimuthL.reset(); impl_->classicAzimuthR.reset();
#endif
    impl_->feedbackHeadBump.reset(); impl_->feedbackHeadBumpR.reset(); impl_->feedbackHPF.reset(); impl_->feedbackHPFR.reset();
    impl_->feedbackPhase.reset(); impl_->feedbackPhaseR.reset();
    impl_->warpHPF_L.reset(); impl_->warpHPF_R.reset(); impl_->warpLPF_L.reset(); impl_->warpLPF_R.reset();
    // The delay buffers are zero-initialized at allocation and are written on
    // every frame even while delay is inactive. Clearing ~3 MB of PSRAM here
    // blocked the audio callback for ~82 ms. Instead of erasing history, mark
    // an epoch: reads older than the activation instant stay logically silent
    // until they are naturally overwritten.
    impl_->framesSinceDelayEnable = impl_->delayEverEnabled ? 0u : 0xFFFFFFFFu;
    impl_->delayEverEnabled = true;
    impl_->reverseCounter=0; impl_->reverseWindowSize=0; impl_->resetReverse(); impl_->warpPhase=0.0f;
    impl_->smoothedDelaySamples = newParams.delayTimeMs * impl_->sampleRate * 0.001f;
  }
  if(!impl_->currentParams.warpActive && newParams.warpActive){
    impl_->warpPhase=0.0f;
    impl_->warpHPF_L.reset(); impl_->warpHPF_R.reset();
    impl_->warpLPF_L.reset(); impl_->warpLPF_R.reset();
  }
  const auto& previous = impl_->currentParams;
  if (newParams.bpm != impl_->manualBpm) {
    impl_->manualBpm = std::clamp(newParams.bpm, static_cast<float>(MidiClockSync::kMinBpm), static_cast<float>(MidiClockSync::kMaxBpm));
    if (impl_->tempoSource == TempoSource::Internal) {
      impl_->applyEffectiveBpm(impl_->manualBpm);
      impl_->tapTempo.setAppliedBpm(impl_->manualBpm);
      impl_->midiClockSync.setAppliedBpm(impl_->manualBpm);
    }
  }

  const float effectiveBpm = static_cast<float>(impl_->transportClock.bpm());
  if (newParams.reverse && newParams.reverseMode != 2 &&
      (!previous.reverse || previous.reverseMode != newParams.reverseMode ||
       (newParams.reverseMode == 1 && previous.delayTimeMs != newParams.delayTimeMs) ||
       (newParams.reverseMode == 0 && (std::fabs(impl_->lastConfiguredReverseBpm - effectiveBpm) >= 0.1f ||
        previous.reverseDivision != newParams.reverseDivision)))) {
    impl_->lastConfiguredReverseBpm = effectiveBpm;
    impl_->reverseWindows.configure(impl_->sampleRate, impl_->bufferSize, effectiveBpm,
        newParams.reverseDivision, newParams.reverseMode == 1, newParams.delayTimeMs);
  }
  impl_->currentParams=newParams;
  impl_->currentParams.bpm = effectiveBpm;
  impl_->dropout.setSeverity(clampf(newParams.dropoutSeverity * 0.01f, 0.0f, 1.0f));
  impl_->updateFilters();
}

HYDRA_DSP_IRAM void TapeCore::Impl::flushDenormals(){
  magneticsL.flushDenormals(); magneticsR.flushDenormals();
  noiseGen.flushDenormals(); noiseGenR.flushDenormals();
  flutterLPF.flushDenormals(); flutterLPFR.flushDenormals();
  warpHPF_L.flushDenormals(); warpHPF_R.flushDenormals();
  warpLPF_L.flushDenormals(); warpLPF_R.flushDenormals();
  for(int c=0;c<2;++c){ studioBump[c].flushDenormals(); studioRolloff[c].flushDenormals(); studioGap[c].flushDenormals(); studioDropout[c].flushDenormals(); studioAzimuth[c].flushDenormals(); }
  reproHeadBump.flushDenormals(); reproHeadBumpR.flushDenormals();
  tapeRolloff.flushDenormals(); tapeRolloffR.flushDenormals();
  gapLossLPF.flushDenormals(); gapLossLPFR.flushDenormals();
  dropoutLPF.flushDenormals(); dropoutLPFR.flushDenormals();
  azimuthFilter.flushDenormals(); azimuthFilterR.flushDenormals();
  dcBlocker.flushDenormals(); dcBlockerR.flushDenormals();
  inputHPF.flushDenormals(); inputHPFR.flushDenormals();
  inputLPF.flushDenormals(); inputLPFR.flushDenormals();
  feedbackGapLoss.flushDenormals(); feedbackGapLossR.flushDenormals();
  feedbackHPF.flushDenormals(); feedbackHPFR.flushDenormals();
  feedbackHeadBump.flushDenormals(); feedbackHeadBumpR.flushDenormals();
  feedbackPhase.flushDenormals(); feedbackPhaseR.flushDenormals();
#if defined(HYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING) && HYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING
  classicRolloffL.flushDenormals(); classicRolloffR.flushDenormals();
  classicReproBumpL.flushDenormals(); classicReproBumpR.flushDenormals();
  classicGapLossL.flushDenormals(); classicGapLossR.flushDenormals();
  classicDropoutL.flushDenormals(); classicDropoutR.flushDenormals();
  classicAzimuthL.flushDenormals(); classicAzimuthR.flushDenormals();
#endif
  if(spring){
    for(int i=0;i<6;++i){ springLPF_L[i].flushDenormals(); springLPF_R[i].flushDenormals(); }
    if(std::fabs(springFB_L)<1e-20f) springFB_L=0.0f;
    if(std::fabs(springFB_R)<1e-20f) springFB_R=0.0f;
  }
  if(std::fabs(inEnvL)<1e-20f) inEnvL=0.0f;
  if(std::fabs(inEnvR)<1e-20f) inEnvR=0.0f;
}

float TapeCore::process(float input){ float oL,oR; processStereo(input,input,&oL,&oR); return oL; }

HYDRA_DSP_IRAM void TapeCore::processStereo(float inL,float inR,float* outL,float* outR){
  processStereoBlock(&inL,&inR,outL,outR,1);
}

HYDRA_DSP_IRAM void TapeCore::processStereoBlock(const float* inLBlock,const float* inRBlock,float* outLBlock,float* outRBlock,uint32_t frames){
  if(frames==0||!inLBlock||!inRBlock||!outLBlock||!outRBlock) return;
  if(!isValid()){
    for(uint32_t i=0;i<frames;++i){ outLBlock[i]=inLBlock[i]; outRBlock[i]=inRBlock[i]; }
    return;
  }
  TapeParams* p=&impl_->currentParams;
  if(impl_->pbTransition){
    const float alpha=1.f-std::exp(-static_cast<float>(frames)/(0.020f*std::max(1.0f,impl_->sampleRate)));
    impl_->pbSpeed+=alpha*(impl_->pbSpeedTarget-impl_->pbSpeed);
    impl_->pbAge+=alpha*(impl_->pbAgeTarget-impl_->pbAge);
    impl_->pbTone+=alpha*(impl_->pbToneTarget-impl_->pbTone);
    impl_->pbBump+=alpha*(impl_->pbBumpTarget-impl_->pbBump);
    if(std::fabs(impl_->pbSpeedTarget-impl_->pbSpeed)<1e-4f&&std::fabs(impl_->pbAgeTarget-impl_->pbAge)<1e-4f&&
       std::fabs(impl_->pbToneTarget-impl_->pbTone)<1e-4f&&std::fabs(impl_->pbBumpTarget-impl_->pbBump)<1e-4f){
      impl_->pbSpeed=impl_->pbSpeedTarget; impl_->pbAge=impl_->pbAgeTarget;
      impl_->pbTone=impl_->pbToneTarget; impl_->pbBump=impl_->pbBumpTarget;
      impl_->pbTransition=false;
    }
    impl_->updatePlaybackFilters();
  }
  impl_->hasProcessed=true;
  // Reverse V2 boundary detection used to evaluate double-precision musical
  // positions twice per sample. The Xtensa LX7 has no double FPU, so that path
  // dominated Reverse cost. Boundaries are block-invariant: collect them once
  // per block with the existing zero-allocation API and consume the offsets.
  // 8 events cannot overflow at the supported block sizes and subdivisions.
  constexpr uint32_t kMaxBlockBoundaries = 8;
  BoundaryEvent blockBoundaries[kMaxBlockBoundaries];
  uint32_t blockBoundaryCount = 0;
  const bool reverseClockSync = (p->reverseMode == 0) &&
      impl_->transportClock.isPlaying() && impl_->delayActive &&
      !impl_->warpActive && impl_->reverse;
  if (reverseClockSync) {
    const auto boundarySubdiv = GlobalTransportClock::fromReverseDivision(p->reverseDivision);
    blockBoundaryCount = impl_->transportClock.getSubdivisionBoundariesInBlock(
        boundarySubdiv, frames, blockBoundaries, kMaxBlockBoundaries);
  }
  uint32_t boundaryCursor = 0;
  const float targetDelay = p->delayTimeMs * impl_->sampleRate * 0.001f;
  const float dropoutBaseCutoff = 6000.f + p->tapeSpeed * 100.f;
  for(uint32_t frameIdx=0;frameIdx<frames;++frameIdx){
    const float inL=inLBlock[frameIdx];
    const float inR=inRBlock[frameIdx];
    float sampleOutL=0.0f, sampleOutR=0.0f;
    float* const outL=&sampleOutL;
    float* const outR=&sampleOutR;
  HYDRA_PROFILE_START(tMod);
  impl_->flutterPhase += impl_->flutterInc; if(impl_->flutterPhase>TWO_PI) impl_->flutterPhase-=TWO_PI;
  impl_->wowPhase += impl_->wowInc; if(impl_->wowPhase>TWO_PI) impl_->wowPhase-=TWO_PI;

  impl_->smoothedDelaySamples += impl_->delaySmoothAlpha*(targetDelay-impl_->smoothedDelaySamples);

  float scrapeL=impl_->mechNoiseL.lowRate(impl_->mechNoiseSlowAlpha,impl_->mechNoiseFastAlpha);
  float scrapeR=impl_->mechNoiseR.lowRate(impl_->mechNoiseSlowAlpha,impl_->mechNoiseFastAlpha);
  float modL = 0.0f, modR = 0.0f;
  impl_->computeUnifiedMotion(scrapeL, scrapeR, modL, modR);

  const bool useAzimuth=impl_->useAzimuth;
  if(useAzimuth){
    impl_->azimuthPhase += impl_->azimuthInc; if(impl_->azimuthPhase>1) impl_->azimuthPhase=0;
    float tri=(impl_->azimuthPhase<0.5f)?(impl_->azimuthPhase*2):(2-impl_->azimuthPhase*2); float azimuthMod=0.5f+(tri*1.5f);
    float targetAz=-0.90f*impl_->azimuthErrorNorm*azimuthMod;
    impl_->smoothedAzCoeff += 0.001f * (targetAz - impl_->smoothedAzCoeff);
    impl_->azimuthFilter.setCoeff(impl_->smoothedAzCoeff);
    impl_->azimuthFilterR.setCoeff(impl_->smoothedAzCoeff);
#if defined(HYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING) && HYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING
    impl_->classicAzimuthL.setCoeff(impl_->smoothedAzCoeff);
    impl_->classicAzimuthR.setCoeff(impl_->smoothedAzCoeff);
#endif
  }

  impl_->dropout.beginFrame(impl_->sampleRate);
  DropoutFrame dropoutL=impl_->dropout.value(0,impl_->sampleRate);
  DropoutFrame dropoutR=impl_->dropout.value(1,impl_->sampleRate);
  float dropoutAvg=0.5f*(dropoutL.ampGain+dropoutR.ampGain);
  float absInL=std::fabs(inL), absInR=std::fabs(inR);
  float aL=(absInL>impl_->inEnvL)?impl_->duckAttackCoeff:impl_->duckReleaseCoeff;
  float aR=(absInR>impl_->inEnvR)?impl_->duckAttackCoeff:impl_->duckReleaseCoeff;
  impl_->inEnvL += aL*(absInL-impl_->inEnvL);
  impl_->inEnvR += aR*(absInR-impl_->inEnvR);

  float hissL=0, hissR=0;
  if(impl_->noiseBaseGain>0.000001f) {
    float duckL=clampf(1.0f-(impl_->noiseDuckStrength*impl_->inEnvL),impl_->noiseMinDuckGain,1.0f);
    float duckR=clampf(1.0f-(impl_->noiseDuckStrength*impl_->inEnvR),impl_->noiseMinDuckGain,1.0f);
    float dropoutLift=clampf((1.0f-dropoutAvg)*0.65f,0.0f,0.65f);
    float noiseMult=clampf(impl_->noiseBaseGain*(1.0f+dropoutLift),0.0f,impl_->noiseBaseGain*1.8f);
    float hissGainL=noiseMult*(1.0f+0.10f*clampf(scrapeL,-1.0f,1.0f));
    float hissGainR=noiseMult*(1.0f+0.10f*clampf(scrapeR,-1.0f,1.0f));
    hissGainL=clampf(hissGainL,0.0f,impl_->noiseBaseGain*2.0f);
    hissGainR=clampf(hissGainR,0.0f,impl_->noiseBaseGain*2.0f);
    hissL=impl_->noiseGen.next()*hissGainL*duckL;
    hissR=impl_->noiseGenR.next()*hissGainR*duckR;
  }
  if(impl_->delayActive){impl_->delayEnableRamp+=impl_->delayRampInc;if(impl_->delayEnableRamp>1)impl_->delayEnableRamp=1;} else impl_->delayEnableRamp=0;
  HYDRA_PROFILE_END(ModulationNoise, tMod);

  HYDRA_PROFILE_START(tWarp);
  float warpSigL=0.0f, warpSigR=0.0f;
  float warpFbL=0.0f, warpFbR=0.0f;
  if(impl_->delayActive && impl_->warpActive){
    const float speedMult = impl_->warpSpeedClamped;
    const float targetGrainSamples = impl_->warpTargetGrainSamples;
    impl_->smoothedGrainSamples += 0.005f * (targetGrainSamples - impl_->smoothedGrainSamples);
    const float grainSamples = impl_->smoothedGrainSamples;
    const float invGrainSamples = fastRecip(grainSamples);

    impl_->warpPhase += speedMult * invGrainSamples;
    if(impl_->warpPhase >= 1.0f) impl_->warpPhase -= 1.0f;
    if(impl_->warpPhase < 0.0f) impl_->warpPhase += 1.0f;

    float phaseL1 = impl_->warpPhase;
    float phaseL2 = phaseL1 + 0.5f;
    if(phaseL2 >= 1.0f) phaseL2 -= 1.0f;

    const float stereoOffset = impl_->warpStereoOffset;
    float phaseR1 = phaseL1 + stereoOffset;
    if(phaseR1 >= 1.0f) phaseR1 -= 1.0f;
    float phaseR2 = phaseR1 + 0.5f;
    if(phaseR2 >= 1.0f) phaseR2 -= 1.0f;

    const float baseDelay = 4.0f;
    float rawL = 0.0f, rawR = 0.0f;

    const int mode = p->warpMode;
    if(mode == 1 || mode == 2){
      // 1-Tap Bug Cru (Mode 1: Hermite, Mode 2: Raw Int Cast)
      const float dSize = grainSamples + 1000.0f;
      float relD_L = (1.0f - phaseL1) * dSize;
      float relD_R = (1.0f - phaseR1) * dSize;

      const float normL = 1.0f - phaseL1;
      const float normR = 1.0f - phaseR1;
      float envL = 1.0f, envR = 1.0f;
      constexpr float declick = 0.05f;
      constexpr float invDeclick = 1.0f / declick;
      if (normL < declick) envL = normL * invDeclick;
      else if (normL > (1.0f - declick)) envL = (1.0f - normL) * invDeclick;
      if (normR < declick) envR = normR * invDeclick;
      else if (normR > (1.0f - declick)) envR = (1.0f - normR) * invDeclick;

      if(mode == 2){
        relD_L = static_cast<float>(static_cast<int32_t>(relD_L));
        relD_R = static_cast<float>(static_cast<int32_t>(relD_R));
        rawL = impl_->readTapeInt(baseDelay + relD_L, impl_->delayLine) * envL;
        rawR = impl_->readTapeInt(baseDelay + relD_R, impl_->delayLineR) * envR;
      } else {
        rawL = impl_->readTapeAt(baseDelay + relD_L, impl_->delayLine) * envL;
        rawR = impl_->readTapeAt(baseDelay + relD_R, impl_->delayLineR) * envR;
      }
    } else {
      // 2-Taps Musical (Mode 0: Hermite, Mode 3: Raw Int Cast)
      const float gainL1 = 1.0f - std::fabs(2.0f * phaseL1 - 1.0f);
      const float gainL2 = 1.0f - std::fabs(2.0f * phaseL2 - 1.0f);
      const float gainR1 = 1.0f - std::fabs(2.0f * phaseR1 - 1.0f);
      const float gainR2 = 1.0f - std::fabs(2.0f * phaseR2 - 1.0f);

      float relD1_L = (1.0f - phaseL1) * grainSamples;
      float relD2_L = (1.0f - phaseL2) * grainSamples;
      float relD1_R = (1.0f - phaseR1) * grainSamples;
      float relD2_R = (1.0f - phaseR2) * grainSamples;

      if(mode == 3){
        relD1_L = static_cast<float>(static_cast<int32_t>(relD1_L));
        relD2_L = static_cast<float>(static_cast<int32_t>(relD2_L));
        relD1_R = static_cast<float>(static_cast<int32_t>(relD1_R));
        relD2_R = static_cast<float>(static_cast<int32_t>(relD2_R));
        rawL = impl_->readTapeInt(baseDelay + relD1_L, impl_->delayLine) * gainL1 +
               impl_->readTapeInt(baseDelay + relD2_L, impl_->delayLine) * gainL2;
        rawR = impl_->readTapeInt(baseDelay + relD1_R, impl_->delayLineR) * gainR1 +
               impl_->readTapeInt(baseDelay + relD2_R, impl_->delayLineR) * gainR2;
      } else {
        rawL = impl_->readTapeAt(baseDelay + relD1_L, impl_->delayLine) * gainL1 +
               impl_->readTapeAt(baseDelay + relD2_L, impl_->delayLine) * gainL2;
        rawR = impl_->readTapeAt(baseDelay + relD1_R, impl_->delayLineR) * gainR1 +
               impl_->readTapeAt(baseDelay + relD2_R, impl_->delayLineR) * gainR2;
      }
    }

    // Direct output retains pristine high-frequency brilliance
    warpSigL = rawL;
    warpSigR = rawR;

    // Feedback path: HPF (120 Hz) -> LPF (2200 Hz) -> soft saturation (tanh / rational)
    float procL = impl_->warpLPF_L.process(impl_->warpHPF_L.process(rawL));
    float procR = impl_->warpLPF_R.process(impl_->warpHPF_R.process(rawR));
    warpFbL = fastSatRational(procL * 1.6f) * (1.0f / 1.6f);
    warpFbR = fastSatRational(procR * 1.6f) * (1.0f / 1.6f);
  }

  HYDRA_PROFILE_END(Warp, tWarp);
  HYDRA_PROFILE_START(tRevCtrl);
  const bool legacyReverse = impl_->reverse && p->reverseMode == 2;
  const bool wantsReverse = impl_->delayActive && !impl_->warpActive &&
                            impl_->reverse && !legacyReverse;
  float& reverseBlend = impl_->reverseBlend;
  if (wantsReverse) {
    if (reverseBlend == 0.f && p->reverseMode == 0) {
      const auto subdiv = GlobalTransportClock::fromReverseDivision(p->reverseDivision);
      impl_->reverseWindows.syncPhase(impl_->transportClock.subdivisionPhase(subdiv), impl_->bufferSize);
    }
    reverseBlend = std::min(1.f, reverseBlend + impl_->reverseFadeStep);
  } else if (reverseBlend > 0.f) {
    reverseBlend = std::max(0.f, reverseBlend - impl_->reverseFadeStep);
    if (reverseBlend == 0.f) impl_->reverseWindows.reset();
  }
  ReverseWindows::Frame reverseFrame{};
  float textureGain = impl_->textureWetGain;
  float delayGain = impl_->delayWetGain;
  float dryGain = impl_->dryGain;
  float feedbackGain = impl_->safeFeedback;
  if (reverseBlend > 0.f) {
    const bool syncToClock = reverseClockSync;
    bool isBoundary = false;
    if (syncToClock) {
      if (blockBoundaryCount >= kMaxBlockBoundaries) {
        // Defensive fallback: never miss a boundary if the fixed event buffer
        // could have been truncated (not expected at supported block sizes).
        const auto subdiv = GlobalTransportClock::fromReverseDivision(p->reverseDivision);
        isBoundary = impl_->transportClock.isSubdivisionBoundary(subdiv);
      } else if (boundaryCursor < blockBoundaryCount &&
                 blockBoundaries[boundaryCursor].frameOffset == frameIdx) {
        isBoundary = true;
        ++boundaryCursor;
      }
    }
    reverseFrame = impl_->reverseWindows.next(isBoundary, syncToClock);
    const float a = impl_->reverseControlAlpha;
    impl_->smoothTexture += a * (textureGain - impl_->smoothTexture);
    impl_->smoothWet += a * (delayGain - impl_->smoothWet);
    const float target = feedbackGain + reverseBlend * (impl_->reverseFeedback - feedbackGain);
    impl_->smoothFeedback += a * (target - impl_->smoothFeedback);
    textureGain = impl_->smoothTexture;
    delayGain = impl_->smoothWet;
    dryGain = 1.f - textureGain;
    feedbackGain = impl_->smoothFeedback;
  } else {
    impl_->smoothTexture = textureGain;
    impl_->smoothWet = delayGain;
    impl_->smoothFeedback = feedbackGain;
  }
  HYDRA_PROFILE_END(ReverseControl, tRevCtrl);

  struct ChannelFrame {
    float conditionedInput;
    float tapeOutput;
    float studioOutput;
    float feedbackSource;
  };

  int studioChannel = 0;
  auto readCh=[&](float input,float noiseInjected,float* buffer,BiquadFilter& reproHeadBumpFilter,BiquadFilter& tr,BiquadFilter& outputGapLossFilter,AllpassFilter& az,BiquadFilter&iHP,BiquadFilter&iLP,OnePoleLP&dropoutFilter,DropoutFrame drop,float mod,float warpSig,float warpFb) HYDRA_LAMBDA_INLINE HYDRA_DSP_IRAM {
    ChannelFrame frame{};
    // --- input conditioning ---
    HYDRA_PROFILE_START(tRecF);
    frame.conditionedInput=iLP.process(iHP.process(input));
    HYDRA_PROFILE_END(RecordFilters, tRecF);
    // --- delay read ---
    HYDRA_PROFILE_START(tConvR);
    float textureSig=(textureGain>0.0f && !impl_->warpActive)?impl_->readTapeAt(200.f+mod,buffer):0.0f;
    float delaySig=0;
    float reverseSig=0;
    if(impl_->delayActive){
      if(impl_->warpActive){
        delaySig = warpSig;
      } else if(legacyReverse){
        float d3 = (impl_->headsMusical ? impl_->musicalHeadDelay3 : impl_->smoothedDelaySamples) + mod;
        delaySig = impl_->readTapeReverse(d3,buffer);
      } else if(reverseBlend < 1.f){
        const uint8_t mask = impl_->activeHeadMask;
        const float base = impl_->smoothedDelaySamples;
        if(mask & 1){
          float d1 = (impl_->headsMusical ? impl_->musicalHeadDelay1 : (base * 0.33f)) + mod * 0.33f;
          delaySig += impl_->readTapeAt(d1,buffer) * 1.0f;
        }
        if(mask & 2){
          float d2 = (impl_->headsMusical ? impl_->musicalHeadDelay2 : (base * 0.66f)) + mod * 0.66f;
          delaySig += impl_->readTapeAt(d2,buffer) * 0.75f;
        }
        if(mask & 4){
          float d3 = (impl_->headsMusical ? impl_->musicalHeadDelay3 : base) + mod;
          delaySig += impl_->readTapeAt(d3,buffer) * 0.55f;
        }
        delaySig *= impl_->invHeadGainSum;
      }
    }
    HYDRA_PROFILE_END(ConventionalReads, tConvR);
    HYDRA_PROFILE_START(tRevR);
    if (reverseBlend > 0.f && !impl_->warpActive && impl_->delayActive) {
      reverseSig = impl_->readReverseWindow(reverseFrame, mod, buffer);
      delaySig += reverseBlend * (reverseSig - delaySig);
    }
    HYDRA_PROFILE_END(ReverseReads, tRevR);
    HYDRA_PROFILE_START(tPlayF);
    float tapeSig = (textureSig * textureGain) + (delaySig * delayGain);
    // --- dropout/gap loss ---
    if(drop.hfLoss<0.9995f||drop.ampGain<0.9995f){
      float dynamicCutoff=dropoutBaseCutoff*(0.35f+0.65f*drop.hfLoss);
      dropoutFilter.setCutoffFastInv(impl_->maxDropoutFc,impl_->invSampleRate,dynamicCutoff);
      tapeSig=dropoutFilter.process(tapeSig);
      tapeSig *= drop.ampGain;
    }
    if(useAzimuth) tapeSig=az.process(tapeSig);
    tapeSig += noiseInjected;
    // --- playback head EQ & routing ---
    if (impl_->warpActive) {
      frame.feedbackSource = warpFb;
    } else {
#if defined(HYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING) && HYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING
      // Dedicated coloration occurs after readCh; Reverse already blended.
      frame.feedbackSource = delaySig;
#elif defined(HYDRA_PHASE72_EXPERIMENTAL_FEEDBACK_ROUTING) && HYDRA_PHASE72_EXPERIMENTAL_FEEDBACK_ROUTING
      // Phase 7.2A experiment ONLY. The forward/reverse blend is already
      // applied to delaySig. Avoid a second reverse blend and keep monitoring
      // gains, injected hiss and playback-only texture outside the repeat loop.
      frame.feedbackSource = experimental::independentTapeFeedbackSource(delaySig);
#else
      // Approved Phase 7.1 / Phase 6.6 sound: preserve this expression
      // byte-for-byte when the experiment is not explicitly compiled in.
      frame.feedbackSource = tapeSig + reverseBlend * (reverseSig - tapeSig);
#endif
    }
    frame.tapeOutput=outputGapLossFilter.process(reproHeadBumpFilter.process(tr.process(tapeSig)));
    // Independent playback memory, same reads and transport. Never feeds record.
    const int c=studioChannel++;
    float cleanWet=delaySig*delayGain;
    if(drop.hfLoss<0.9995f||drop.ampGain<0.9995f){
      impl_->studioDropout[c].setCutoffFastInv(impl_->maxDropoutFc,impl_->invSampleRate,dropoutBaseCutoff*(0.35f+0.65f*drop.hfLoss));
      cleanWet=impl_->studioDropout[c].process(cleanWet)*drop.ampGain;
    }
    if(useAzimuth){ impl_->studioAzimuth[c].setCoeff(impl_->smoothedAzCoeff); cleanWet=impl_->studioAzimuth[c].process(cleanWet); }
    cleanWet += noiseInjected;
    frame.studioOutput=impl_->studioGap[c].process(impl_->studioBump[c].process(impl_->studioRolloff[c].process(cleanWet)));
    HYDRA_PROFILE_END(PlaybackFilters, tPlayF);
    return frame;
  };

  auto feedbackFrom=[&](float feedbackInput,BiquadFilter&fbHPF,BiquadFilter&feedbackHeadBumpFilter,OnePoleLP&feedbackGapLossFilter,AllpassFilter&feedbackPhaseFilter,float inputEnv) HYDRA_LAMBDA_INLINE HYDRA_DSP_IRAM {
    HYDRA_PROFILE_START(tFb);
    float feedSig=0; if(impl_->delayActive){
      if(impl_->warpActive){
        const float fbGain = impl_->warpFbGain;
        feedSig = feedbackInput * fbGain;
        feedSig = impl_->feedbackCompressor(feedSig);
        feedSig *= impl_->delayEnableRamp;
      } else {
        feedSig=fbHPF.process(feedbackInput);
        float feedbackDrive=1.0f+(feedbackGain*0.30f*(1.f-reverseBlend))+clampf(inputEnv*0.18f,0.f,0.22f)+(impl_->feedbackWearAmount*0.10f);
        feedSig*=feedbackDrive;
        feedSig=feedbackHeadBumpFilter.process(feedSig);
        feedSig=feedbackGapLossFilter.process(feedSig);
        feedSig=feedbackPhaseFilter.process(feedSig);

        // subtle pre-emphasis before staged saturation
        float satIn=feedSig*impl_->feedbackPreEmph;

        // two light saturation stages with trim between them (less harsh than single heavy drive)
        satIn=fastSatRational(satIn*1.10f);
        satIn *= 0.82f;
        satIn=fastSatRational(satIn*1.08f);

        // complementary de-emphasis via cached reciprocal to avoid per-sample division
        feedSig=satIn*impl_->feedbackDeEmph;

        feedSig *= feedbackGain;
        feedSig=impl_->feedbackCompressor(feedSig);
        feedSig *= impl_->delayEnableRamp;
      }
    }
    HYDRA_PROFILE_END(Feedback, tFb);
    return feedSig;
  };

  auto writeCh=[&](float input,float dryTapeOutput,const ChannelFrame& frame,float feedbackInput,float* buffer,DCBlocker& dc,TapeMagnetics& mag) HYDRA_LAMBDA_INLINE HYDRA_DSP_IRAM {
    // --- record amplifier ---
    HYDRA_PROFILE_START(tAmp);
    const float freshInput = frame.conditionedInput * impl_->driveGain;
    float recSig=dc.process(freshInput + feedbackInput); recSig=clampf(recSig,-4,4);
    HYDRA_PROFILE_END(RecordAmplifier, tAmp);
    // A Warp repeat must decay without repeatedly opening the record bias gate.
    const bool allowBias = (!impl_->warpActive && reverseBlend == 0.f) ||
                           std::fabs(freshInput) > 1e-7f;
    // --- magnetic saturation/write ---
    HYDRA_PROFILE_START(tMagWrite);
    if(!impl_->freeze) buffer[impl_->writeHead]=mag.process(recSig, allowBias);
    HYDRA_PROFILE_END(MagneticWrite, tMagWrite);
    HYDRA_PROFILE_START(tLim);
    const float limited = impl_->outputLimiter((input*dryGain)+dryTapeOutput);
    HYDRA_PROFILE_END(Limiter, tLim);
    return limited;
  };

  ChannelFrame frameL=readCh(inL,hissL,impl_->delayLine,impl_->reproHeadBump,impl_->tapeRolloff,impl_->gapLossLPF,impl_->azimuthFilter,impl_->inputHPF,impl_->inputLPF,impl_->dropoutLPF,dropoutL,modL,warpSigL,warpFbL);
  ChannelFrame frameR=readCh(inR,hissR,impl_->delayLineR,impl_->reproHeadBumpR,impl_->tapeRolloffR,impl_->gapLossLPFR,impl_->azimuthFilterR,impl_->inputHPFR,impl_->inputLPFR,impl_->dropoutLPFR,dropoutR,modR,warpSigR,warpFbR);
#if defined(HYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING) && HYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING
  if (!impl_->warpActive) {
    // Version C: read-head playback degradation before the original feedback
    // electronics and the unchanged magnetic write. All filters are private
    // to this path, so monitoring outputs and their state stay unchanged.
    const auto colorTapeFeedback = [&](float readHead, float hiss, DropoutFrame drop,
                                       OnePoleLP& dropoutLP, AllpassFilter& azimuth,
                                       BiquadFilter& shelf, BiquadFilter& bump,
                                       BiquadFilter& gap) HYDRA_LAMBDA_INLINE HYDRA_DSP_IRAM {
      float playback = readHead;
      if (drop.hfLoss < 0.9995f || drop.ampGain < 0.9995f) {
        const float cutoff = dropoutBaseCutoff * (0.35f + 0.65f * drop.hfLoss);
        dropoutLP.setCutoffFastInv(impl_->maxDropoutFc, impl_->invSampleRate, cutoff);
        playback = dropoutLP.process(playback) * drop.ampGain;
      }
      if (useAzimuth) playback = azimuth.process(playback);
      playback = gap.process(bump.process(shelf.process(playback)));
      return experimental::classicTapeFeedbackSource(playback, hiss);
    };
    frameL.feedbackSource = colorTapeFeedback(frameL.feedbackSource, hissL, dropoutL,
        impl_->classicDropoutL, impl_->classicAzimuthL,
        impl_->classicRolloffL, impl_->classicReproBumpL, impl_->classicGapLossL);
    frameR.feedbackSource = colorTapeFeedback(frameR.feedbackSource, hissR, dropoutR,
        impl_->classicDropoutR, impl_->classicAzimuthR,
        impl_->classicRolloffR, impl_->classicReproBumpR, impl_->classicGapLossR);
  }
#endif
  const float fbSourceL=impl_->pingPong?frameR.feedbackSource:frameL.feedbackSource;
  const float fbSourceR=impl_->pingPong?frameL.feedbackSource:frameR.feedbackSource;
  const float feedbackL=feedbackFrom(fbSourceL,impl_->feedbackHPF,impl_->feedbackHeadBump,impl_->feedbackGapLoss,impl_->feedbackPhase,impl_->inEnvL);
  const float feedbackR=feedbackFrom(fbSourceR,impl_->feedbackHPFR,impl_->feedbackHeadBumpR,impl_->feedbackGapLossR,impl_->feedbackPhaseR,impl_->inEnvR);
  *outL=writeCh(inL,frameL.tapeOutput,frameL,feedbackL,impl_->delayLine,impl_->dcBlocker,impl_->magneticsL);
  *outR=writeCh(inR,frameR.tapeOutput,frameR,feedbackR,impl_->delayLineR,impl_->dcBlockerR,impl_->magneticsR);
  // One shared auxiliary chain. Morph its input, then restore transparent dry
  // after all wet processing. No delay reset, re-read, or extra engine instance.
  const float pathTarget=p->signalPathMode == 1 ? 1.f : 0.f;
  const float pathStep=1.f/std::max(1.f,.030f*impl_->sampleRate);
  float& pathBlend=impl_->signalPathBlend;
  pathBlend += clampf(pathTarget-pathBlend,-pathStep,pathStep);
  const float wetTarget=clampf(p->studioWetLevel*.01f,0.f,2.f);
  impl_->studioWetCurrent += clampf(wetTarget-impl_->studioWetCurrent,-2.f*pathStep,2.f*pathStep);
  if(pathBlend>0.f){
    *outL += pathBlend*(frameL.studioOutput-*outL);
    *outR += pathBlend*(frameR.studioOutput-*outR);
  }
  HYDRA_PROFILE_START(tAux);
  if (impl_->reverseSmear && legacyReverse) {
    for (int i=0;i<4;i++) {
      *outL=impl_->reverseAP_L[i].process(*outL);
      *outR=impl_->reverseAP_R[i].process(*outR);
    }
  } else if ((impl_->reverseSmear && wantsReverse) || impl_->reverseSmearBlend > 0.f) {
    float& blend=impl_->reverseSmearBlend;
    blend=std::clamp(blend + (impl_->reverseSmear && wantsReverse
        ? impl_->reverseFadeStep : -impl_->reverseFadeStep), 0.f, 1.f);
    float smearL=*outL, smearR=*outR;
    for (int i=0;i<4;i++) {
      smearL=impl_->reverseAP_L[i].process(smearL);
      smearR=impl_->reverseAP_R[i].process(smearR);
    }
    *outL += blend * (smearL - *outL);
    *outR += blend * (smearR - *outR);
  }
  if(impl_->spring){
    float dryL=*outL,dryR=*outR;
    int stages=impl_->springStages;
    float inWithFBL = *outL + impl_->springFB_L * impl_->springFeedbackGain;
    float inWithFBR = *outR + impl_->springFB_R * impl_->springFeedbackGain;
    float wetL = inWithFBL, wetR = inWithFBR;
    for(int i=0;i<stages&&i<6;i++){ wetL=impl_->springLPF_L[i].process(impl_->springAP_L[i].process(wetL)); wetR=impl_->springLPF_R[i].process(impl_->springAP_R[i].process(wetR)); }
    impl_->springFB_L = wetL; impl_->springFB_R = wetR;
    float wetMix=impl_->springWetGain;
    *outL=impl_->outputLimiter(dryL*(1-wetMix)+wetL*wetMix); *outR=impl_->outputLimiter(dryR*(1-wetMix)+wetR*wetMix);
  }
  if(pathBlend>0.f){
    const float trim=1.f+pathBlend*(impl_->studioWetCurrent-1.f);
    const float studioL=(*outL)*trim+inL*pathBlend;
    const float studioR=(*outR)*trim+inR*pathBlend;
    // Separate emergency sample-peak guard; no envelope or auto normalization.
    // Identity for finite sums within full scale, including dry-only transients.
    *outL=std::isfinite(studioL)?clampf(studioL,-1.f,1.f):0.f;
    *outR=std::isfinite(studioR)?clampf(studioR,-1.f,1.f):0.f;
  }
  if(impl_->freeze){if(impl_->freezeFade<1.0f){impl_->freezeFade+=0.0002f; if(impl_->freezeFade>1.0f)impl_->freezeFade=1.0f;}} else {if(impl_->freezeFade>0.0f){impl_->freezeFade-=0.0008f; if(impl_->freezeFade<0.0f)impl_->freezeFade=0.0f;}}
  HYDRA_PROFILE_END(Auxiliary, tAux);
  HYDRA_PROFILE_START(tTr);
  if(impl_->writeHead<TAPE_BUFFER_GUARD){ impl_->mirrorDelayGuard(impl_->delayLine); impl_->mirrorDelayGuard(impl_->delayLineR); }
  impl_->writeHead++; if(impl_->writeHead>=impl_->bufferSize) impl_->writeHead=0;
  outLBlock[frameIdx]=sampleOutL;
  outRBlock[frameIdx]=sampleOutR;
  impl_->transportClock.tick();
  if(impl_->framesSinceDelayEnable!=0xFFFFFFFFu) ++impl_->framesSinceDelayEnable;
  HYDRA_PROFILE_END(Transport, tTr);
  }
  impl_->tapTempo.update(impl_->transportClock.absoluteSamplePosition());
  impl_->midiClockSync.update(impl_->transportClock.absoluteSamplePosition());
  impl_->flushDenormals();
}

void TapeCore::getMotionTelemetry(float* outModL, float* outModR) const {
  if (outModL) *outModL = impl_->motionState.lastOutL;
  if (outModR) *outModR = impl_->motionState.lastOutR;
}

} // namespace hydra::dsp
