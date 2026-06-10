#include "hydra_dsp.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

constexpr float kSampleRate = 48000.0f;
constexpr float kMaxDelayMs = 2000.0f;
constexpr uint32_t kFrames = 64;

hydra_dsp_handle* make_handle() {
  hydra_dsp_handle* h = nullptr;
  assert(hydra_dsp_create(kSampleRate, kMaxDelayMs, &h) == 0);
  assert(h != nullptr);
  assert(hydra_dsp_prepare(h, kFrames, HYDRA_DSP_CHANNELS_STEREO) == 0);
  return h;
}

void configure_deterministic(hydra_dsp_handle* h) {
  hydra_dsp_params p{};
  p.flutterDepth = 0.0f;
  p.wowDepth = 0.0f;
  p.dropoutSeverity = 0.0f;
  p.drive = 0.0f;
  p.noise = 0.0f;
  p.tapeSpeed = 50.0f;
  p.tapeAge = 0.0f;
  p.headBumpAmount = 0.0f;
  p.azimuthError = 0.0f;
  p.flutterRate = 0.1f;
  p.wowRate = 0.1f;
  p.delayActive = 0.0f;
  p.delayTimeMs = 250.0f;
  p.feedback = 0.0f;
  p.dryWet = 0.0f;
  p.activeHeads = 7.0f;
  p.bpm = 120.0f;
  p.headsMusical = 0.0f;
  p.guitarFocus = 0.0f;
  p.tone = 100.0f;
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
}


void configure_silent_wet_delay(hydra_dsp_handle* h, float drive = 0.0f) {
  hydra_dsp_params p{};
  p.flutterDepth = 0.0f;
  p.wowDepth = 0.0f;
  p.dropoutSeverity = 0.0f;
  p.drive = drive;
  p.noise = 0.0f;
  p.tapeSpeed = 50.0f;
  p.tapeAge = 0.0f;
  p.headBumpAmount = 0.0f;
  p.azimuthError = 0.0f;
  p.flutterRate = 0.1f;
  p.wowRate = 0.1f;
  p.delayActive = 1.0f;
  p.delayTimeMs = 10.0f;
  p.feedback = 0.0f;
  p.dryWet = 100.0f;
  p.activeHeads = 4.0f;
  p.bpm = 120.0f;
  p.headsMusical = 0.0f;
  p.guitarFocus = 0.0f;
  p.tone = 100.0f;
  p.pingPong = 0.0f;
  p.freeze = 0.0f;
  p.reverse = 0.0f;
  p.reverseSmear = 0.0f;
  p.spring = 0.0f;
  p.springDecay = 0.0f;
  p.springDamping = 0.0f;
  p.springMix = 0.0f;
  assert(hydra_dsp_set_params(h, &p) == 0);
  assert(hydra_dsp_commit(h) == 0);
}

float process_peak(hydra_dsp_handle* h, const std::vector<float>& inL, const std::vector<float>& inR) {
  std::vector<float> outL(inL.size());
  std::vector<float> outR(inR.size());
  assert(hydra_dsp_process(h, inL.data(), inR.data(), outL.data(), outR.data(), inL.size()) == 0);

  float peak = 0.0f;
  for (size_t i = 0; i < outL.size(); ++i) {
    peak = std::max(peak, std::fabs(outL[i]));
    peak = std::max(peak, std::fabs(outR[i]));
  }
  return peak;
}


void configure_ping_pong_delay(hydra_dsp_handle* h, bool pingPong) {
  hydra_dsp_params p{};
  p.flutterDepth = 0.0f;
  p.wowDepth = 0.0f;
  p.dropoutSeverity = 0.0f;
  p.drive = 40.0f;
  p.noise = 0.0f;
  p.tapeSpeed = 50.0f;
  p.tapeAge = 0.0f;
  p.headBumpAmount = 0.0f;
  p.azimuthError = 0.0f;
  p.flutterRate = 0.1f;
  p.wowRate = 0.1f;
  p.delayActive = 1.0f;
  p.delayTimeMs = 10.0f;
  p.feedback = 75.0f;
  p.dryWet = 100.0f;
  p.delayWet = 100.0f;
  p.activeHeads = 4.0f;
  p.bpm = 120.0f;
  p.headsMusical = 0.0f;
  p.guitarFocus = 0.0f;
  p.tone = 100.0f;
  p.pingPong = pingPong ? 1.0f : 0.0f;
  p.freeze = 0.0f;
  p.reverse = 0.0f;
  p.reverseSmear = 0.0f;
  p.spring = 0.0f;
  p.springDecay = 0.0f;
  p.springDamping = 0.0f;
  p.springMix = 0.0f;
  assert(hydra_dsp_set_params(h, &p) == 0);
  assert(hydra_dsp_commit(h) == 0);
}

float channel_energy(const std::vector<float>& v, size_t begin, size_t end) {
  float energy = 0.0f;
  end = std::min(end, v.size());
  for (size_t i = begin; i < end; ++i) energy += v[i] * v[i];
  return energy;
}

void test_ping_pong_cross_feedback_moves_repeat_to_opposite_channel() {
  hydra_dsp_handle* h = make_handle();
  configure_ping_pong_delay(h, true);

  std::vector<float> inL(1600, 0.0f), inR(1600, 0.0f), outL(1600), outR(1600);
  inL[0] = 1.0f;
  assert(hydra_dsp_process(h, inL.data(), inR.data(), outL.data(), outR.data(), inL.size()) == 0);

  const float firstEchoL = channel_energy(outL, 430, 540);
  const float firstEchoR = channel_energy(outR, 430, 540);
  const float secondEchoL = channel_energy(outL, 900, 1040);
  const float secondEchoR = channel_energy(outR, 900, 1040);

  assert(firstEchoL > firstEchoR * 100.0f);
  assert(secondEchoR > firstEchoR * 5.0f);
  assert(secondEchoR > 1e-5f);

  hydra_dsp_destroy(h);
}

void test_known_buffer() {
  hydra_dsp_handle* h = make_handle();
  configure_deterministic(h);

  std::array<float, 16> inL{};
  std::array<float, 16> inR{};
  for (size_t i = 0; i < inL.size(); ++i) {
    inL[i] = (static_cast<float>(i) - 8.0f) * 0.05f;
    inR[i] = -inL[i] * 0.75f;
  }

  std::array<float, 16> outL{};
  std::array<float, 16> outR{};
  assert(hydra_dsp_process(h, inL.data(), inR.data(), outL.data(), outR.data(), inL.size()) == 0);

  constexpr std::array<float, 16> expectedL = {
      -0.4f, -0.35f, -0.3f, -0.25f, -0.2f, -0.15f, -0.1f, -0.05f,
      0.0f, 0.05f, 0.1f, 0.15f, 0.2f, 0.25f, 0.3f, 0.35f,
  };
  constexpr std::array<float, 16> expectedR = {
      0.3f, 0.2625f, 0.225f, 0.1875f, 0.15f, 0.1125f, 0.075f, 0.0375f,
      0.0f, -0.0375f, -0.075f, -0.1125f, -0.15f, -0.1875f, -0.225f, -0.2625f,
  };

  for (size_t i = 0; i < expectedL.size(); ++i) {
    assert(std::fabs(outL[i] - expectedL[i]) < 1e-7f);
    assert(std::fabs(outR[i] - expectedR[i]) < 1e-7f);
  }

  hydra_dsp_destroy(h);
}

void test_reset_state() {
  hydra_dsp_handle* h = make_handle();
  configure_deterministic(h);

  std::vector<float> inL(kFrames, 0.0f), inR(kFrames, 0.0f);
  inL[0] = 1.0f;
  inR[0] = -1.0f;

  std::vector<float> outAL(kFrames), outAR(kFrames), outBL(kFrames), outBR(kFrames);
  assert(hydra_dsp_process(h, inL.data(), inR.data(), outAL.data(), outAR.data(), kFrames) == 0);

  hydra_dsp_reset(h);
  assert(hydra_dsp_process(h, inL.data(), inR.data(), outBL.data(), outBR.data(), kFrames) == 0);

  for (uint32_t i = 0; i < kFrames; ++i) {
    assert(std::fabs(outAL[i] - outBL[i]) < 1e-7f);
    assert(std::fabs(outAR[i] - outBR[i]) < 1e-7f);
  }

  hydra_dsp_destroy(h);
}

void test_parameter_automation() {
  hydra_dsp_handle* h = make_handle();
  configure_deterministic(h);

  std::array<float, 8> inputL{};
  std::array<float, 8> inputR{};
  inputL.fill(0.5f);
  inputR.fill(-0.5f);

  std::array<float, 8> outL{};
  std::array<float, 8> outR{};

  const std::array<float, 3> dryWetValues = {0.0f, 50.0f, 100.0f};
  std::array<float, 3> firstSampleAbs{};

  for (size_t i = 0; i < dryWetValues.size(); ++i) {
    assert(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_DRY_WET, dryWetValues[i]) == 0);
    assert(hydra_dsp_commit(h) == 0);
    assert(hydra_dsp_process(h, inputL.data(), inputR.data(), outL.data(), outR.data(), inputL.size()) == 0);
    firstSampleAbs[i] = std::fabs(outL[0]);
  }

  // Delay remains disabled, so increasing dry/wet should reduce dry contribution.
  assert(firstSampleAbs[0] > firstSampleAbs[1]);
  assert(firstSampleAbs[1] > firstSampleAbs[2]);
  assert(firstSampleAbs[2] < 1e-7f);

  hydra_dsp_destroy(h);
}


void test_silent_wet_delay_does_not_record_bias() {
  hydra_dsp_handle* h = make_handle();
  configure_silent_wet_delay(h);

  std::vector<float> zeros(2048, 0.0f);
  const float peak = process_peak(h, zeros, zeros);
  assert(peak < 1e-7f);

  hydra_dsp_destroy(h);
}

void test_reenabled_delay_clears_magnetic_state() {
  hydra_dsp_handle* h = make_handle();
  configure_silent_wet_delay(h, 100.0f);

  std::vector<float> hot(256, 0.75f);
  assert(process_peak(h, hot, hot) >= 0.0f);

  hydra_dsp_params disabled{};
  disabled.delayActive = 0.0f;
  disabled.delayTimeMs = 10.0f;
  disabled.activeHeads = 4.0f;
  disabled.tone = 100.0f;
  assert(hydra_dsp_set_params(h, &disabled) == 0);
  assert(hydra_dsp_commit(h) == 0);

  configure_silent_wet_delay(h);

  std::vector<float> zeros(2048, 0.0f);
  const float peak = process_peak(h, zeros, zeros);
  assert(peak < 1e-7f);

  hydra_dsp_destroy(h);
}

}  // namespace

int main() {
  test_known_buffer();
  test_reset_state();
  test_parameter_automation();
  test_silent_wet_delay_does_not_record_bias();
  test_reenabled_delay_clears_magnetic_state();
  test_ping_pong_cross_feedback_moves_repeat_to_opposite_channel();
  std::cout << "regression_core: ok\n";
  return 0;
}
