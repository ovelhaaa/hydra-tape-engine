// Non-gating observation copy; original checks/thresholds retained, continue after failures.
#include <cstdio>
static int observations_failed=0;
#define CHECK(x) do { if(!(x)){std::printf("OBSERVATION_FAIL line=%d expr=%s\n",__LINE__,#x);++observations_failed;} } while(0)

#include "hydra_dsp.h"
#include "hydra_pcm.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

namespace {

constexpr float kSampleRate = 48000.0f;
constexpr float kMaxDelayMs = 2000.0f;
constexpr uint32_t kFrames = 64;

hydra_dsp_handle* make_handle(float maxDelayMs = kMaxDelayMs) {
  hydra_dsp_handle* h = nullptr;
  CHECK(hydra_dsp_create(kSampleRate, maxDelayMs, &h) == 0);
  CHECK(h != nullptr);
  CHECK(hydra_dsp_prepare(h, kFrames, HYDRA_DSP_CHANNELS_STEREO) == 0);
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
  CHECK(hydra_dsp_set_params_v1_3(h, &p) == 0);
  CHECK(hydra_dsp_commit(h) == 0);
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
  CHECK(hydra_dsp_set_params_v1_3(h, &p) == 0);
  CHECK(hydra_dsp_commit(h) == 0);
}

float process_peak(hydra_dsp_handle* h, const std::vector<float>& inL, const std::vector<float>& inR) {
  std::vector<float> outL(inL.size());
  std::vector<float> outR(inR.size());
  CHECK(hydra_dsp_process(h, inL.data(), inR.data(), outL.data(), outR.data(), inL.size()) == 0);

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
  CHECK(hydra_dsp_set_params_v1_3(h, &p) == 0);
  CHECK(hydra_dsp_commit(h) == 0);
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
  CHECK(hydra_dsp_process(h, inL.data(), inR.data(), outL.data(), outR.data(), inL.size()) == 0);

  const float firstEchoL = channel_energy(outL, 430, 540);
  const float firstEchoR = channel_energy(outR, 430, 540);
  const float secondEchoL = channel_energy(outL, 900, 1040);
  const float secondEchoR = channel_energy(outR, 900, 1040);

  CHECK(firstEchoL > firstEchoR * 100.0f);
  CHECK(secondEchoR > firstEchoR * 5.0f);
  CHECK(secondEchoR > 1e-5f);

  hydra_dsp_destroy(h);
}

void set_param(hydra_dsp_handle* h, hydra_dsp_param_id id, float value) {
  CHECK(hydra_dsp_set_parameter(h, id, value) == 0);
  CHECK(hydra_dsp_commit(h) == 0);
}

void test_freeze_preserves_recording_until_released() {
  constexpr size_t ringFrames = 960;
  auto* h = make_handle(20.0f);
  configure_ping_pong_delay(h, false);
  set_param(h, HYDRA_DSP_PARAM_FEEDBACK, 0.0f);

  std::vector<float> input(ringFrames, 0.0f), silence(ringFrames, 0.0f);
  std::vector<float> outL(ringFrames), outR(ringFrames);
  input[80] = 0.8f;
  CHECK(hydra_dsp_process(h, input.data(), silence.data(), outL.data(), outR.data(), ringFrames) == 0);

  set_param(h, HYDRA_DSP_PARAM_FREEZE, 1.0f);
  const auto processSilence = [&] {
    CHECK(hydra_dsp_process(h, silence.data(), silence.data(), outL.data(), outR.data(), ringFrames) == 0);
    return channel_energy(outL, 530, 620);
  };
  const float firstLoop = processSilence();
  for (int loop = 0; loop < 8; ++loop) {
    CHECK(processSilence() > firstLoop * 0.5f);
  }
  CHECK(firstLoop > 1e-5f);

  set_param(h, HYDRA_DSP_PARAM_FREEZE, 0.0f);
  for (int loop = 0; loop < 8; ++loop) {
    processSilence();
  }
  CHECK(processSilence() < firstLoop * 0.01f);
  hydra_dsp_destroy(h);
}

void test_reverse_stereo_channel_isolation() {
  for (bool leftInput : {true, false}) {
    std::vector<float> signal(4096, 0.0f), silence(4096, 0.0f);
    std::vector<float> outL(4096), outR(4096);
    signal[100] = 0.8f;
    signal[260] = -0.4f;
    auto* h = make_handle();
    configure_ping_pong_delay(h, false);
    set_param(h, HYDRA_DSP_PARAM_FEEDBACK, 0.0f);
    set_param(h, HYDRA_DSP_PARAM_REVERSE, 1.0f);
    const auto& inL = leftInput ? signal : silence;
    const auto& inR = leftInput ? silence : signal;
    CHECK(hydra_dsp_process(h, inL.data(), inR.data(), outL.data(), outR.data(), signal.size()) == 0);
    const auto reverse = leftInput ? outL : outR;
    const auto& quiet = leftInput ? outR : outL;
    CHECK(channel_energy(reverse, 150, reverse.size()) > 1e-5f);
    CHECK(channel_energy(quiet, 0, quiet.size()) < 1e-10f);

    set_param(h, HYDRA_DSP_PARAM_REVERSE, 0.0f);
    hydra_dsp_reset(h);
    CHECK(hydra_dsp_process(h, inL.data(), inR.data(), outL.data(), outR.data(), signal.size()) == 0);
    const auto& forward = leftInput ? outL : outR;
    CHECK(!std::equal(reverse.begin(), reverse.end(), forward.begin(),
                      [](float a, float b) { return std::fabs(a - b) < 1e-5f; }));
    hydra_dsp_destroy(h);
  }
}

void test_max_delay_buffer_wrap_is_block_independent() {
  constexpr size_t frames = 4096;
  auto* h = make_handle(20.0f);
  configure_ping_pong_delay(h, false);
  set_param(h, HYDRA_DSP_PARAM_DELAY_MS, 20.0f);
  std::vector<float> inL(frames, 0.0f), inR(frames, 0.0f);
  std::vector<float> fullL(frames), fullR(frames), splitL(frames), splitR(frames);
  inL[955] = 0.8f;
  inR[1920] = -0.6f;
  CHECK(hydra_dsp_process(h, inL.data(), inR.data(), fullL.data(), fullR.data(), frames) == 0);
  hydra_dsp_destroy(h);
  h = make_handle(20.0f);
  configure_ping_pong_delay(h, false);
  set_param(h, HYDRA_DSP_PARAM_DELAY_MS, 20.0f);
  for (size_t offset = 0; offset < frames; offset += 37) {
    const auto count = std::min<size_t>(37, frames - offset);
    CHECK(hydra_dsp_process(h, inL.data() + offset, inR.data() + offset,
                           splitL.data() + offset, splitR.data() + offset, count) == 0);
  }
  CHECK(channel_energy(fullL, 1880, 1990) > 1e-5f);
  std::printf("wrap_R_energy=%.12g block_exact_L=%d block_exact_R=%d\n",channel_energy(fullR,2840,2960),fullL==splitL,fullR==splitR);
  CHECK(channel_energy(fullR, 2840, 2960) > 1e-5f);
  CHECK(fullL == splitL);
  CHECK(fullR == splitR);
  hydra_dsp_destroy(h);
}

void test_feedback_adds_same_channel_repeat() {
  auto* h = make_handle();
  configure_ping_pong_delay(h, false);
  std::vector<float> inL(1600, 0.0f), inR(1600, 0.0f), outL(1600), outR(1600);
  inL[0] = 0.8f;
  CHECK(hydra_dsp_process(h, inL.data(), inR.data(), outL.data(), outR.data(), inL.size()) == 0);
  const float withFeedback = channel_energy(outL, 900, 1040);
  CHECK(channel_energy(outR, 900, 1040) < 1e-10f);
  hydra_dsp_destroy(h);
  h = make_handle();
  configure_ping_pong_delay(h, false);
  set_param(h, HYDRA_DSP_PARAM_FEEDBACK, 0.0f);
  CHECK(hydra_dsp_process(h, inL.data(), inR.data(), outL.data(), outR.data(), inL.size()) == 0);
  CHECK(withFeedback > 1e-5f);
  CHECK(withFeedback > 1.05f * channel_energy(outL, 900, 1040));
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
  CHECK(hydra_dsp_process(h, inL.data(), inR.data(), outL.data(), outR.data(), inL.size()) == 0);

  constexpr std::array<float, 16> expectedL = {
      -0.4f, -0.35f, -0.3f, -0.25f, -0.2f, -0.15f, -0.1f, -0.05f,
      0.0f, 0.05f, 0.1f, 0.15f, 0.2f, 0.25f, 0.3f, 0.35f,
  };
  constexpr std::array<float, 16> expectedR = {
      0.3f, 0.2625f, 0.225f, 0.1875f, 0.15f, 0.1125f, 0.075f, 0.0375f,
      0.0f, -0.0375f, -0.075f, -0.1125f, -0.15f, -0.1875f, -0.225f, -0.2625f,
  };

  for (size_t i = 0; i < expectedL.size(); ++i) {
    CHECK(std::fabs(outL[i] - expectedL[i]) < 1e-7f);
    CHECK(std::fabs(outR[i] - expectedR[i]) < 1e-7f);
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
  CHECK(hydra_dsp_process(h, inL.data(), inR.data(), outAL.data(), outAR.data(), kFrames) == 0);

  hydra_dsp_reset(h);
  CHECK(hydra_dsp_process(h, inL.data(), inR.data(), outBL.data(), outBR.data(), kFrames) == 0);

  for (uint32_t i = 0; i < kFrames; ++i) {
    CHECK(std::fabs(outAL[i] - outBL[i]) < 1e-7f);
    CHECK(std::fabs(outAR[i] - outBR[i]) < 1e-7f);
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
    CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_DRY_WET, dryWetValues[i]) == 0);
    CHECK(hydra_dsp_commit(h) == 0);
    CHECK(hydra_dsp_process(h, inputL.data(), inputR.data(), outL.data(), outR.data(), inputL.size()) == 0);
    firstSampleAbs[i] = std::fabs(outL[0]);
  }

  // Delay remains disabled, so increasing dry/wet should reduce dry contribution.
  CHECK(firstSampleAbs[0] > firstSampleAbs[1]);
  CHECK(firstSampleAbs[1] > firstSampleAbs[2]);
  CHECK(firstSampleAbs[2] < 1e-7f);

  hydra_dsp_destroy(h);
}


void test_silent_wet_delay_does_not_record_bias() {
  hydra_dsp_handle* h = make_handle();
  configure_silent_wet_delay(h);

  std::vector<float> zeros(2048, 0.0f);
  const float peak = process_peak(h, zeros, zeros);
  CHECK(peak < 1e-7f);

  hydra_dsp_destroy(h);
}

void test_reenabled_delay_clears_magnetic_state() {
  hydra_dsp_handle* h = make_handle();
  configure_silent_wet_delay(h, 100.0f);

  std::vector<float> hot(256, 0.75f);
  CHECK(process_peak(h, hot, hot) >= 0.0f);

  hydra_dsp_params disabled{};
  disabled.delayActive = 0.0f;
  disabled.delayTimeMs = 10.0f;
  disabled.activeHeads = 4.0f;
  disabled.tone = 100.0f;
  CHECK(hydra_dsp_set_params_v1_3(h, &disabled) == 0);
  CHECK(hydra_dsp_commit(h) == 0);

  configure_silent_wet_delay(h);

  std::vector<float> zeros(2048, 0.0f);
  const float peak = process_peak(h, zeros, zeros);
  CHECK(peak < 1e-7f);

  hydra_dsp_destroy(h);
}

void test_pcm32_pack_saturates_outside_integer_range() {
  using hydra::dsp::saturatePcm32;
  constexpr float edge = 2147483648.0f;
  const float lastPositive = std::nextafter(edge, 0.0f);
  const float infinity = std::numeric_limits<float>::infinity();
  CHECK(saturatePcm32(lastPositive) == 2147483520);
  CHECK(saturatePcm32(edge) == INT32_MAX);
  CHECK(saturatePcm32(-edge) == INT32_MIN);
  CHECK(saturatePcm32(std::nextafter(-edge, -infinity)) == INT32_MIN);
  CHECK(saturatePcm32(42.75f) == 42);
  CHECK(saturatePcm32(-42.75f) == -42);
  CHECK(saturatePcm32(infinity) == INT32_MAX);
  CHECK(saturatePcm32(-infinity) == INT32_MIN);
  CHECK(saturatePcm32(std::numeric_limits<float>::quiet_NaN()) == 0);
}

}  // namespace

int main() {
  test_pcm32_pack_saturates_outside_integer_range();
  test_known_buffer();
  test_freeze_preserves_recording_until_released();
  test_reverse_stereo_channel_isolation();
  test_max_delay_buffer_wrap_is_block_independent();
  test_feedback_adds_same_channel_repeat();
  test_reset_state();
  test_parameter_automation();
  test_silent_wet_delay_does_not_record_bias();
  test_reenabled_delay_clears_magnetic_state();
  test_ping_pong_cross_feedback_moves_repeat_to_opposite_channel();
  std::cout << "regression_core: assertions observed (see failures)\n";
  std::printf("observations_failed=%d\n",observations_failed); return observations_failed?1:0;
}
