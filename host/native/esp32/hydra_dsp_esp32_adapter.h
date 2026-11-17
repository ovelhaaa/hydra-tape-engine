#pragma once

#include "hydra_dsp.h"

class HydraDspEsp32Adapter {
public:
  HydraDspEsp32Adapter(float sampleRate, float maxDelayMs = 2000.0f,
                       float historyMs = 0.0f);
  ~HydraDspEsp32Adapter();

  bool isValid() const;
  bool isReverseClamped() const;
  float effectiveReverseMs() const;
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

  int transportPlay();
  int transportPause();
  int transportStop();
  int transportReset();
  int transportSetBpm(float bpm);
  int transportSetTimeSignature(uint32_t numerator, uint32_t denominator);
  int transportGetState(hydra_dsp_transport_state* outState) const;

  int tapTempoEvent(uint64_t timestamp_frames = 0);
  int tapTempoGetState(hydra_dsp_tap_tempo_state* outState) const;
  int tapTempoReset();

private:
  bool applyParams(const hydra_dsp_params_v1_4& params, bool force);

  hydra_dsp_handle* handle_ = nullptr;
  hydra_dsp_params_v1_4 params_{};
  bool paramsInitialized_ = false;
};
