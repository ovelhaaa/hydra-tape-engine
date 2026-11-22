#pragma once

#include <algorithm>
#include <cmath>

namespace hydra::dsp::detail {

// All RBJ biquads need a digital center/corner frequency strictly below
// Nyquist. Their formulas become unreliable (and a high shelf can become
// unstable) when callers request an aliased / out-of-band cutoff.
// 0.45 * fs matches the pre-existing peak-filter guard.
inline float boundedBiquadFrequency(float sampleRate, float frequency) {
  const float upper = 0.45f * sampleRate;
  if (!std::isfinite(frequency)) return 1.0f;
  return std::max(1.0f, std::min(frequency, upper));
}

inline float boundedBiquadQ(float q) {
  if (!std::isfinite(q)) return 0.707f;
  return std::max(q, 0.001f);
}

// Threshold = 0.6, ratio = 1.5:1, total knee width = 0.2.
// Standard quadratic soft knee: the output and its first derivative match
// the unity-gain region at 0.5 and the compressed region at 0.7.
// Pure scalar / allocation-free; applies symmetrically to both polarities.
inline float continuousFeedbackSoftKnee(float x) {
  constexpr float threshold = 0.6f;
  constexpr float kneeStart = 0.5f;
  constexpr float kneeEnd = 0.7f;
  constexpr float inverseRatio = 2.0f / 3.0f;
  constexpr float inverseDoubleKnee = 2.5f; // 1 / (2 * 0.2)

  const float magnitude = std::fabs(x);
  if (magnitude <= kneeStart) return x;

  float compressed;
  if (magnitude >= kneeEnd) {
    compressed = threshold + (magnitude - threshold) * inverseRatio;
  } else {
    const float distance = magnitude - kneeStart;
    compressed = magnitude +
                 (inverseRatio - 1.0f) * distance * distance * inverseDoubleKnee;
  }
  return std::copysign(compressed, x);
}

} // namespace hydra::dsp::detail
