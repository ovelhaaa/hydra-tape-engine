#pragma once

// F4 performance-transition experiment, opt-in and compile-time only.
// 0 keeps the frozen Vintage F3-A expressions byte-for-byte.
// 1 enables short, block-size independent gain ramps for head selection,
//   freeze release, mix and drive. Tone is measured, not re-smoothed.
#ifndef HYDRA_DSP_REFORM_F4_TRANSITIONS
#define HYDRA_DSP_REFORM_F4_TRANSITIONS 0
#endif
#if HYDRA_DSP_REFORM_F4_TRANSITIONS != 0 && HYDRA_DSP_REFORM_F4_TRANSITIONS != 1
#error "HYDRA_DSP_REFORM_F4_TRANSITIONS must be 0 or 1"
#endif
#if HYDRA_DSP_REFORM_F4_TRANSITIONS && \
    (!defined(HYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING) || !HYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING || \
     HYDRA_DSP_REFORM_F2_VARIANT != 2 || HYDRA_DSP_REFORM_F3_VARIANT != 0)
#error "F4 transitions require explicit Classic C, F2 Vintage (variant 2) and F3=0"
#endif

namespace hydra::dsp::experimental {
// Head selection is the priority gesture. 10 ms is the measured compromise:
// long enough to remove the invHeadGainSum step, short enough to feel immediate.
constexpr float kF4HeadFadeSeconds = 0.010f;
// Mix and drive only ramp the gains that are actually involved. A single 10 ms
// linear segment preserves the final value exactly and has no block coupling.
constexpr float kF4MixFadeSeconds = 0.010f;
constexpr float kF4DriveFadeSeconds = 0.010f;
// Freeze is a hard write gate while active. On release the record path fades
// in: this is the only window where the frozen buffer is written again.
constexpr float kF4FreezeReleaseFadeSeconds = 0.012f;
// Guard for the normalization during a mask-to-silence transition (the public
// API never selects 0 heads, but TapeParams can be driven directly).
constexpr float kF4HeadNormEpsilon = 1e-6f;
} // namespace hydra::dsp::experimental
