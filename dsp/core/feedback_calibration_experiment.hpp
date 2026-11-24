#pragma once

// F2 is an offline/opt-in calibration of Classic C, never a routing selector.
#ifndef HYDRA_DSP_REFORM_F2_VARIANT
#define HYDRA_DSP_REFORM_F2_VARIANT 0
#endif
#if HYDRA_DSP_REFORM_F2_VARIANT < 0 || HYDRA_DSP_REFORM_F2_VARIANT > 2
#error "HYDRA_DSP_REFORM_F2_VARIANT must be 0, 1 or 2"
#endif
#if HYDRA_DSP_REFORM_F2_VARIANT != 0 && \
    (!defined(HYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING) || !HYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING)
#error "F2 calibration requires explicit Classic C routing"
#endif

namespace hydra::dsp::experimental {
constexpr float kF2FeedbackHighpassHz = HYDRA_DSP_REFORM_F2_VARIANT == 1 ? 180.f :
                                      HYDRA_DSP_REFORM_F2_VARIANT == 2 ? 240.f : 300.f;
constexpr float kF2FeedbackBumpScale = HYDRA_DSP_REFORM_F2_VARIANT == 1 ? 0.012f :
                                     HYDRA_DSP_REFORM_F2_VARIANT == 2 ? 0.016f : 0.02f;
} // namespace hydra::dsp::experimental
