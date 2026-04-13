#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hydra_dsp_handle hydra_dsp_handle;

typedef enum hydra_dsp_param_id {
  HYDRA_DSP_PARAM_FLUTTER_DEPTH = 0,
  HYDRA_DSP_PARAM_WOW_DEPTH,
  HYDRA_DSP_PARAM_DROPOUT,
  HYDRA_DSP_PARAM_DRIVE,
  HYDRA_DSP_PARAM_NOISE,
  HYDRA_DSP_PARAM_DELAY_ACTIVE,
  HYDRA_DSP_PARAM_DELAY_MS,
  HYDRA_DSP_PARAM_FEEDBACK,
  HYDRA_DSP_PARAM_DRY_WET,
  HYDRA_DSP_PARAM_RESET
} hydra_dsp_param_id;

int hydra_dsp_create(float sample_rate, float max_delay_ms, hydra_dsp_handle** out_handle);
void hydra_dsp_destroy(hydra_dsp_handle* handle);
int hydra_dsp_prepare(hydra_dsp_handle* handle, uint32_t max_frames, uint32_t channels);
void hydra_dsp_reset(hydra_dsp_handle* handle);
int hydra_dsp_set_parameter(hydra_dsp_handle* handle, hydra_dsp_param_id param_id, float value);
int hydra_dsp_get_parameter(hydra_dsp_handle* handle, hydra_dsp_param_id param_id, float* out_value);
int hydra_dsp_process(hydra_dsp_handle* handle,
                      const float* in_l,
                      const float* in_r,
                      float* out_l,
                      float* out_r,
                      uint32_t frames);

#ifdef __cplusplus
}
#endif
