#pragma once

#include "hydra_dsp.h"

class HydraDspEsp32Adapter {
public:
  HydraDspEsp32Adapter(float sampleRate, float maxDelayMs = 2000.0f,
                       float historyMs = 0.0f);
  ~HydraDspEsp32Adapter();

  bool isValid() const;
  void updateParams(const hydra_dsp_params& params);
  void updateParams(const hydra_dsp_params_v1_4& params);
  void updateFilters();
  float process(float input);
  void processStereo(float inL, float inR, float* outL, float* outR);
  int processStereoBlock(const float* inL,
                         const float* inR,
                         float* outL,
                         float* outR,
                         uint32_t frames);

private:
  bool applyParams(const hydra_dsp_params_v1_4& params, bool force);

  hydra_dsp_handle* handle_ = nullptr;
  hydra_dsp_params_v1_4 params_{};
  bool paramsInitialized_ = false;
};
