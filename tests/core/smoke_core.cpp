#include "hydra_dsp.h"

#include <cassert>
#include <vector>

int main() {
  hydra_dsp_handle* h = nullptr;
  assert(hydra_dsp_create(44100.0f, 2000.0f, &h) == 0);
  assert(h != nullptr);
  assert(hydra_dsp_prepare(h, 32, 2) == 0);

  std::vector<float> inL(8, 0.1f), inR(8, -0.1f), outL(8), outR(8);
  assert(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_DELAY_ACTIVE, 1.0f) == 0);
  assert(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_DRY_WET, 50.0f) == 0);
  assert(hydra_dsp_process(h, inL.data(), inR.data(), outL.data(), outR.data(), 8) == 0);

  hydra_dsp_reset(h);
  assert(hydra_dsp_process(h, inL.data(), inR.data(), outL.data(), outR.data(), 8) == 0);

  float v = 0.0f;
  assert(hydra_dsp_get_parameter(h, HYDRA_DSP_PARAM_DRY_WET, &v) == 0);
  assert(v == 50.0f);

  hydra_dsp_destroy(h);
  return 0;
}
