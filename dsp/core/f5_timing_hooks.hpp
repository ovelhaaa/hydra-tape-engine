#pragma once

// Test-only C view of the F5 timing diagnostics. Compiled only when
// HYDRA_DSP_TEST_HOOKS is defined for the measurement library. It mirrors
// TapeCore::TimingDiagnostics and never changes DSP state or output.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hydra_f5_timing_state {
  double transport_bpm;
  double musical_position;
  uint64_t absolute_sample_position;
  uint64_t transport_sample_position;
  float target_beat_ms;
  float effective_beat_ms;
  float head_delay_samples[3];
  float target_head_samples[3];
  float smoothed_delay_samples;
  float target_delay_samples;
  float reverse_requested_ms;
  float reverse_effective_ms;
  int32_t reverse_clamped;
  int32_t musical;
  int32_t gliding;
  int32_t variant;
} hydra_f5_timing_state;

struct hydra_dsp_handle;
int hydra_dsp_test_timing_state(const struct hydra_dsp_handle* handle,
                                hydra_f5_timing_state* out);

#ifdef __cplusplus
}
#endif
