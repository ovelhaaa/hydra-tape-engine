// Compiled with the original API 1.2 header from e4cae43.
#include "hydra_dsp.h"

#include <stdlib.h>

int main(void) {
  hydra_dsp_handle* handle = NULL;
  hydra_dsp_params* params = calloc(1, sizeof(*params));
  float value = 0.0f;
  if (!params || sizeof(*params) != 116 ||
      hydra_dsp_create(48000.0f, 2000.0f, &handle) != 0) {
    free(params);
    return 1;
  }

  params->drive = 20.0f;
  params->springMix = 37.0f;
  const int ok = hydra_dsp_set_params(handle, params) == 0 &&
                 hydra_dsp_commit(handle) == 0 &&
                 hydra_dsp_get_parameter(handle, HYDRA_DSP_PARAM_SPRING_MIX, &value) == 0 &&
                 value == 37.0f;
  free(params);
  hydra_dsp_destroy(handle);
  return ok ? 0 : 2;
}
