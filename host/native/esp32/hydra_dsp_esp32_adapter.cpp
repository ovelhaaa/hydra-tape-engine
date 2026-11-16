#include "hydra_dsp_esp32_adapter.h"

namespace {

bool sameParams(const hydra_dsp_params& a, const hydra_dsp_params& b) {
  return a.flutterDepth == b.flutterDepth &&
         a.wowDepth == b.wowDepth &&
         a.dropoutSeverity == b.dropoutSeverity &&
         a.drive == b.drive &&
         a.noise == b.noise &&
         a.tapeSpeed == b.tapeSpeed &&
         a.tapeAge == b.tapeAge &&
         a.headBumpAmount == b.headBumpAmount &&
         a.azimuthError == b.azimuthError &&
         a.flutterRate == b.flutterRate &&
         a.wowRate == b.wowRate &&
         a.delayActive == b.delayActive &&
         a.delayTimeMs == b.delayTimeMs &&
         a.feedback == b.feedback &&
         a.dryWet == b.dryWet &&
         a.delayWet == b.delayWet &&
         a.activeHeads == b.activeHeads &&
         a.bpm == b.bpm &&
         a.headsMusical == b.headsMusical &&
         a.guitarFocus == b.guitarFocus &&
         a.tone == b.tone &&
         a.pingPong == b.pingPong &&
         a.freeze == b.freeze &&
         a.reverse == b.reverse &&
         a.reverseSmear == b.reverseSmear &&
         a.spring == b.spring &&
         a.springDecay == b.springDecay &&
         a.springDamping == b.springDamping &&
         a.springMix == b.springMix &&
         a.warpActive == b.warpActive &&
         a.warpSpeed == b.warpSpeed &&
         a.warpGrainMs == b.warpGrainMs &&
         a.warpStereo == b.warpStereo &&
         a.warpFeedback == b.warpFeedback &&
         a.warpMode == b.warpMode;
}

}  // namespace

HydraDspEsp32Adapter::HydraDspEsp32Adapter(float sampleRate, float maxDelayMs,
                                             float historyMs) {
  if (hydra_dsp_create_v1_4(sampleRate, maxDelayMs, historyMs, &handle_) == 0) {
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

bool HydraDspEsp32Adapter::isReverseClamped() const {
  int clamped = 0;
  if (handle_ && hydra_dsp_get_reverse_status(handle_, nullptr, &clamped) == 0) {
    return clamped != 0;
  }
  return false;
}

float HydraDspEsp32Adapter::effectiveReverseMs() const {
  float ms = 0.0f;
  if (handle_ && hydra_dsp_get_reverse_status(handle_, &ms, nullptr) == 0) {
    return ms;
  }
  return 0.0f;
}

void HydraDspEsp32Adapter::updateParams(const hydra_dsp_params& params) {
  updateParams({params, HYDRA_DSP_REVERSE_LEGACY, HYDRA_DSP_REVERSE_QUARTER});
}

void HydraDspEsp32Adapter::updateParams(const hydra_dsp_params_v1_4& params) {
  applyParams(params, false);
}

bool HydraDspEsp32Adapter::applyParams(const hydra_dsp_params_v1_4& params, bool force) {
  if (!handle_) return false;
  if (!force && paramsInitialized_ && sameParams(params_.v1_3, params.v1_3) &&
      params_.reverseMode == params.reverseMode && params_.reverseDivision == params.reverseDivision) return false;

  const int setResult = hydra_dsp_set_params_sized(handle_, &params, sizeof(params));
  if (setResult != 0) return false;
  const int commitResult = hydra_dsp_commit(handle_);
  if (commitResult != 0) return false;

  params_ = params;
  paramsInitialized_ = true;
  return true;
}

void HydraDspEsp32Adapter::updateFilters() {
  applyParams(params_, true);
}

float HydraDspEsp32Adapter::process(float input) {
  if (!handle_) return input;
  float outL = input;
  float outR = input;
  if (processStereoBlock(&input, &input, &outL, &outR, 1) != 0) {
    return input;
  }
  return outL;
}

void HydraDspEsp32Adapter::processStereo(float inL, float inR, float* outL, float* outR) {
  if (!outL || !outR) return;
  if (processStereoBlock(&inL, &inR, outL, outR, 1) != 0) {
    *outL = inL;
    *outR = inR;
  }
}

int HydraDspEsp32Adapter::processStereoBlock(const float* inL,
                                             const float* inR,
                                             float* outL,
                                             float* outR,
                                             uint32_t frames) {
  if (!handle_ || !inL || !inR || !outL || !outR) return -1;
  return hydra_dsp_process(handle_, inL, inR, outL, outR, frames);
}
