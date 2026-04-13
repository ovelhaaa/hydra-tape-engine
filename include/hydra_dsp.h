#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hydra_dsp_handle hydra_dsp_handle;

enum { HYDRA_DSP_CHANNELS_STEREO = 2 };

typedef struct hydra_dsp_params {
  float flutterDepth, wowDepth, dropoutSeverity, drive, noise;
  float tapeSpeed, tapeAge, headBumpAmount, azimuthError;
  float flutterRate, wowRate;
  float delayActive, delayTimeMs, feedback, dryWet;
  float activeHeads, bpm, headsMusical, guitarFocus, tone;
  float pingPong, freeze, reverse, reverseSmear, spring;
  float springDecay, springDamping, springMix;
} hydra_dsp_params;

typedef enum hydra_dsp_param_id {
  HYDRA_DSP_PARAM_FLUTTER_DEPTH = 0,
  HYDRA_DSP_PARAM_WOW_DEPTH,
  HYDRA_DSP_PARAM_DROPOUT,
  HYDRA_DSP_PARAM_DRIVE,
  HYDRA_DSP_PARAM_NOISE,
  HYDRA_DSP_PARAM_TAPE_SPEED,
  HYDRA_DSP_PARAM_TAPE_AGE,
  HYDRA_DSP_PARAM_HEAD_BUMP,
  HYDRA_DSP_PARAM_AZIMUTH,
  HYDRA_DSP_PARAM_FLUTTER_RATE,
  HYDRA_DSP_PARAM_WOW_RATE,
  HYDRA_DSP_PARAM_DELAY_ACTIVE,
  HYDRA_DSP_PARAM_DELAY_MS,
  HYDRA_DSP_PARAM_FEEDBACK,
  HYDRA_DSP_PARAM_DRY_WET,
  HYDRA_DSP_PARAM_ACTIVE_HEADS,
  HYDRA_DSP_PARAM_BPM,
  HYDRA_DSP_PARAM_HEADS_MUSICAL,
  HYDRA_DSP_PARAM_GUITAR_FOCUS,
  HYDRA_DSP_PARAM_TONE,
  HYDRA_DSP_PARAM_PING_PONG,
  HYDRA_DSP_PARAM_FREEZE,
  HYDRA_DSP_PARAM_REVERSE,
  HYDRA_DSP_PARAM_REVERSE_SMEAR,
  HYDRA_DSP_PARAM_SPRING,
  HYDRA_DSP_PARAM_SPRING_DECAY,
  HYDRA_DSP_PARAM_SPRING_DAMPING,
  HYDRA_DSP_PARAM_SPRING_MIX,
  HYDRA_DSP_PARAM_RESET
} hydra_dsp_param_id;

int hydra_dsp_create(float sample_rate, float max_delay_ms, hydra_dsp_handle** out_handle);
void hydra_dsp_destroy(hydra_dsp_handle* handle);
// channels must be HYDRA_DSP_CHANNELS_STEREO.
int hydra_dsp_prepare(hydra_dsp_handle* handle, uint32_t max_frames, uint32_t channels);
void hydra_dsp_reset(hydra_dsp_handle* handle);
int hydra_dsp_set_parameter(hydra_dsp_handle* handle, hydra_dsp_param_id param_id, float value);
int hydra_dsp_get_parameter(hydra_dsp_handle* handle, hydra_dsp_param_id param_id, float* out_value);
int hydra_dsp_set_params(hydra_dsp_handle* handle, const hydra_dsp_params* params);
int hydra_dsp_commit(hydra_dsp_handle* handle);
int hydra_dsp_process(hydra_dsp_handle* handle,
                      const float* in_l,
                      const float* in_r,
                      float* out_l,
                      float* out_r,
                      uint32_t frames);

#ifdef __cplusplus
}
#endif
