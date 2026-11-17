#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hydra_dsp_handle hydra_dsp_handle;

enum { HYDRA_DSP_CHANNELS_STEREO = 2 };

enum {
  HYDRA_DSP_API_VERSION_MAJOR = 1,
  HYDRA_DSP_API_VERSION_MINOR = 5,
  HYDRA_DSP_API_VERSION_PATCH = 0,
  HYDRA_DSP_API_VERSION =
      (HYDRA_DSP_API_VERSION_MAJOR << 16) |
      (HYDRA_DSP_API_VERSION_MINOR << 8) |
      HYDRA_DSP_API_VERSION_PATCH
};

#define HYDRA_DSP_PARAMS_SIZE_V1_2 (29 * sizeof(float))
#define HYDRA_DSP_PARAMS_SIZE_V1_3 (35 * sizeof(float))
#define HYDRA_DSP_PARAMS_SIZE_V1_4 (37 * sizeof(float))

typedef struct hydra_dsp_params_v1_2 {
  float flutterDepth, wowDepth, dropoutSeverity, drive, noise;
  float tapeSpeed, tapeAge, headBumpAmount, azimuthError;
  float flutterRate, wowRate;
  float delayActive, delayTimeMs, feedback, dryWet, delayWet;
  float activeHeads, bpm, headsMusical, guitarFocus, tone;
  float pingPong, freeze, reverse, reverseSmear, spring;
  float springDecay, springDamping, springMix;
} hydra_dsp_params_v1_2;

typedef struct hydra_dsp_params {
  float flutterDepth, wowDepth, dropoutSeverity, drive, noise;
  float tapeSpeed, tapeAge, headBumpAmount, azimuthError;
  float flutterRate, wowRate;
  float delayActive, delayTimeMs, feedback, dryWet, delayWet;
  float activeHeads, bpm, headsMusical, guitarFocus, tone;
  float pingPong, freeze, reverse, reverseSmear, spring;
  float springDecay, springDamping, springMix;
  float warpActive, warpSpeed, warpGrainMs, warpStereo, warpFeedback, warpMode;
} hydra_dsp_params;

typedef enum hydra_dsp_reverse_mode {
  HYDRA_DSP_REVERSE_RHYTHMIC = 0,
  HYDRA_DSP_REVERSE_FREE = 1,
  HYDRA_DSP_REVERSE_LEGACY = 2
} hydra_dsp_reverse_mode;

typedef enum hydra_dsp_reverse_division {
  HYDRA_DSP_REVERSE_QUARTER = 0,
  HYDRA_DSP_REVERSE_WHOLE = 1,
  HYDRA_DSP_REVERSE_HALF = 2,
  HYDRA_DSP_REVERSE_EIGHTH = 3,
  HYDRA_DSP_REVERSE_SIXTEENTH = 4,
  HYDRA_DSP_REVERSE_DOTTED_QUARTER = 5,
  HYDRA_DSP_REVERSE_DOTTED_EIGHTH = 6,
  HYDRA_DSP_REVERSE_TRIPLET_QUARTER = 7,
  HYDRA_DSP_REVERSE_TRIPLET_EIGHTH = 8
} hydra_dsp_reverse_division;

typedef enum hydra_dsp_transport_playback_state {
  HYDRA_DSP_TRANSPORT_STOPPED = 0,
  HYDRA_DSP_TRANSPORT_PLAYING = 1,
  HYDRA_DSP_TRANSPORT_PAUSED = 2
} hydra_dsp_transport_playback_state;

typedef enum hydra_dsp_subdivision {
  HYDRA_DSP_SUBDIV_WHOLE = 0,
  HYDRA_DSP_SUBDIV_HALF = 1,
  HYDRA_DSP_SUBDIV_QUARTER = 2,
  HYDRA_DSP_SUBDIV_EIGHTH = 3,
  HYDRA_DSP_SUBDIV_SIXTEENTH = 4,
  HYDRA_DSP_SUBDIV_THIRTY_SECOND = 5,
  HYDRA_DSP_SUBDIV_DOTTED_QUARTER = 6,
  HYDRA_DSP_SUBDIV_DOTTED_EIGHTH = 7,
  HYDRA_DSP_SUBDIV_TRIPLET_QUARTER = 8,
  HYDRA_DSP_SUBDIV_TRIPLET_EIGHTH = 9,
  HYDRA_DSP_SUBDIV_COUNT = 10
} hydra_dsp_subdivision;

typedef struct hydra_dsp_transport_state {
  float bpm;
  float beat_phase;
  float bar_phase;
  double musical_position;
  uint64_t absolute_sample_position;
  uint64_t transport_sample_position;
  int64_t beat_index;
  int64_t bar_index;
  uint32_t time_signature_numerator;
  uint32_t time_signature_denominator;
  int playing;
  uint32_t generation;
} hydra_dsp_transport_state;

typedef struct hydra_dsp_params_v1_4 {
  hydra_dsp_params v1_3;
  float reverseMode, reverseDivision;
} hydra_dsp_params_v1_4;

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
  HYDRA_DSP_PARAM_DELAY_WET = 15,
  HYDRA_DSP_PARAM_ACTIVE_HEADS = 16,
  HYDRA_DSP_PARAM_BPM = 17,
  HYDRA_DSP_PARAM_HEADS_MUSICAL = 18,
  HYDRA_DSP_PARAM_GUITAR_FOCUS = 19,
  HYDRA_DSP_PARAM_TONE = 20,
  HYDRA_DSP_PARAM_PING_PONG = 21,
  HYDRA_DSP_PARAM_FREEZE = 22,
  HYDRA_DSP_PARAM_REVERSE = 23,
  HYDRA_DSP_PARAM_REVERSE_SMEAR = 24,
  HYDRA_DSP_PARAM_SPRING = 25,
  HYDRA_DSP_PARAM_SPRING_DECAY = 26,
  HYDRA_DSP_PARAM_SPRING_DAMPING = 27,
  HYDRA_DSP_PARAM_SPRING_MIX = 28,
  HYDRA_DSP_PARAM_RESET = 29,
  HYDRA_DSP_PARAM_WARP_ACTIVE = 30,
  HYDRA_DSP_PARAM_WARP_SPEED = 31,
  HYDRA_DSP_PARAM_WARP_GRAIN_MS = 32,
  HYDRA_DSP_PARAM_WARP_STEREO = 33,
  HYDRA_DSP_PARAM_WARP_FEEDBACK = 34,
  HYDRA_DSP_PARAM_WARP_MODE = 35,
  HYDRA_DSP_PARAM_REVERSE_MODE = 36,
  HYDRA_DSP_PARAM_REVERSE_DIVISION = 37,
  HYDRA_DSP_PARAM_COUNT = 38
} hydra_dsp_param_id;

typedef enum hydra_dsp_smoothing_policy {
  HYDRA_DSP_SMOOTHING_NONE = 0,
  HYDRA_DSP_SMOOTHING_BLOCK_EDGE = 1,
  HYDRA_DSP_SMOOTHING_PER_SAMPLE_INTERNAL = 2
} hydra_dsp_smoothing_policy;

enum {
  HYDRA_DSP_PARAM_SPEC_FLAG_NONE = 0,
  HYDRA_DSP_PARAM_SPEC_FLAG_RUNTIME_MAX = 1u << 0
};

typedef struct hydra_dsp_param_spec {
  hydra_dsp_param_id id;
  const char* name;
  float min_value;
  float max_value;
  float default_value;
  hydra_dsp_smoothing_policy smoothing;
  uint32_t flags;
} hydra_dsp_param_spec;

uint32_t hydra_dsp_get_api_version(void);
uint32_t hydra_dsp_get_param_count(void);
const hydra_dsp_param_spec* hydra_dsp_get_param_specs(void);
int hydra_dsp_get_param_spec(hydra_dsp_param_id param_id, hydra_dsp_param_spec* out_spec);
int hydra_dsp_get_param_spec_for_handle(const hydra_dsp_handle* handle,
                                        hydra_dsp_param_id param_id,
                                        hydra_dsp_param_spec* out_spec);

int hydra_dsp_create(float sample_rate, float max_delay_ms, hydra_dsp_handle** out_handle);
/* Opt-in shared history capacity for longer reverse windows. Allocates only at create. */
int hydra_dsp_create_v1_4(float sample_rate, float max_delay_ms, float history_ms,
                          hydra_dsp_handle** out_handle);
void hydra_dsp_destroy(hydra_dsp_handle* handle);
// channels must be HYDRA_DSP_CHANNELS_STEREO.
int hydra_dsp_prepare(hydra_dsp_handle* handle, uint32_t max_frames, uint32_t channels);
void hydra_dsp_reset(hydra_dsp_handle* handle);
int hydra_dsp_set_parameter(hydra_dsp_handle* handle, hydra_dsp_param_id param_id, float value);
int hydra_dsp_get_parameter(hydra_dsp_handle* handle, hydra_dsp_param_id param_id, float* out_value);
/* Legacy ABI: reads the 116-byte API 1.2 structure and resets Warp to defaults. */
int hydra_dsp_set_params(hydra_dsp_handle* handle, const hydra_dsp_params_v1_2* params);

/* Full 140-byte API 1.3 structure, including Warp. */
int hydra_dsp_set_params_v1_3(hydra_dsp_handle* handle, const hydra_dsp_params* params);

/**
 * @brief Sets DSP parameters for callers using legacy API 1.2 structure (116 bytes).
 *
 * Guarantees that warp parameters safely retain default inactive values without
 * reading out-of-bounds from the caller's buffer.
 */
int hydra_dsp_set_params_v1_2(hydra_dsp_handle* handle, const hydra_dsp_params_v1_2* params);

/**
 * @brief Sets DSP parameters with explicit struct size for backward and forward ABI safety.
 *
 * Sizes below V1.4 select legacy Reverse with the quarter-note division as default.
 * If struct_size is less than HYDRA_DSP_PARAMS_SIZE_V1_3 (140 bytes), parameters introduced
 * in API 1.3 (such as warp parameters) safely default without out-of-bounds reads.
 *
 * @param handle Handle to DSP instance
 * @param params Pointer to parameter structure buffer
 * @param struct_size Size in bytes of caller's structure buffer
 * @return 0 on success, -1 on null handle/params, -2 on struct_size < HYDRA_DSP_PARAMS_SIZE_V1_2
 */
int hydra_dsp_set_params_sized(hydra_dsp_handle* handle, const void* params, size_t struct_size);
int hydra_dsp_commit(hydra_dsp_handle* handle);

/**
 * @brief Retrieves the effective reverse playback window duration and whether it was clamped by buffer limits.
 *
 * @param handle Handle to DSP instance
 * @param out_effective_ms Output pointer to receive effective window length in milliseconds (may be NULL)
 * @param out_is_clamped Output pointer to receive 1 if window duration was clamped by history capacity, 0 otherwise (may be NULL)
 * @return 0 on success, -1 on null handle
 */
int hydra_dsp_get_reverse_status(const hydra_dsp_handle* handle, float* out_effective_ms, int* out_is_clamped);

/**
 * @brief Retrieves detailed reverse window timing diagnostics including requested vs effective duration and max history.
 *
 * @param handle Handle to DSP instance
 * @param out_requested_ms Output pointer for nominal requested duration in ms (may be NULL)
 * @param out_effective_ms Output pointer for effective applied duration in ms (may be NULL)
 * @param out_is_clamped Output pointer for clamp flag (1 if clamped, 0 otherwise, may be NULL)
 * @param out_max_history_ms Output pointer for total available history capacity in ms (may be NULL)
 * @return 0 on success, -1 on null handle
 */
int hydra_dsp_get_reverse_diagnostics(const hydra_dsp_handle* handle,
                                      float* out_requested_ms,
                                      float* out_effective_ms,
                                      int* out_is_clamped,
                                      float* out_max_history_ms);

int hydra_dsp_transport_play(hydra_dsp_handle* handle);
int hydra_dsp_transport_pause(hydra_dsp_handle* handle);
int hydra_dsp_transport_stop(hydra_dsp_handle* handle);
int hydra_dsp_transport_reset(hydra_dsp_handle* handle);
int hydra_dsp_transport_locate(hydra_dsp_handle* handle, double musical_pos, uint64_t sample_pos);
int hydra_dsp_transport_set_bpm(hydra_dsp_handle* handle, float bpm);
int hydra_dsp_transport_set_time_signature(hydra_dsp_handle* handle, uint32_t numerator, uint32_t denominator);
int hydra_dsp_transport_get_state(const hydra_dsp_handle* handle, hydra_dsp_transport_state* out_state);

int hydra_dsp_process(hydra_dsp_handle* handle,
                      const float* in_l,
                      const float* in_r,
                      float* out_l,
                      float* out_r,
                      uint32_t frames);

#ifdef __cplusplus
}
#endif
