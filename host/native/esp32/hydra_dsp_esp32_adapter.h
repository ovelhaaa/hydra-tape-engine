#pragma once

#include "TapeDelay.h"
#include "hydra_dsp.h"

class HydraDspEsp32Adapter {
public:
  HydraDspEsp32Adapter(float sampleRate, float maxDelayMs = 2000.0f);
  ~HydraDspEsp32Adapter();

  bool isValid() const;
  void updateParams(const TapeParams& params);
  void updateFilters();
  float process(float input);
  void processStereo(float inL, float inR, float* outL, float* outR);

private:
  hydra_dsp_handle* handle_ = nullptr;
  TapeParams params_{};
};
