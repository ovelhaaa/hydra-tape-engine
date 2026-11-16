#pragma once

#include <cstdint>

namespace hydra::dsp {

inline int32_t saturatePcm32(float sample) {
  constexpr float lower = -2147483648.0f;
  constexpr float upper = 2147483648.0f;
  // INT32_MAX rounds up to upper in float32, so the upper bound is exclusive.
  if (sample >= lower && sample < upper) return static_cast<int32_t>(sample);
  if (sample >= upper) return INT32_MAX;
  if (sample < lower) return INT32_MIN;
  return 0; // NaN
}

} // namespace hydra::dsp
