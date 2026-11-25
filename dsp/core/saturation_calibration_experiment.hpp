#pragma once

#include "feedback_calibration_experiment.hpp"

// Independent opt-in experiment; zero keeps every original expression intact.
#ifndef HYDRA_DSP_REFORM_F3_VARIANT
#define HYDRA_DSP_REFORM_F3_VARIANT 0
#endif
#if HYDRA_DSP_REFORM_F3_VARIANT < 0 || HYDRA_DSP_REFORM_F3_VARIANT > 2
#error "HYDRA_DSP_REFORM_F3_VARIANT must be 0, 1 or 2"
#endif
#if HYDRA_DSP_REFORM_F3_VARIANT != 0 && \
    (!defined(HYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING) || !HYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING || HYDRA_DSP_REFORM_F2_VARIANT != 2)
#error "F3 candidates require explicit Classic C and F2 Vintage (variant 2)"
#endif

namespace hydra::dsp::experimental {
constexpr float kF3FirstDrive = HYDRA_DSP_REFORM_F3_VARIANT == 1 ? 1.04f : 1.16f;
constexpr float kF3InterstageTrim = HYDRA_DSP_REFORM_F3_VARIANT == 1 ? 0.86f : 0.78f;
constexpr float kF3SecondDrive = HYDRA_DSP_REFORM_F3_VARIANT == 1 ? 1.08f : 1.07f;
} // namespace hydra::dsp::experimental
