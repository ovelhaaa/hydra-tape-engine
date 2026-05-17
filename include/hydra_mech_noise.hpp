#pragma once

#include <cmath>
#include <cstdint>

#ifndef HYDRA_AUDIO_INLINE
#define HYDRA_AUDIO_INLINE inline
#endif

namespace hydra { namespace dsp {

constexpr float kScrapeModAmount = 0.00015f;
constexpr float kMechNoiseSlowTimeSeconds = 0.2604f;
constexpr float kMechNoiseFastTimeSeconds = 0.00833f;

HYDRA_AUDIO_INLINE float mechNoiseOnePoleAlpha(float sampleRate, float timeSeconds) {
  if (sampleRate <= 0.0f || timeSeconds <= 0.0f) return 1.0f;
  return 1.0f - ::expf(-1.0f / (timeSeconds * sampleRate));
}

struct MechNoise {
  uint32_t initialSeed = 22222u;
  uint32_t s = 22222u;
  float z1 = 0.0f;
  float z2 = 0.0f;

  explicit MechNoise(uint32_t seed = 22222u) : initialSeed(seed), s(seed) {}

  HYDRA_AUDIO_INLINE void reset() {
    s = initialSeed;
    z1 = 0.0f;
    z2 = 0.0f;
  }

  HYDRA_AUDIO_INLINE void reset(uint32_t seed) {
    initialSeed = seed;
    s = seed;
    z1 = 0.0f;
    z2 = 0.0f;
  }

  HYDRA_AUDIO_INLINE uint32_t randu() {
    s = s * 1664525u + 1013904223u;
    return s;
  }

  HYDRA_AUDIO_INLINE float white() {
    return float((randu() >> 9) & 0x7FFFFF) * (1.0f / 4194304.0f) - 1.0f;
  }

  HYDRA_AUDIO_INLINE float lowRate(float aSlow, float aFast) {
    float w = white();
    z1 += aSlow * (w - z1);
    z2 += aFast * (z1 - z2);
    return z2;
  }
};

} } // namespace hydra::dsp
