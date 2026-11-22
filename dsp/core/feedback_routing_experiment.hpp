#pragma once

namespace hydra::dsp::experimental {

// Variant B: clean head read, no monitor gain and no playback degradation.
// The Reverse crossfade has already happened at the read-head stage.
inline float independentTapeFeedbackSource(float delayHeadSignal) {
  return delayHeadSignal;
}

// Variant C: classic-style reproduction signal fed back to the record chain.
// The caller applies playback-head EQ, dropout and azimuth to the unscaled
// read-head signal using SEPARATE stateful filters. Tape hiss is modeled as a
// small playback-noise contribution before the unchanged feedback electronics.
// No monitoring mix or output-only effects are involved.
//
// This gain is an EXPERIMENTAL fixed hypothesis, not an RE-201 calibration
// and not a public parameter. Reassess based on spectral/noise A/B/C evidence.
constexpr float kClassicTapeHissRecirculation = 0.20f;

inline float classicTapeFeedbackSource(float degradedHeadSignal,
                                       float tapeHiss) {
  return degradedHeadSignal +
         kClassicTapeHissRecirculation * tapeHiss;
}

} // namespace hydra::dsp::experimental
