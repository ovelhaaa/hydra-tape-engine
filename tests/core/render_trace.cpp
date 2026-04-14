#include "hydra_dsp.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <vector>

int main() {
  constexpr float sampleRate = 48000.0f;
  constexpr uint32_t block = 32;
  constexpr uint32_t totalFrames = 2048;

  hydra_dsp_handle* h = nullptr;
  assert(hydra_dsp_create(sampleRate, 2000.0f, &h) == 0);
  assert(hydra_dsp_prepare(h, block, HYDRA_DSP_CHANNELS_STEREO) == 0);

  hydra_dsp_params p{};
  p.flutterDepth = 12.0f;
  p.wowDepth = 8.0f;
  p.dropoutSeverity = 10.0f;
  p.drive = 15.0f;
  p.noise = 0.0f;
  p.tapeSpeed = 50.0f;
  p.tapeAge = 35.0f;
  p.headBumpAmount = 20.0f;
  p.azimuthError = 15.0f;
  p.flutterRate = 5.0f;
  p.wowRate = 0.7f;
  p.delayActive = 1.0f;
  p.delayTimeMs = 280.0f;
  p.feedback = 33.0f;
  p.dryWet = 42.0f;
  p.activeHeads = 7.0f;
  p.bpm = 120.0f;
  p.headsMusical = 0.0f;
  p.guitarFocus = 0.0f;
  p.tone = 55.0f;
  p.pingPong = 0.0f;
  p.freeze = 0.0f;
  p.reverse = 0.0f;
  p.reverseSmear = 0.0f;
  p.spring = 0.0f;
  p.springDecay = 60.0f;
  p.springDamping = 45.0f;
  p.springMix = 0.0f;

  assert(hydra_dsp_set_params(h, &p) == 0);
  assert(hydra_dsp_commit(h) == 0);

  std::vector<float> inL(totalFrames), inR(totalFrames), outL(totalFrames), outR(totalFrames);
  for (uint32_t i = 0; i < totalFrames; ++i) {
    const float t = static_cast<float>(i) / sampleRate;
    inL[i] = 0.45f * std::sin(2.0f * 3.1415926535f * 220.0f * t) + 0.2f * std::sin(2.0f * 3.1415926535f * 330.0f * t);
    inR[i] = 0.9f * inL[i];
  }

  for (uint32_t start = 0; start < totalFrames; start += block) {
    if (start == 512) {
      assert(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_DRY_WET, 55.0f) == 0);
      assert(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_FEEDBACK, 40.0f) == 0);
      assert(hydra_dsp_commit(h) == 0);
    }
    if (start == 1024) {
      assert(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_REVERSE, 1.0f) == 0);
      assert(hydra_dsp_commit(h) == 0);
    }
    if (start == 1536) {
      assert(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_REVERSE, 0.0f) == 0);
      assert(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_HEADS_MUSICAL, 1.0f) == 0);
      assert(hydra_dsp_commit(h) == 0);
    }

    assert(hydra_dsp_process(h, inL.data() + start, inR.data() + start, outL.data() + start, outR.data() + start, block) == 0);
  }

  hydra_dsp_destroy(h);

  std::cout << std::fixed << std::setprecision(9);
  for (uint32_t i = 0; i < totalFrames; ++i) {
    std::cout << outL[i] << ',' << outR[i] << '\n';
  }

  return 0;
}
