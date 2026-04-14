#include "hydra_dsp.h"
#include "tape_core.hpp"

#include <new>

struct hydra_dsp_handle {
  hydra::dsp::TapeCore* core;
  hydra::dsp::TapeParams params;
  bool dirty;
};

static const hydra_dsp_param_spec kParamSpecs[HYDRA_DSP_PARAM_COUNT] = {
    {HYDRA_DSP_PARAM_FLUTTER_DEPTH, "flutterDepth", 0.0f, 100.0f, 20.0f, HYDRA_DSP_SMOOTHING_BLOCK_EDGE},
    {HYDRA_DSP_PARAM_WOW_DEPTH, "wowDepth", 0.0f, 100.0f, 15.0f, HYDRA_DSP_SMOOTHING_BLOCK_EDGE},
    {HYDRA_DSP_PARAM_DROPOUT, "dropoutSeverity", 0.0f, 100.0f, 8.0f, HYDRA_DSP_SMOOTHING_BLOCK_EDGE},
    {HYDRA_DSP_PARAM_DRIVE, "drive", 0.0f, 100.0f, 40.0f, HYDRA_DSP_SMOOTHING_BLOCK_EDGE},
    {HYDRA_DSP_PARAM_NOISE, "noise", 0.0f, 100.0f, 30.0f, HYDRA_DSP_SMOOTHING_BLOCK_EDGE},
    {HYDRA_DSP_PARAM_TAPE_SPEED, "tapeSpeed", 0.0f, 100.0f, 50.0f, HYDRA_DSP_SMOOTHING_BLOCK_EDGE},
    {HYDRA_DSP_PARAM_TAPE_AGE, "tapeAge", 0.0f, 100.0f, 40.0f, HYDRA_DSP_SMOOTHING_BLOCK_EDGE},
    {HYDRA_DSP_PARAM_HEAD_BUMP, "headBumpAmount", 0.0f, 100.0f, 30.0f, HYDRA_DSP_SMOOTHING_BLOCK_EDGE},
    {HYDRA_DSP_PARAM_AZIMUTH, "azimuthError", 0.0f, 100.0f, 10.0f, HYDRA_DSP_SMOOTHING_BLOCK_EDGE},
    {HYDRA_DSP_PARAM_FLUTTER_RATE, "flutterRate", 0.1f, 20.0f, 6.0f, HYDRA_DSP_SMOOTHING_BLOCK_EDGE},
    {HYDRA_DSP_PARAM_WOW_RATE, "wowRate", 0.1f, 5.0f, 0.8f, HYDRA_DSP_SMOOTHING_BLOCK_EDGE},
    {HYDRA_DSP_PARAM_DELAY_ACTIVE, "delayActive", 0.0f, 1.0f, 0.0f, HYDRA_DSP_SMOOTHING_NONE},
    {HYDRA_DSP_PARAM_DELAY_MS, "delayTimeMs", 10.0f, 2000.0f, 500.0f, HYDRA_DSP_SMOOTHING_PER_SAMPLE_INTERNAL},
    {HYDRA_DSP_PARAM_FEEDBACK, "feedback", 0.0f, 100.0f, 40.0f, HYDRA_DSP_SMOOTHING_BLOCK_EDGE},
    {HYDRA_DSP_PARAM_DRY_WET, "dryWet", 0.0f, 100.0f, 50.0f, HYDRA_DSP_SMOOTHING_BLOCK_EDGE},
    {HYDRA_DSP_PARAM_ACTIVE_HEADS, "activeHeads", 1.0f, 7.0f, 4.0f, HYDRA_DSP_SMOOTHING_NONE},
    {HYDRA_DSP_PARAM_BPM, "bpm", 30.0f, 300.0f, 120.0f, HYDRA_DSP_SMOOTHING_BLOCK_EDGE},
    {HYDRA_DSP_PARAM_HEADS_MUSICAL, "headsMusical", 0.0f, 1.0f, 0.0f, HYDRA_DSP_SMOOTHING_NONE},
    {HYDRA_DSP_PARAM_GUITAR_FOCUS, "guitarFocus", 0.0f, 1.0f, 0.0f, HYDRA_DSP_SMOOTHING_NONE},
    {HYDRA_DSP_PARAM_TONE, "tone", 0.0f, 100.0f, 50.0f, HYDRA_DSP_SMOOTHING_BLOCK_EDGE},
    {HYDRA_DSP_PARAM_PING_PONG, "pingPong", 0.0f, 1.0f, 0.0f, HYDRA_DSP_SMOOTHING_NONE},
    {HYDRA_DSP_PARAM_FREEZE, "freeze", 0.0f, 1.0f, 0.0f, HYDRA_DSP_SMOOTHING_NONE},
    {HYDRA_DSP_PARAM_REVERSE, "reverse", 0.0f, 1.0f, 0.0f, HYDRA_DSP_SMOOTHING_NONE},
    {HYDRA_DSP_PARAM_REVERSE_SMEAR, "reverseSmear", 0.0f, 1.0f, 0.0f, HYDRA_DSP_SMOOTHING_NONE},
    {HYDRA_DSP_PARAM_SPRING, "spring", 0.0f, 1.0f, 0.0f, HYDRA_DSP_SMOOTHING_NONE},
    {HYDRA_DSP_PARAM_SPRING_DECAY, "springDecay", 0.0f, 100.0f, 60.0f, HYDRA_DSP_SMOOTHING_BLOCK_EDGE},
    {HYDRA_DSP_PARAM_SPRING_DAMPING, "springDamping", 0.0f, 100.0f, 45.0f, HYDRA_DSP_SMOOTHING_BLOCK_EDGE},
    {HYDRA_DSP_PARAM_SPRING_MIX, "springMix", 0.0f, 100.0f, 50.0f, HYDRA_DSP_SMOOTHING_BLOCK_EDGE},
    {HYDRA_DSP_PARAM_RESET, "reset", 0.0f, 1.0f, 0.0f, HYDRA_DSP_SMOOTHING_NONE},
};

static void from_c(const hydra_dsp_params& in, hydra::dsp::TapeParams* out) {
  out->flutterDepth = in.flutterDepth;
  out->wowDepth = in.wowDepth;
  out->dropoutSeverity = in.dropoutSeverity;
  out->drive = in.drive;
  out->noise = in.noise;
  out->tapeSpeed = in.tapeSpeed;
  out->tapeAge = in.tapeAge;
  out->headBumpAmount = in.headBumpAmount;
  out->azimuthError = in.azimuthError;
  out->flutterRate = in.flutterRate;
  out->wowRate = in.wowRate;
  out->delayActive = in.delayActive > 0.5f;
  out->delayTimeMs = in.delayTimeMs;
  out->feedback = in.feedback;
  out->dryWet = in.dryWet;
  out->activeHeads = (int)in.activeHeads;
  out->bpm = in.bpm;
  out->headsMusical = in.headsMusical > 0.5f;
  out->guitarFocus = in.guitarFocus > 0.5f;
  out->tone = in.tone;
  out->pingPong = in.pingPong > 0.5f;
  out->freeze = in.freeze > 0.5f;
  out->reverse = in.reverse > 0.5f;
  out->reverseSmear = in.reverseSmear > 0.5f;
  out->spring = in.spring > 0.5f;
  out->springDecay = in.springDecay;
  out->springDamping = in.springDamping;
  out->springMix = in.springMix;
}

static int set_param(hydra_dsp_handle* h, hydra_dsp_param_id id, float v) {
  switch (id) {
    case HYDRA_DSP_PARAM_FLUTTER_DEPTH: h->params.flutterDepth = v; break;
    case HYDRA_DSP_PARAM_WOW_DEPTH: h->params.wowDepth = v; break;
    case HYDRA_DSP_PARAM_DROPOUT: h->params.dropoutSeverity = v; break;
    case HYDRA_DSP_PARAM_DRIVE: h->params.drive = v; break;
    case HYDRA_DSP_PARAM_NOISE: h->params.noise = v; break;
    case HYDRA_DSP_PARAM_TAPE_SPEED: h->params.tapeSpeed = v; break;
    case HYDRA_DSP_PARAM_TAPE_AGE: h->params.tapeAge = v; break;
    case HYDRA_DSP_PARAM_HEAD_BUMP: h->params.headBumpAmount = v; break;
    case HYDRA_DSP_PARAM_AZIMUTH: h->params.azimuthError = v; break;
    case HYDRA_DSP_PARAM_FLUTTER_RATE: h->params.flutterRate = v; break;
    case HYDRA_DSP_PARAM_WOW_RATE: h->params.wowRate = v; break;
    case HYDRA_DSP_PARAM_DELAY_ACTIVE: h->params.delayActive = v > 0.5f; break;
    case HYDRA_DSP_PARAM_DELAY_MS: h->params.delayTimeMs = v; break;
    case HYDRA_DSP_PARAM_FEEDBACK: h->params.feedback = v; break;
    case HYDRA_DSP_PARAM_DRY_WET: h->params.dryWet = v; break;
    case HYDRA_DSP_PARAM_ACTIVE_HEADS: h->params.activeHeads = (int)v; break;
    case HYDRA_DSP_PARAM_BPM: h->params.bpm = v; break;
    case HYDRA_DSP_PARAM_HEADS_MUSICAL: h->params.headsMusical = v > 0.5f; break;
    case HYDRA_DSP_PARAM_GUITAR_FOCUS: h->params.guitarFocus = v > 0.5f; break;
    case HYDRA_DSP_PARAM_TONE: h->params.tone = v; break;
    case HYDRA_DSP_PARAM_PING_PONG: h->params.pingPong = v > 0.5f; break;
    case HYDRA_DSP_PARAM_FREEZE: h->params.freeze = v > 0.5f; break;
    case HYDRA_DSP_PARAM_REVERSE: h->params.reverse = v > 0.5f; break;
    case HYDRA_DSP_PARAM_REVERSE_SMEAR: h->params.reverseSmear = v > 0.5f; break;
    case HYDRA_DSP_PARAM_SPRING: h->params.spring = v > 0.5f; break;
    case HYDRA_DSP_PARAM_SPRING_DECAY: h->params.springDecay = v; break;
    case HYDRA_DSP_PARAM_SPRING_DAMPING: h->params.springDamping = v; break;
    case HYDRA_DSP_PARAM_SPRING_MIX: h->params.springMix = v; break;
    default: return -2;
  }
  h->dirty = true;
  return 0;
}

extern "C" {
uint32_t hydra_dsp_get_api_version(void) {
  return HYDRA_DSP_API_VERSION;
}

uint32_t hydra_dsp_get_param_count(void) {
  return HYDRA_DSP_PARAM_COUNT;
}

const hydra_dsp_param_spec* hydra_dsp_get_param_specs(void) {
  return kParamSpecs;
}

int hydra_dsp_get_param_spec(hydra_dsp_param_id param_id, hydra_dsp_param_spec* out_spec) {
  if (!out_spec) return -1;
  if (param_id < 0 || param_id >= HYDRA_DSP_PARAM_COUNT) return -2;
  *out_spec = kParamSpecs[param_id];
  return 0;
}

int hydra_dsp_create(float sample_rate, float max_delay_ms, hydra_dsp_handle** out_handle) {
  if (!out_handle) return -1;
  auto* h = new (std::nothrow) hydra_dsp_handle;
  if (!h) return -2;
  h->core = new (std::nothrow) hydra::dsp::TapeCore(sample_rate, max_delay_ms);
  if (!h->core || !h->core->isValid()) { delete h->core; delete h; return -3; }
  h->params = h->core->params();
  h->dirty = false;
  *out_handle = h;
  return 0;
}

void hydra_dsp_destroy(hydra_dsp_handle* handle) {
  if (!handle) return;
  delete handle->core;
  delete handle;
}

int hydra_dsp_prepare(hydra_dsp_handle* handle, uint32_t, uint32_t channels) {
  if (!handle) return -1;
  return channels == HYDRA_DSP_CHANNELS_STEREO ? 0 : -2;
}

void hydra_dsp_reset(hydra_dsp_handle* handle) {
  if (!handle) return;
  handle->core->reset();
}

int hydra_dsp_set_parameter(hydra_dsp_handle* handle, hydra_dsp_param_id param_id, float value) {
  if (!handle) return -1;
  if (param_id == HYDRA_DSP_PARAM_RESET) {
    handle->core->reset();
    return 0;
  }
  return set_param(handle, param_id, value);
}

int hydra_dsp_get_parameter(hydra_dsp_handle* handle, hydra_dsp_param_id param_id, float* out_value) {
  if (!handle || !out_value) return -1;
  switch (param_id) {
    case HYDRA_DSP_PARAM_FLUTTER_DEPTH: *out_value = handle->params.flutterDepth; break;
    case HYDRA_DSP_PARAM_WOW_DEPTH: *out_value = handle->params.wowDepth; break;
    case HYDRA_DSP_PARAM_DROPOUT: *out_value = handle->params.dropoutSeverity; break;
    case HYDRA_DSP_PARAM_DRIVE: *out_value = handle->params.drive; break;
    case HYDRA_DSP_PARAM_NOISE: *out_value = handle->params.noise; break;
    case HYDRA_DSP_PARAM_TAPE_SPEED: *out_value = handle->params.tapeSpeed; break;
    case HYDRA_DSP_PARAM_TAPE_AGE: *out_value = handle->params.tapeAge; break;
    case HYDRA_DSP_PARAM_HEAD_BUMP: *out_value = handle->params.headBumpAmount; break;
    case HYDRA_DSP_PARAM_AZIMUTH: *out_value = handle->params.azimuthError; break;
    case HYDRA_DSP_PARAM_FLUTTER_RATE: *out_value = handle->params.flutterRate; break;
    case HYDRA_DSP_PARAM_WOW_RATE: *out_value = handle->params.wowRate; break;
    case HYDRA_DSP_PARAM_DELAY_ACTIVE: *out_value = handle->params.delayActive ? 1.0f : 0.0f; break;
    case HYDRA_DSP_PARAM_DELAY_MS: *out_value = handle->params.delayTimeMs; break;
    case HYDRA_DSP_PARAM_FEEDBACK: *out_value = handle->params.feedback; break;
    case HYDRA_DSP_PARAM_DRY_WET: *out_value = handle->params.dryWet; break;
    case HYDRA_DSP_PARAM_ACTIVE_HEADS: *out_value = (float)handle->params.activeHeads; break;
    case HYDRA_DSP_PARAM_BPM: *out_value = handle->params.bpm; break;
    case HYDRA_DSP_PARAM_HEADS_MUSICAL: *out_value = handle->params.headsMusical ? 1.0f : 0.0f; break;
    case HYDRA_DSP_PARAM_GUITAR_FOCUS: *out_value = handle->params.guitarFocus ? 1.0f : 0.0f; break;
    case HYDRA_DSP_PARAM_TONE: *out_value = handle->params.tone; break;
    case HYDRA_DSP_PARAM_PING_PONG: *out_value = handle->params.pingPong ? 1.0f : 0.0f; break;
    case HYDRA_DSP_PARAM_FREEZE: *out_value = handle->params.freeze ? 1.0f : 0.0f; break;
    case HYDRA_DSP_PARAM_REVERSE: *out_value = handle->params.reverse ? 1.0f : 0.0f; break;
    case HYDRA_DSP_PARAM_REVERSE_SMEAR: *out_value = handle->params.reverseSmear ? 1.0f : 0.0f; break;
    case HYDRA_DSP_PARAM_SPRING: *out_value = handle->params.spring ? 1.0f : 0.0f; break;
    case HYDRA_DSP_PARAM_SPRING_DECAY: *out_value = handle->params.springDecay; break;
    case HYDRA_DSP_PARAM_SPRING_DAMPING: *out_value = handle->params.springDamping; break;
    case HYDRA_DSP_PARAM_SPRING_MIX: *out_value = handle->params.springMix; break;
    case HYDRA_DSP_PARAM_RESET: *out_value = 0.0f; break;
    default: return -2;
  }
  return 0;
}

int hydra_dsp_set_params(hydra_dsp_handle* handle, const hydra_dsp_params* params) {
  if (!handle || !params) return -1;
  from_c(*params, &handle->params);
  handle->dirty = true;
  return 0;
}

int hydra_dsp_commit(hydra_dsp_handle* handle) {
  if (!handle) return -1;
  if (handle->dirty) {
    handle->core->updateParams(handle->params);
    handle->dirty = false;
  }
  return 0;
}

int hydra_dsp_process(hydra_dsp_handle* handle,
                      const float* in_l,
                      const float* in_r,
                      float* out_l,
                      float* out_r,
                      uint32_t frames) {
  if (!handle || !out_l || !out_r || !in_l || !in_r) return -1;
  if (handle->dirty) handle->core->updateParams(handle->params), handle->dirty = false;
  for (uint32_t i = 0; i < frames; ++i) {
    handle->core->processStereo(in_l[i], in_r[i], &out_l[i], &out_r[i]);
  }
  return 0;
}
}
