#include "hydra_dsp.h"

#include <cassert>
#include <vector>

int main() {
  assert(hydra_dsp_get_api_version() == HYDRA_DSP_API_VERSION);
  assert(HYDRA_DSP_API_VERSION_MINOR == 1);
  assert(hydra_dsp_get_param_count() == HYDRA_DSP_PARAM_COUNT);

  hydra_dsp_param_spec spec{};
  assert(hydra_dsp_get_param_spec(HYDRA_DSP_PARAM_DELAY_MS, &spec) == 0);
  assert(spec.id == HYDRA_DSP_PARAM_DELAY_MS);
  assert(spec.min_value == 10.0f);
  assert(spec.max_value == 2000.0f);
  assert((spec.flags & HYDRA_DSP_PARAM_SPEC_FLAG_RUNTIME_MAX) != 0u);

  hydra_dsp_handle* h = nullptr;
  assert(hydra_dsp_create(44100.0f, 2000.0f, &h) == 0);
  assert(h != nullptr);
  assert(hydra_dsp_prepare(h, 32, HYDRA_DSP_CHANNELS_STEREO) == 0);

  hydra_dsp_param_spec runtimeSpec{};
  assert(hydra_dsp_get_param_spec_for_handle(h, HYDRA_DSP_PARAM_DELAY_MS, &runtimeSpec) == 0);
  assert(runtimeSpec.max_value == 2000.0f);

  hydra_dsp_params params{};
  params.delayActive = 1.0f;
  params.dryWet = 50.0f;
  params.feedback = 40.0f;
  params.activeHeads = 7.0f;
  params.headsMusical = 1.0f;
  params.bpm = 120.0f;
  assert(hydra_dsp_set_params(h, &params) == 0);
  assert(hydra_dsp_commit(h) == 0);

  std::vector<float> inL(8, 0.1f), inR(8, -0.1f), outL(8), outR(8);
  assert(hydra_dsp_process(h, inL.data(), inR.data(), outL.data(), outR.data(), 8) == 0);

  hydra_dsp_reset(h);
  assert(hydra_dsp_process(h, inL.data(), inR.data(), outL.data(), outR.data(), 8) == 0);

  float v = 0.0f;
  assert(hydra_dsp_get_parameter(h, HYDRA_DSP_PARAM_DRY_WET, &v) == 0);

  hydra_dsp_destroy(h);
  return 0;
}
