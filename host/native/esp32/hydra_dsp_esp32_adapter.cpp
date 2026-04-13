#include "hydra_dsp_esp32_adapter.h"

HydraDspEsp32Adapter::HydraDspEsp32Adapter(float sampleRate, float maxDelayMs) {
  if (hydra_dsp_create(sampleRate, maxDelayMs, &handle_) == 0) {
    hydra_dsp_prepare(handle_, 128, 2);
  }
}

HydraDspEsp32Adapter::~HydraDspEsp32Adapter() {
  hydra_dsp_destroy(handle_);
}

bool HydraDspEsp32Adapter::isValid() const {
  return handle_ != nullptr;
}

void HydraDspEsp32Adapter::updateParams(const TapeParams& params) {
  if (!handle_) return;
  params_ = params;
  hydra_dsp_set_parameter(handle_, HYDRA_DSP_PARAM_FLUTTER_DEPTH, params.flutterDepth);
  hydra_dsp_set_parameter(handle_, HYDRA_DSP_PARAM_WOW_DEPTH, params.wowDepth);
  hydra_dsp_set_parameter(handle_, HYDRA_DSP_PARAM_DROPOUT, params.dropoutSeverity);
  hydra_dsp_set_parameter(handle_, HYDRA_DSP_PARAM_DRIVE, params.drive);
  hydra_dsp_set_parameter(handle_, HYDRA_DSP_PARAM_NOISE, params.noise);
  hydra_dsp_set_parameter(handle_, HYDRA_DSP_PARAM_DELAY_ACTIVE, params.delayActive ? 1.0f : 0.0f);
  hydra_dsp_set_parameter(handle_, HYDRA_DSP_PARAM_DELAY_MS, params.delayTimeMs);
  hydra_dsp_set_parameter(handle_, HYDRA_DSP_PARAM_FEEDBACK, params.feedback);
  hydra_dsp_set_parameter(handle_, HYDRA_DSP_PARAM_DRY_WET, params.dryWet);
}

void HydraDspEsp32Adapter::updateFilters() {
  updateParams(params_);
}

float HydraDspEsp32Adapter::process(float input) {
  float outL = input;
  float outR = input;
  hydra_dsp_process(handle_, &input, &input, &outL, &outR, 1);
  return outL;
}

void HydraDspEsp32Adapter::processStereo(float inL, float inR, float* outL, float* outR) {
  hydra_dsp_process(handle_, &inL, &inR, outL, outR, 1);
}
