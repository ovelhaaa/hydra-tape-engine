#include "hydra_dsp.h"
#include "tape_core.hpp"

#include <new>

struct hydra_dsp_handle {
  hydra::dsp::TapeCore* core;
  hydra::dsp::TapeParams params;
};

static int set_param(hydra_dsp_handle* h, hydra_dsp_param_id id, float v) {
  switch (id) {
    case HYDRA_DSP_PARAM_FLUTTER_DEPTH: h->params.flutterDepth = v; break;
    case HYDRA_DSP_PARAM_WOW_DEPTH: h->params.wowDepth = v; break;
    case HYDRA_DSP_PARAM_DROPOUT: h->params.dropoutSeverity = v; break;
    case HYDRA_DSP_PARAM_DRIVE: h->params.drive = v; break;
    case HYDRA_DSP_PARAM_NOISE: h->params.noise = v; break;
    case HYDRA_DSP_PARAM_DELAY_ACTIVE: h->params.delayActive = v > 0.5f; break;
    case HYDRA_DSP_PARAM_DELAY_MS: h->params.delayTimeMs = v; break;
    case HYDRA_DSP_PARAM_FEEDBACK: h->params.feedback = v; break;
    case HYDRA_DSP_PARAM_DRY_WET: h->params.dryWet = v; break;
    default: return -2;
  }
  h->core->updateParams(h->params);
  return 0;
}

extern "C" {
int hydra_dsp_create(float sample_rate, float max_delay_ms, hydra_dsp_handle** out_handle) {
  if (!out_handle) return -1;
  auto* h = new (std::nothrow) hydra_dsp_handle;
  if (!h) return -2;
  h->core = new (std::nothrow) hydra::dsp::TapeCore(sample_rate, max_delay_ms);
  if (!h->core || !h->core->isValid()) { delete h->core; delete h; return -3; }
  h->params = h->core->params();
  *out_handle = h;
  return 0;
}

void hydra_dsp_destroy(hydra_dsp_handle* handle) {
  if (!handle) return;
  delete handle->core;
  delete handle;
}

int hydra_dsp_prepare(hydra_dsp_handle* handle, uint32_t, uint32_t) {
  return handle ? 0 : -1;
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
    case HYDRA_DSP_PARAM_DELAY_ACTIVE: *out_value = handle->params.delayActive ? 1.0f : 0.0f; break;
    case HYDRA_DSP_PARAM_DELAY_MS: *out_value = handle->params.delayTimeMs; break;
    case HYDRA_DSP_PARAM_FEEDBACK: *out_value = handle->params.feedback; break;
    case HYDRA_DSP_PARAM_DRY_WET: *out_value = handle->params.dryWet; break;
    default: return -2;
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
  for (uint32_t i = 0; i < frames; ++i) {
    handle->core->processStereo(in_l[i], in_r[i], &out_l[i], &out_r[i]);
  }
  return 0;
}
}
