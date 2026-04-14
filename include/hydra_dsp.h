#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hydra_dsp_handle hydra_dsp_handle;

enum { HYDRA_DSP_CHANNELS_STEREO = 2 };

enum {
  HYDRA_DSP_API_VERSION_MAJOR = 1,
  HYDRA_DSP_API_VERSION_MINOR = 0,
  HYDRA_DSP_API_VERSION_PATCH = 0,
  HYDRA_DSP_API_VERSION =
      (HYDRA_DSP_API_VERSION_MAJOR << 16) |
      (HYDRA_DSP_API_VERSION_MINOR << 8) |
      HYDRA_DSP_API_VERSION_PATCH
};

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
  HYDRA_DSP_PARAM_WOW_DEPTH = 1,
  HYDRA_DSP_PARAM_DROPOUT = 2,
  HYDRA_DSP_PARAM_DRIVE = 3,
  HYDRA_DSP_PARAM_NOISE = 4,
  HYDRA_DSP_PARAM_TAPE_SPEED = 5,
  HYDRA_DSP_PARAM_TAPE_AGE = 6,
  HYDRA_DSP_PARAM_HEAD_BUMP = 7,
  HYDRA_DSP_PARAM_AZIMUTH = 8,
  HYDRA_DSP_PARAM_FLUTTER_RATE = 9,
  HYDRA_DSP_PARAM_WOW_RATE = 10,
  HYDRA_DSP_PARAM_DELAY_ACTIVE = 11,
  HYDRA_DSP_PARAM_DELAY_MS = 12,
  HYDRA_DSP_PARAM_FEEDBACK = 13,
  HYDRA_DSP_PARAM_DRY_WET = 14,
  HYDRA_DSP_PARAM_ACTIVE_HEADS = 15,
  HYDRA_DSP_PARAM_BPM = 16,
  HYDRA_DSP_PARAM_HEADS_MUSICAL = 17,
  HYDRA_DSP_PARAM_GUITAR_FOCUS = 18,
  HYDRA_DSP_PARAM_TONE = 19,
  HYDRA_DSP_PARAM_PING_PONG = 20,
  HYDRA_DSP_PARAM_FREEZE = 21,
  HYDRA_DSP_PARAM_REVERSE = 22,
  HYDRA_DSP_PARAM_REVERSE_SMEAR = 23,
  HYDRA_DSP_PARAM_SPRING = 24,
  HYDRA_DSP_PARAM_SPRING_DECAY = 25,
  HYDRA_DSP_PARAM_SPRING_DAMPING = 26,
  HYDRA_DSP_PARAM_SPRING_MIX = 27,
  HYDRA_DSP_PARAM_RESET = 28,
  HYDRA_DSP_PARAM_COUNT = 29
} hydra_dsp_param_id;

typedef enum hydra_dsp_smoothing_policy {
  HYDRA_DSP_SMOOTHING_NONE = 0,
  HYDRA_DSP_SMOOTHING_BLOCK_EDGE = 1,
  HYDRA_DSP_SMOOTHING_PER_SAMPLE_INTERNAL = 2
} hydra_dsp_smoothing_policy;

typedef struct hydra_dsp_param_spec {
  hydra_dsp_param_id id;
  const char* name;
  float min_value;
  float max_value;
  float default_value;
  hydra_dsp_smoothing_policy smoothing;
} hydra_dsp_param_spec;

uint32_t hydra_dsp_get_api_version(void);
uint32_t hydra_dsp_get_param_count(void);
const hydra_dsp_param_spec* hydra_dsp_get_param_specs(void);
int hydra_dsp_get_param_spec(hydra_dsp_param_id param_id, hydra_dsp_param_spec* out_spec);

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
