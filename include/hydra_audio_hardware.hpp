#pragma once
#include <cstdint>
#include <cstddef>
#include <cmath>
#include "hydra_pcm.hpp"
namespace hydra { namespace hardware {
constexpr int mclkPin=14, bclkPin=15, wsPin=16, doutPin=17, dinPin=18;
constexpr unsigned sampleRate=44100, mclkMultiple=256;
// PCM1808 I2S payload occupies bits 31:8. Ignore undefined trailing clocks.
inline int32_t pcm24Signed(uint32_t slot) {
  const uint32_t raw=slot >> 8;
  return (raw & 0x800000u) ? static_cast<int32_t>(raw)-16777216 : static_cast<int32_t>(raw);
}
inline float pcm24Float(uint32_t slot) { return pcm24Signed(slot)*(1.0f/8388608.0f); }
inline int32_t pcm32(float x) { return hydra::dsp::saturatePcm32(x*2147483648.0f); }
// Share route selection as an aligned word on Xtensa.
enum class Route : uint32_t { Normal, Silence, Tone, Left, Right, Monitor };
inline float tone(uint32_t &phase) {
  // Fixed-cost oscillator, one polynomial per diagnostic frame; no libm in callback.
  phase += 42852281u; // round(440/44100 * 2^32)
  float x=static_cast<float>(phase)*(1.0f/4294967296.0f);
  // Fold to [-pi/2,pi/2], seventh-order sine (max error < 0.00016).
  x=x<0.25f ? x : (x<0.75f ? 0.5f-x : x-1.0f);
  x*=6.283185307179586f;
  const float x2=x*x;
  return 0.1f*x*(1.0f+x2*(-1.0f/6+x2*(1.0f/120-x2/5040)));

}
// Caller only uses this for non-Normal routes, bypassing the musical engine.
inline void diagnosticFrame(Route route, float left, float right, uint32_t &phase,
                            int32_t &outL, int32_t &outR) {
  float l=0, r=0;
  if(route==Route::Monitor) { l=left*0.25f; r=right*0.25f; }
  else if(route==Route::Tone || route==Route::Left || route==Route::Right) {
    const float t=tone(phase);
    l=route==Route::Right ? 0 : t;
    r=route==Route::Left ? 0 : t;
  }
  outL=pcm32(l); outR=pcm32(r);
}
struct Capture {
  uint32_t frames=0, failures=0, shortReads=0, clips[2]={};
  float peak[2]={}, sum[2]={}, squares[2]={};
  void add(uint32_t left,uint32_t right) {
    const float v[2]={pcm24Float(left),pcm24Float(right)};
    for(int c=0;c<2;++c) { const float a=std::fabs(v[c]);
      if(a>peak[c]) peak[c]=a;
      sum[c]+=v[c]; squares[c]+=v[c]*v[c]; if(a>=0.99f) ++clips[c];
    }
    ++frames;
  }
};
}} // namespace hydra::hardware
