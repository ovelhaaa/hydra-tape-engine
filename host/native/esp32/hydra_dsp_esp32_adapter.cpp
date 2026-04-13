#include "hydra_dsp_esp32_adapter.h"

HydraDspEsp32Adapter::HydraDspEsp32Adapter(float sampleRate, float maxDelayMs) {
  if (hydra_dsp_create(sampleRate, maxDelayMs, &handle_) == 0) {
    if (hydra_dsp_prepare(handle_, 128, HYDRA_DSP_CHANNELS_STEREO) != 0) {
      hydra_dsp_destroy(handle_);
      handle_ = nullptr;
    }
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
  hydra_dsp_params p{};
  p.flutterDepth = params.flutterDepth;
  p.wowDepth = params.wowDepth;
  p.dropoutSeverity = params.dropoutSeverity;
  p.drive = params.drive;
  p.noise = params.noise;
  p.tapeSpeed = params.tapeSpeed;
  p.tapeAge = params.tapeAge;
  p.headBumpAmount = params.headBumpAmount;
  p.azimuthError = params.azimuthError;
  p.flutterRate = params.flutterRate;
  p.wowRate = params.wowRate;
  p.delayActive = params.delayActive ? 1.0f : 0.0f;
  p.delayTimeMs = params.delayTimeMs;
  p.feedback = params.feedback;
  p.dryWet = params.dryWet;
  p.activeHeads = (float)params.activeHeads;
  p.bpm = params.bpm;
  p.headsMusical = params.headsMusical ? 1.0f : 0.0f;
  p.guitarFocus = params.guitarFocus ? 1.0f : 0.0f;
  p.tone = params.tone;
  p.pingPong = params.pingPong ? 1.0f : 0.0f;
  p.freeze = params.freeze ? 1.0f : 0.0f;
  p.reverse = params.reverse ? 1.0f : 0.0f;
  p.reverseSmear = params.reverseSmear ? 1.0f : 0.0f;
  p.spring = params.spring ? 1.0f : 0.0f;
  p.springDecay = params.springDecay;
  p.springDamping = params.springDamping;
  p.springMix = params.springMix;
  hydra_dsp_set_params(handle_, &p);
  hydra_dsp_commit(handle_);
}

void HydraDspEsp32Adapter::updateFilters() {
  updateParams(params_);
}

float HydraDspEsp32Adapter::process(float input) {
  if (!handle_) return input;
  float outL = input;
  float outR = input;
  if (hydra_dsp_process(handle_, &input, &input, &outL, &outR, 1) != 0) {
    return input;
  }
  return outL;
}

void HydraDspEsp32Adapter::processStereo(float inL, float inR, float* outL, float* outR) {
  if (!outL || !outR) return;
  if (!handle_ || hydra_dsp_process(handle_, &inL, &inR, outL, outR, 1) != 0) {
    *outL = inL;
    *outR = inR;
  }
}
