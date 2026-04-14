#include "hydra_dsp.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {

struct Scenario {
  std::string name;
  float delay_time_ms;
  float feedback;
  float drive;
  float tone;
  float flutter_depth;
  float wow_depth;
  float flutter_rate;
  float wow_rate;
  float dry_wet;
  bool enable_reverse_pulse;
  bool enable_musical_heads_tail;
};

const std::array<Scenario, 5> kScenarios{ {
    {"baseline_glue", 280.0f, 33.0f, 15.0f, 55.0f, 12.0f, 8.0f, 5.0f, 0.7f, 42.0f, true, true},
    {"short_slap_bright", 95.0f, 18.0f, 8.0f, 72.0f, 5.0f, 2.0f, 6.0f, 1.2f, 30.0f, false, false},
    {"feedback_edge", 440.0f, 72.0f, 22.0f, 48.0f, 14.0f, 9.0f, 4.5f, 0.6f, 58.0f, true, false},
    {"saturated_dark", 360.0f, 48.0f, 62.0f, 28.0f, 9.0f, 4.0f, 3.0f, 0.5f, 64.0f, false, true},
    {"modulated_space", 620.0f, 52.0f, 18.0f, 61.0f, 24.0f, 15.0f, 8.0f, 1.8f, 50.0f, true, true},
} };

const Scenario* find_scenario(const std::string& name) {
  for (const auto& scenario : kScenarios) {
    if (scenario.name == name) {
      return &scenario;
    }
  }
  return nullptr;
}

void print_usage(const char* argv0) {
  std::cerr << "Usage: " << argv0 << " [--scenario <name>] [--list-scenarios]" << '\n';
}

}  // namespace

int main(int argc, char** argv) {
  std::string scenario_name = "baseline_glue";

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--scenario") {
      if (i + 1 >= argc) {
        print_usage(argv[0]);
        return 2;
      }
      scenario_name = argv[++i];
      continue;
    }
    if (arg == "--list-scenarios") {
      for (const auto& scenario : kScenarios) {
        std::cout << scenario.name << '\n';
      }
      return 0;
    }
    print_usage(argv[0]);
    return 2;
  }

  const Scenario* scenario = find_scenario(scenario_name);
  if (scenario == nullptr) {
    std::cerr << "unknown scenario: " << scenario_name << '\n';
    return 2;
  }

  constexpr float sampleRate = 48000.0f;
  constexpr uint32_t block = 32;
  constexpr uint32_t totalFrames = 2048;

  hydra_dsp_handle* h = nullptr;
  assert(hydra_dsp_create(sampleRate, 2000.0f, &h) == 0);
  assert(hydra_dsp_prepare(h, block, HYDRA_DSP_CHANNELS_STEREO) == 0);

  hydra_dsp_params p{};
  p.flutterDepth = scenario->flutter_depth;
  p.wowDepth = scenario->wow_depth;
  p.dropoutSeverity = 10.0f;
  p.drive = scenario->drive;
  p.noise = 0.0f;
  p.tapeSpeed = 50.0f;
  p.tapeAge = 35.0f;
  p.headBumpAmount = 20.0f;
  p.azimuthError = 15.0f;
  p.flutterRate = scenario->flutter_rate;
  p.wowRate = scenario->wow_rate;
  p.delayActive = 1.0f;
  p.delayTimeMs = scenario->delay_time_ms;
  p.feedback = scenario->feedback;
  p.dryWet = scenario->dry_wet;
  p.activeHeads = 7.0f;
  p.bpm = 120.0f;
  p.headsMusical = 0.0f;
  p.guitarFocus = 0.0f;
  p.tone = scenario->tone;
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
      assert(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_DRY_WET, scenario->dry_wet + 8.0f) == 0);
      assert(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_FEEDBACK, scenario->feedback + 7.0f) == 0);
      assert(hydra_dsp_commit(h) == 0);
    }
    if (start == 1024 && scenario->enable_reverse_pulse) {
      assert(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_REVERSE, 1.0f) == 0);
      assert(hydra_dsp_commit(h) == 0);
    }
    if (start == 1536) {
      if (scenario->enable_reverse_pulse) {
        assert(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_REVERSE, 0.0f) == 0);
      }
      if (scenario->enable_musical_heads_tail) {
        assert(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_HEADS_MUSICAL, 1.0f) == 0);
      }
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
