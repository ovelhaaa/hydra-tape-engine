#pragma once

#include <algorithm>
#include <cmath>

namespace hydra::dsp::experimental {

#ifndef HYDRA_RA_AUDIO_INLINE
#if defined(__GNUC__) || defined(__clang__)
#define HYDRA_RA_AUDIO_INLINE inline __attribute__((always_inline))
#else
#define HYDRA_RA_AUDIO_INLINE inline
#endif
#endif

#ifndef HYDRA_RA_IRAM
#if defined(ESP_PLATFORM) && defined(IRAM_ATTR)
#define HYDRA_RA_IRAM IRAM_ATTR
#else
#define HYDRA_RA_IRAM
#endif
#endif

HYDRA_RA_AUDIO_INLINE float raClampf(float v, float lo, float hi) {
  return std::max(lo, std::min(v, hi));
}

HYDRA_RA_AUDIO_INLINE float raFastRecip(float d) {
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

class RepeatAgingLPF {
 public:
  void reset() { z = 0.0f; }
  void flushDenormals() {
    if (std::fabs(z) < 1e-20f) z = 0.0f;
  }
  HYDRA_RA_AUDIO_INLINE void setCutoffFastInv(float maxFc, float invFs, float fc) {
    constexpr float kTwoPi = 6.28318530718f;
    fc = raClampf(fc, 20.0f, maxFc);
    const float x = kTwoPi * fc * invFs;
    a = raClampf(x * raFastRecip(1.0f + x), 0.0f, 1.0f);
  }
  HYDRA_RA_AUDIO_INLINE float process(float in) {
    z += a * (in - z);
    return z;
  }

 private:
  float a = 0.1f;
  float z = 0.0f;
};

class RepeatAgingAllpass {
 public:
  void setCoeff(float c) { a1 = raClampf(c, -0.99f, 0.99f); }
  void reset() { z1 = 0.0f; }
  void flushDenormals() {
    if (std::fabs(z1) < 1e-20f) z1 = 0.0f;
  }
  HYDRA_RA_AUDIO_INLINE float process(float in) {
    const float out = a1 * in + z1;
    z1 = in - a1 * out;
    return out;
  }

 private:
  float a1 = 0.0f;
  float z1 = 0.0f;
};

struct RepeatAgingCoeffs {
  float invSampleRate = 1.0f / 48000.0f;
  float maxDropoutFc = 21600.0f;
  float c1EnvAttackAlpha = 0.0f;
  float c1EnvReleaseAlpha = 0.0f;
  float c1HfAttackAlpha = 0.0f;
  float c1HfReleaseAlpha = 0.0f;
  float c2FastAttackAlpha = 0.0f;
  float c2FastReleaseAlpha = 0.0f;
  float c2SlowAttackAlpha = 0.0f;
  float c2SlowReleaseAlpha = 0.0f;
  float c2TransAttackAlpha = 0.0f;
  float c2TransReleaseAlpha = 0.0f;

  void update(float sampleRate) {
    const float fs = std::max(1.0f, sampleRate);
    invSampleRate = 1.0f / fs;
    maxDropoutFc = 0.45f * fs;
    c1EnvAttackAlpha = 1.0f - std::exp(-1.0f / (0.015f * fs));
    c1EnvReleaseAlpha = 1.0f - std::exp(-1.0f / (0.180f * fs));
    c1HfAttackAlpha = 1.0f - std::exp(-1.0f / (0.008f * fs));
    c1HfReleaseAlpha = 1.0f - std::exp(-1.0f / (0.120f * fs));
    c2FastAttackAlpha = 1.0f - std::exp(-1.0f / (0.0025f * fs));
    c2FastReleaseAlpha = 1.0f - std::exp(-1.0f / (0.025f * fs));
    c2SlowAttackAlpha = 1.0f - std::exp(-1.0f / (0.020f * fs));
    c2SlowReleaseAlpha = 1.0f - std::exp(-1.0f / (0.200f * fs));
    c2TransAttackAlpha = 1.0f - std::exp(-1.0f / (0.0015f * fs));
    c2TransReleaseAlpha = 1.0f - std::exp(-1.0f / (0.035f * fs));
  }
};

struct RepeatAgingChannelState {
  float prevX = 0.0f;
  float envSlow = 0.0f;
  float hfEnv = 0.0f;
  RepeatAgingLPF wearLPF;
  float fastEnv = 0.0f;
  float slowEnv = 0.0f;
  float transSmooth = 0.0f;
  RepeatAgingLPF softLPF;
  RepeatAgingAllpass smearAP;

  void reset() {
    prevX = 0.0f;
    envSlow = 0.0f;
    hfEnv = 0.0f;
    wearLPF.reset();
    fastEnv = 0.0f;
    slowEnv = 0.0f;
    transSmooth = 0.0f;
    softLPF.reset();
    smearAP.reset();
    smearAP.setCoeff(0.38f);
  }

  void flushDenormals() {
    if (std::fabs(prevX) < 1e-20f) prevX = 0.0f;
    if (std::fabs(envSlow) < 1e-20f) envSlow = 0.0f;
    if (std::fabs(hfEnv) < 1e-20f) hfEnv = 0.0f;
    wearLPF.flushDenormals();
    if (std::fabs(fastEnv) < 1e-20f) fastEnv = 0.0f;
    if (std::fabs(slowEnv) < 1e-20f) slowEnv = 0.0f;
    if (std::fabs(transSmooth) < 1e-20f) transSmooth = 0.0f;
    softLPF.flushDenormals();
    smearAP.flushDenormals();
  }
};

// C1: Dynamic Spectral Wear (parallel dry/wet mix; amount == 0 -> exact x)
HYDRA_RA_IRAM HYDRA_RA_AUDIO_INLINE float processDynamicSpectralWear(
    float x,
    RepeatAgingChannelState& st,
    const RepeatAgingCoeffs& c,
    float tapeAgeNorm,
    float tapeSpeedNorm,
    float depthScale,
    float* outAmount = nullptr) {
  const float absX = std::fabs(x);
  const float absHf = std::fabs(x - st.prevX);
  st.prevX = x;
  const float aEnv = (absX > st.envSlow) ? c.c1EnvAttackAlpha : c.c1EnvReleaseAlpha;
  st.envSlow += aEnv * (absX - st.envSlow);
  const float aHf = (absHf > st.hfEnv) ? c.c1HfAttackAlpha : c.c1HfReleaseAlpha;
  st.hfEnv += aHf * (absHf - st.hfEnv);
  const float wearNorm = raClampf(1.4f * st.envSlow + 1.1f * st.hfEnv, 0.0f, 1.0f);
  const float ageHeadroom = 0.35f + 0.65f * (1.0f - 0.60f * tapeAgeNorm);
  const float amount = raClampf(wearNorm * ageHeadroom * depthScale, 0.0f, 1.0f);
  if (outAmount) *outAmount = amount;
  const float maxCut = raClampf(11000.0f + 3000.0f * tapeSpeedNorm, 4000.0f, c.maxDropoutFc);
  const float minCut = 2400.0f + 600.0f * tapeSpeedNorm;
  const float fc = maxCut - (maxCut - minCut) * (amount * 0.78f);
  st.wearLPF.setCutoffFastInv(c.maxDropoutFc, c.invSampleRate, fc);
  const float y = st.wearLPF.process(x);
  return x + amount * (y - x);
}

// C2: Transient Softening (parallel dry/wet mix; mod == 0 -> exact x)
HYDRA_RA_IRAM HYDRA_RA_AUDIO_INLINE float processTransientSoftening(
    float x,
    RepeatAgingChannelState& st,
    const RepeatAgingCoeffs& c,
    float tapeSpeedNorm,
    float depthScale,
    float* outMod = nullptr) {
  const float absX = std::fabs(x);
  const float aFast = (absX > st.fastEnv) ? c.c2FastAttackAlpha : c.c2FastReleaseAlpha;
  st.fastEnv += aFast * (absX - st.fastEnv);
  const float aSlow = (absX > st.slowEnv) ? c.c2SlowAttackAlpha : c.c2SlowReleaseAlpha;
  st.slowEnv += aSlow * (absX - st.slowEnv);
  const float transient = std::max(0.0f, st.fastEnv - 1.22f * st.slowEnv);
  const float transNorm = raClampf(transient * raFastRecip(st.slowEnv + 0.03f), 0.0f, 1.0f);
  const float aTrans = (transNorm > st.transSmooth) ? c.c2TransAttackAlpha : c.c2TransReleaseAlpha;
  st.transSmooth += aTrans * (transNorm - st.transSmooth);
  const float mod = raClampf(st.transSmooth * depthScale, 0.0f, 1.0f);
  if (outMod) *outMod = mod;
  const float minCut = 1400.0f + 600.0f * tapeSpeedNorm;
  const float maxCut = std::min(14000.0f, c.maxDropoutFc);
  const float fc = maxCut - (maxCut - minCut) * mod;
  st.softLPF.setCutoffFastInv(c.maxDropoutFc, c.invSampleRate, fc);
  const float y = st.softLPF.process(x);
  const float ap = st.smearAP.process(y);
  const float wet = y + 0.30f * (ap - y);
  return x + mod * (wet - x);
}

}  // namespace hydra::dsp::experimental
