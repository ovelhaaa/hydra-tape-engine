#include "hydra_dsp.h"
#include "check.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>

namespace {

constexpr float kTwoPi = 6.28318530718f;

float computeRmsDiff(const std::vector<float>& a, const std::vector<float>& b, size_t start = 0) {
  double sum = 0.0;
  size_t count = 0;
  for (size_t i = start; i < a.size() && i < b.size(); ++i) {
    const double d = static_cast<double>(a[i]) - static_cast<double>(b[i]);
    sum += d * d;
    ++count;
  }
  return count > 0 ? static_cast<float>(std::sqrt(sum / static_cast<double>(count))) : 0.0f;
}

std::vector<float> renderToneWithMotion(int mode,
                                        float motionAmount,
                                        float stereoMotion,
                                        hydra_dsp_param_id advParam,
                                        float advValue,
                                        std::vector<float>* outR = nullptr,
                                        uint32_t kBlocks = 240) {
  constexpr float kSampleRate = 48000.0f;
  constexpr uint32_t kBlock = 128;

  hydra_dsp_handle* h = nullptr;
  CHECK(hydra_dsp_create_v1_4(kSampleRate, 2000.0f, 4000.0f, &h) == 0);
  CHECK(hydra_dsp_prepare(h, kBlock, HYDRA_DSP_CHANNELS_STEREO) == 0);

  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_DELAY_ACTIVE, 1.0f) == 0);
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_DELAY_MS, 120.0f) == 0);
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_FEEDBACK, 35.0f) == 0);
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_DRY_WET, 100.0f) == 0);
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_DELAY_WET, 100.0f) == 0);
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_WOW_DEPTH, 22.0f) == 0);
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_FLUTTER_DEPTH, 26.0f) == 0);
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_TAPE_AGE, 65.0f) == 0);
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_AZIMUTH, 0.0f) == 0);
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_DROPOUT, 0.0f) == 0);
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_NOISE, 0.0f) == 0);
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_MOTION_MODE, static_cast<float>(mode)) == 0);
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_MOTION_AMOUNT, motionAmount) == 0);
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_STEREO_MOTION, stereoMotion) == 0);
  if (advParam != HYDRA_DSP_PARAM_COUNT) {
    CHECK(hydra_dsp_set_parameter(h, advParam, advValue) == 0);
  }
  CHECK(hydra_dsp_commit(h) == 0);

  std::vector<float> inL(kBlock), inR(kBlock), blockOutL(kBlock), blockOutR(kBlock);
  std::vector<float> allL(kBlock * kBlocks);
  if (outR) outR->resize(kBlock * kBlocks);

  uint32_t sampleIdx = 0;
  for (uint32_t b = 0; b < kBlocks; ++b) {
    for (uint32_t i = 0; i < kBlock; ++i) {
      const float s = 0.35f * std::sin(kTwoPi * 440.0f * static_cast<float>(sampleIdx) / kSampleRate);
      inL[i] = s;
      inR[i] = s;
      ++sampleIdx;
    }
    CHECK(hydra_dsp_process(h, inL.data(), inR.data(), blockOutL.data(), blockOutR.data(), kBlock) == 0);
    std::copy(blockOutL.begin(), blockOutL.end(), allL.begin() + b * kBlock);
    if (outR) {
      std::copy(blockOutR.begin(), blockOutR.end(), outR->begin() + b * kBlock);
    }
  }

  hydra_dsp_destroy(h);
  return allL;
}

void testParamContract() {
  CHECK(hydra_dsp_get_param_count() == HYDRA_DSP_PARAM_COUNT);
  CHECK(HYDRA_DSP_PARAM_MOTION_TENSION_REC == 46);
  CHECK(HYDRA_DSP_PARAM_COUNT >= 47);

  hydra_dsp_param_spec spec{};
  CHECK(hydra_dsp_get_param_spec(HYDRA_DSP_PARAM_MOTION_MODE, &spec) == 0);
  CHECK(spec.min_value == 0.0f && spec.max_value == 3.0f && spec.default_value == 0.0f);

  CHECK(hydra_dsp_get_param_spec(HYDRA_DSP_PARAM_MOTION_AMOUNT, &spec) == 0);
  CHECK(spec.min_value == 0.0f && spec.max_value == 100.0f && spec.default_value == 100.0f);

  CHECK(hydra_dsp_get_param_spec(HYDRA_DSP_PARAM_STEREO_MOTION, &spec) == 0);
  CHECK(spec.min_value == 0.0f && spec.max_value == 100.0f && spec.default_value == 100.0f);

  hydra_dsp_handle* h = nullptr;
  CHECK(hydra_dsp_create(48000.0f, 2000.0f, &h) == 0);
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_MOTION_MODE, 2.0f) == 0);
  float val = -1.0f;
  CHECK(hydra_dsp_get_parameter(h, HYDRA_DSP_PARAM_MOTION_MODE, &val) == 0);
  CHECK(val == 2.0f);

  float modL = 99.0f, modR = 99.0f;
  CHECK(hydra_dsp_get_motion_telemetry(h, &modL, &modR) == 0);
  CHECK(std::isfinite(modL) && std::isfinite(modR));
  hydra_dsp_destroy(h);
}

void testModesAndControlsDistinct() {
  std::vector<float> r0, r1, r2, r3;
  auto m0 = renderToneWithMotion(0, 100.0f, 100.0f, HYDRA_DSP_PARAM_COUNT, 0.0f, &r0);
  auto m1 = renderToneWithMotion(1, 100.0f, 100.0f, HYDRA_DSP_PARAM_COUNT, 0.0f, &r1);
  auto m2 = renderToneWithMotion(2, 100.0f, 100.0f, HYDRA_DSP_PARAM_COUNT, 0.0f, &r2);
  auto m3 = renderToneWithMotion(3, 100.0f, 100.0f, HYDRA_DSP_PARAM_COUNT, 0.0f, &r3);

  const size_t wetStart = 48000 / 4; // after delay fills
  CHECK(computeRmsDiff(m0, m1, wetStart) > 1e-3f);
  CHECK(computeRmsDiff(m0, m2, wetStart) > 1e-3f);
  CHECK(computeRmsDiff(m1, m2, wetStart) > 1e-3f);
  CHECK(computeRmsDiff(m2, m3, wetStart) > 1e-3f);

  // Motion Amount = 0 -> zero modulation -> L and R identical (with azimuth=0)
  std::vector<float> zeroR;
  auto zeroL = renderToneWithMotion(3, 0.0f, 100.0f, HYDRA_DSP_PARAM_COUNT, 0.0f, &zeroR);
  CHECK(computeRmsDiff(zeroL, zeroR, wetStart) < 1e-6f);

  // Stereo Motion = 0 in M2 -> L and R identical
  std::vector<float> monoR;
  auto monoL = renderToneWithMotion(2, 100.0f, 0.0f, HYDRA_DSP_PARAM_COUNT, 0.0f, &monoR);
  CHECK(computeRmsDiff(monoL, monoR, wetStart) < 1e-6f);

  // Advanced controls have measurable effect
  auto m1_in0 = renderToneWithMotion(1, 100.0f, 100.0f, HYDRA_DSP_PARAM_MOTION_INERTIA, 0.0f);
  auto m1_in100 = renderToneWithMotion(1, 100.0f, 100.0f, HYDRA_DSP_PARAM_MOTION_INERTIA, 100.0f);
  CHECK(computeRmsDiff(m1_in0, m1_in100, wetStart) > 1e-3f);

  auto m1_dc0 = renderToneWithMotion(1, 100.0f, 100.0f, HYDRA_DSP_PARAM_MOTION_DRIFT_CHAR, 0.0f);
  auto m1_dc100 = renderToneWithMotion(1, 100.0f, 100.0f, HYDRA_DSP_PARAM_MOTION_DRIFT_CHAR, 100.0f);
  CHECK(computeRmsDiff(m1_dc0, m1_dc100, wetStart) > 1e-3f);

  auto m2_tx0 = renderToneWithMotion(2, 100.0f, 100.0f, HYDRA_DSP_PARAM_MOTION_TEXTURE, 0.0f);
  auto m2_tx100 = renderToneWithMotion(2, 100.0f, 100.0f, HYDRA_DSP_PARAM_MOTION_TEXTURE, 100.0f);
  CHECK(computeRmsDiff(m2_tx0, m2_tx100, wetStart) > 1e-3f);

  auto m2_ir0 = renderToneWithMotion(2, 100.0f, 100.0f, HYDRA_DSP_PARAM_MOTION_IRREGULARITY, 0.0f);
  auto m2_ir100 = renderToneWithMotion(2, 100.0f, 100.0f, HYDRA_DSP_PARAM_MOTION_IRREGULARITY, 100.0f);
  CHECK(computeRmsDiff(m2_ir0, m2_ir100, wetStart) > 1e-3f);

  auto m3_in0 = renderToneWithMotion(3, 100.0f, 100.0f, HYDRA_DSP_PARAM_MOTION_INSTABILITY, 0.0f, nullptr, 750);
  auto m3_in100 = renderToneWithMotion(3, 100.0f, 100.0f, HYDRA_DSP_PARAM_MOTION_INSTABILITY, 100.0f, nullptr, 750);
  CHECK(computeRmsDiff(m3_in0, m3_in100, wetStart) > 1e-3f);

  auto m3_tr0 = renderToneWithMotion(3, 100.0f, 100.0f, HYDRA_DSP_PARAM_MOTION_TENSION_REC, 0.0f, nullptr, 1500);
  auto m3_tr100 = renderToneWithMotion(3, 100.0f, 100.0f, HYDRA_DSP_PARAM_MOTION_TENSION_REC, 100.0f, nullptr, 1500);
  CHECK(computeRmsDiff(m3_tr0, m3_tr100, wetStart) > 1e-4f);
}

void testClicklessSwitchingHighFeedbackMultiHeadReverseSpring() {
  constexpr float kSampleRate = 48000.0f;
  constexpr uint32_t kBlock = 128;

  hydra_dsp_handle* h = nullptr;
  CHECK(hydra_dsp_create_v1_4(kSampleRate, 2000.0f, 4000.0f, &h) == 0);
  CHECK(hydra_dsp_prepare(h, kBlock, HYDRA_DSP_CHANNELS_STEREO) == 0);

  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_DELAY_ACTIVE, 1.0f) == 0);
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_DELAY_MS, 250.0f) == 0);
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_FEEDBACK, 85.0f) == 0);
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_ACTIVE_HEADS, 7.0f) == 0); // Multi-head
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_REVERSE, 1.0f) == 0);
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_SPRING, 1.0f) == 0);
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_DRY_WET, 85.0f) == 0);
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_DELAY_WET, 80.0f) == 0);
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_WOW_DEPTH, 25.0f) == 0);
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_FLUTTER_DEPTH, 30.0f) == 0);
  CHECK(hydra_dsp_commit(h) == 0);

  std::vector<float> inL(kBlock), inR(kBlock), outL(kBlock), outR(kBlock);
  float prevL = 0.0f, prevR = 0.0f;
  float maxJump = 0.0f;
  uint32_t sampleIdx = 0;

  // Switch modes: slow transitions + rapid 10ms consecutive switches
  const int scheduleBlocks[] = {100, 140, 180, 220, 224, 228, 232, 236, 260};
  const int scheduleModes[]  = {  1,   2,   3,   0,   3,   1,   2,   0,   3};
  size_t schedIdx = 0;

  for (int b = 0; b < 320; ++b) {
    if (schedIdx < 9 && b == scheduleBlocks[schedIdx]) {
      CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_MOTION_MODE, static_cast<float>(scheduleModes[schedIdx])) == 0);
      CHECK(hydra_dsp_commit(h) == 0);
      ++schedIdx;
    }

    for (uint32_t i = 0; i < kBlock; ++i) {
      // Continuous guitar-like harmonic input for first 120 blocks, then tail
      const float env = (b < 120) ? 0.35f : 0.0f;
      const float s = env * (0.7f * std::sin(kTwoPi * 220.0f * sampleIdx / kSampleRate) +
                             0.3f * std::sin(kTwoPi * 440.0f * sampleIdx / kSampleRate));
      inL[i] = s;
      inR[i] = s;
      ++sampleIdx;
    }

    CHECK(hydra_dsp_process(h, inL.data(), inR.data(), outL.data(), outR.data(), kBlock) == 0);

    for (uint32_t i = 0; i < kBlock; ++i) {
      CHECK(std::isfinite(outL[i]) && std::isfinite(outR[i]));
      CHECK(std::fabs(outL[i]) <= 2.0f && std::fabs(outR[i]) <= 2.0f);
      if (b > 2) {
        maxJump = std::max(maxJump, std::fabs(outL[i] - prevL));
        maxJump = std::max(maxJump, std::fabs(outR[i] - prevR));
      }
      prevL = outL[i];
      prevR = outR[i];
    }

    // Confirm delay tail stays alive after switching (no delay reset!)
    if (b == 245) {
      double blockEnergy = 0.0;
      for (uint32_t i = 0; i < kBlock; ++i) blockEnergy += outL[i] * outL[i];
      CHECK(blockEnergy > 1e-4);
    }
  }

  CHECK(maxJump < 0.25f);
  hydra_dsp_destroy(h);
}

void testRapidTransitionDerivativeAndContinuity() {
  constexpr float kSampleRate = 48000.0f;
  constexpr uint32_t kStepSamples = 1; // sample-accurate derivative check

  hydra_dsp_handle* h = nullptr;
  CHECK(hydra_dsp_create_v1_4(kSampleRate, 2000.0f, 4000.0f, &h) == 0);
  CHECK(hydra_dsp_prepare(h, 128, HYDRA_DSP_CHANNELS_STEREO) == 0);

  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_DELAY_ACTIVE, 1.0f) == 0);
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_DELAY_MS, 200.0f) == 0);
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_FEEDBACK, 55.0f) == 0);
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_WOW_DEPTH, 28.0f) == 0);
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_FLUTTER_DEPTH, 32.0f) == 0);
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_TAPE_AGE, 70.0f) == 0);
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_MOTION_AMOUNT, 100.0f) == 0);
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_STEREO_MOTION, 100.0f) == 0);
  CHECK(hydra_dsp_commit(h) == 0);

  // Schedule covers:
  // - full transitions: M0->M1, M1->M2, M2->M3, M3->M0
  // - mid-transition 10ms switches: M0->M3->M1 (at +10ms)
  // - bounce-back before 150ms completion: M1->M2->M1 (at +30ms)
  struct SwitchEvent { uint32_t sample; int mode; };
  const SwitchEvent events[] = {
    {  4800, 1 }, // M0 -> M1 (100ms in)
    { 14400, 2 }, // M1 -> M2 (200ms later)
    { 24000, 3 }, // M2 -> M3
    { 33600, 0 }, // M3 -> M0
    { 43200, 3 }, // M0 -> M3
    { 43680, 1 }, // +10ms mid-transition -> M1
    { 44160, 2 }, // +10ms mid-transition -> M2
    { 45600, 1 }, // +30ms bounce back M1->M2->M1 before 150ms finishes
    { 46080, 3 }, // +10ms -> M3
    { 46560, 0 }, // +10ms -> M0
  };
  size_t evIdx = 0;

  float prevModL = 0.0f, prevModR = 0.0f;
  float prevVelL = 0.0f, prevVelR = 0.0f;
  float maxPosJump = 0.0f;
  float maxAccelJump = 0.0f;

  for (uint32_t n = 0; n < 56000; ++n) {
    if (evIdx < sizeof(events) / sizeof(events[0]) && n == events[evIdx].sample) {
      CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_MOTION_MODE, static_cast<float>(events[evIdx].mode)) == 0);
      CHECK(hydra_dsp_commit(h) == 0);
      ++evIdx;
    }

    const float s = 0.3f * std::sin(kTwoPi * 220.0f * static_cast<float>(n) / kSampleRate);
    float outL = 0.0f, outR = 0.0f;
    CHECK(hydra_dsp_process(h, &s, &s, &outL, &outR, kStepSamples) == 0);
    CHECK(std::isfinite(outL) && std::isfinite(outR));

    float modL = 0.0f, modR = 0.0f;
    CHECK(hydra_dsp_get_motion_telemetry(h, &modL, &modR) == 0);
    CHECK(std::isfinite(modL) && std::isfinite(modR));

    if (n > 0) {
      const float velL = modL - prevModL;
      const float velR = modR - prevModR;
      maxPosJump = std::max(maxPosJump, std::max(std::fabs(velL), std::fabs(velR)));
      if (n > 1) {
        const float accL = velL - prevVelL;
        const float accR = velR - prevVelR;
        maxAccelJump = std::max(maxAccelJump, std::max(std::fabs(accL), std::fabs(accR)));
      }
      prevVelL = velL;
      prevVelR = velR;
    }
    prevModL = modL;
    prevModR = modR;
  }

  // Read-head position and velocity must stay smooth across all rapid switches
  CHECK(maxPosJump < 0.20f);
  CHECK(maxAccelJump < 0.20f);
  hydra_dsp_destroy(h);
}

struct TransitionMetrics {
  float maxPosJumpSamples = 0.0f;
  float maxVelJumpSamplesPerSample = 0.0f;
  float maxPitchCents = 0.0f;
  float audioPeak = 0.0f;
  float audioRms = 0.0f;
  int nonFiniteCount = 0;
  float maxDisplacementSamples = 0.0f;
  int settlingSamples = 0;
};

TransitionMetrics runTransitionSequence(const int* modes,
                                        const uint32_t* switchSamples,
                                        size_t numEvents,
                                        uint32_t totalSamples,
                                        float sampleRate,
                                        float delayMs,
                                        float feedback,
                                        bool reverse,
                                        float activeHeads) {
  hydra_dsp_handle* h = nullptr;
  CHECK(hydra_dsp_create_v1_4(sampleRate, 2000.0f, 4000.0f, &h) == 0);
  CHECK(hydra_dsp_prepare(h, 128, HYDRA_DSP_CHANNELS_STEREO) == 0);

  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_DELAY_ACTIVE, 1.0f) == 0);
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_DELAY_MS, delayMs) == 0);
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_FEEDBACK, feedback) == 0);
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_ACTIVE_HEADS, activeHeads) == 0);
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_REVERSE, reverse ? 1.0f : 0.0f) == 0);
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_SPRING, 1.0f) == 0);
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_DRY_WET, 80.0f) == 0);
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_DELAY_WET, 80.0f) == 0);
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_WOW_DEPTH, 45.0f) == 0);
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_FLUTTER_DEPTH, 45.0f) == 0);
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_TAPE_AGE, 75.0f) == 0);
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_MOTION_AMOUNT, 100.0f) == 0);
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_STEREO_MOTION, 100.0f) == 0);
  CHECK(hydra_dsp_commit(h) == 0);

  TransitionMetrics m{};
  float prevModL = 0.0f, prevModR = 0.0f;
  float prevVelL = 0.0f, prevVelR = 0.0f;
  double sumSq = 0.0;
  size_t evIdx = 0;
  uint32_t lastSwitchSample = 0;

  for (uint32_t n = 0; n < totalSamples; ++n) {
    if (evIdx < numEvents && n == switchSamples[evIdx]) {
      CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_MOTION_MODE, static_cast<float>(modes[evIdx])) == 0);
      CHECK(hydra_dsp_commit(h) == 0);
      lastSwitchSample = n;
      ++evIdx;
    }
    const float s = (n < static_cast<uint32_t>(sampleRate * 0.8f))
                        ? 0.30f * std::sin(kTwoPi * 220.0f * static_cast<float>(n) / sampleRate)
                        : 0.0f;
    float outL = 0.0f, outR = 0.0f;
    CHECK(hydra_dsp_process(h, &s, &s, &outL, &outR, 1) == 0);
    if (!std::isfinite(outL) || !std::isfinite(outR)) ++m.nonFiniteCount;
    m.audioPeak = std::max(m.audioPeak, std::max(std::fabs(outL), std::fabs(outR)));
    sumSq += static_cast<double>(outL) * outL + static_cast<double>(outR) * outR;

    float modL = 0.0f, modR = 0.0f;
    CHECK(hydra_dsp_get_motion_telemetry(h, &modL, &modR) == 0);
    if (!std::isfinite(modL) || !std::isfinite(modR)) ++m.nonFiniteCount;
    m.maxDisplacementSamples = std::max(m.maxDisplacementSamples, std::max(std::fabs(modL), std::fabs(modR)));

    if (n > 0) {
      const float velL = modL - prevModL;
      const float velR = modR - prevModR;
      const float posJump = std::max(std::fabs(velL), std::fabs(velR));
      m.maxPosJumpSamples = std::max(m.maxPosJumpSamples, posJump);
      // Instantaneous pitch change in cents: 1200 * log2(1 - d(mod)/dn)
      const float rateRatio = std::max(0.5f, 1.0f - posJump);
      const float cents = std::fabs(1200.0f * std::log2(rateRatio));
      m.maxPitchCents = std::max(m.maxPitchCents, cents);
      if (n > 1) {
        const float acc = std::max(std::fabs(velL - prevVelL), std::fabs(velR - prevVelR));
        m.maxVelJumpSamplesPerSample = std::max(m.maxVelJumpSamplesPerSample, acc);
      }
      prevVelL = velL;
      prevVelR = velR;
    }
    prevModL = modL;
    prevModR = modR;
  }
  m.settlingSamples = static_cast<int>(totalSamples - lastSwitchSample);
  m.audioRms = static_cast<float>(std::sqrt(sumSq / (2.0 * totalSamples)));
  hydra_dsp_destroy(h);
  return m;
}

void testTransitionContinuityMatrix() {
  // M0->M1->M3, M1->M2->M1, M3->M0->M2, 10ms switches, <half crossfade, high feedback, reverse, multi-head
  const uint32_t sw1[] = { 4800, 5280, 5760, 8160, 8640, 12000, 12480, 12960 }; // 10ms (480 samples) & 50ms
  const int md1[]      = {    1,    3,    1,    2,    1,     3,     0,     2 };
  auto m = runTransitionSequence(md1, sw1, 8, 28800, 48000.0f, 220.0f, 90.0f, true, 7.0f);
  std::printf("[TransitionMatrix] maxPosJump=%.5f samp, maxVelJump=%.5f samp/samp, maxPitch=%.2f cents, peak=%.4f, rms=%.4f, maxDisp=%.2f samp, nonFinite=%d\n",
              m.maxPosJumpSamples, m.maxVelJumpSamplesPerSample, m.maxPitchCents,
              m.audioPeak, m.audioRms, m.maxDisplacementSamples, m.nonFiniteCount);
  CHECK(m.nonFiniteCount == 0);
  CHECK(m.maxPosJumpSamples < 0.25f);
  CHECK(m.maxVelJumpSamplesPerSample < 0.15f);
  CHECK(m.maxDisplacementSamples <= 0.25f * (220.0f * 48.0f) + 1e-3f);
  CHECK(m.audioPeak <= 2.0f);
}

void testMotionAmountAndStereoMotionDuringTransitions() {
  constexpr float kSampleRate = 48000.0f;
  hydra_dsp_handle* h = nullptr;
  CHECK(hydra_dsp_create_v1_4(kSampleRate, 2000.0f, 4000.0f, &h) == 0);
  CHECK(hydra_dsp_prepare(h, 128, HYDRA_DSP_CHANNELS_STEREO) == 0);

  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_DELAY_ACTIVE, 1.0f) == 0);
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_DELAY_MS, 200.0f) == 0);
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_FEEDBACK, 50.0f) == 0);
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_WOW_DEPTH, 40.0f) == 0);
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_FLUTTER_DEPTH, 40.0f) == 0);
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_TAPE_AGE, 70.0f) == 0);
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_MOTION_MODE, 3.0f) == 0);
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_MOTION_AMOUNT, 100.0f) == 0);
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_STEREO_MOTION, 100.0f) == 0);
  CHECK(hydra_dsp_commit(h) == 0);

  auto stepSamples = [&](uint32_t count, float& maxJump) {
    float prevModL = 0.0f, prevModR = 0.0f;
    CHECK(hydra_dsp_get_motion_telemetry(h, &prevModL, &prevModR) == 0);
    for (uint32_t n = 0; n < count; ++n) {
      const float s = 0.25f * std::sin(kTwoPi * 330.0f * static_cast<float>(n) / kSampleRate);
      float oL = 0.0f, oR = 0.0f;
      CHECK(hydra_dsp_process(h, &s, &s, &oL, &oR, 1) == 0);
      CHECK(std::isfinite(oL) && std::isfinite(oR));
      float mL = 0.0f, mR = 0.0f;
      CHECK(hydra_dsp_get_motion_telemetry(h, &mL, &mR) == 0);
      maxJump = std::max(maxJump, std::max(std::fabs(mL - prevModL), std::fabs(mR - prevModR)));
      prevModL = mL;
      prevModR = mR;
    }
  };

  float maxJump = 0.0f;
  // Warm up M3 so displacement and L/R difference are active
  stepSamples(9600, maxJump);

  // Trigger rapid mode switch M3 -> M1 -> M2 AND simultaneously drop Motion Amount 100 -> 0
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_MOTION_MODE, 1.0f) == 0);
  CHECK(hydra_dsp_commit(h) == 0);
  stepSamples(480, maxJump); // 10ms
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_MOTION_MODE, 2.0f) == 0);
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_MOTION_AMOUNT, 0.0f) == 0);
  CHECK(hydra_dsp_commit(h) == 0);

  // Within 35 ms (1680 samples, 30 ms slew), mechanical motion must converge to exact 0.0f even mid-transition!
  stepSamples(1680, maxJump);
  float modL = 99.0f, modR = 99.0f;
  CHECK(hydra_dsp_get_motion_telemetry(h, &modL, &modR) == 0);
  CHECK(modL == 0.0f && modR == 0.0f);

  // Ramp Motion Amount 0 -> 100 and drop Stereo Motion 100 -> 0 during mode switch M2 -> M3
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_MOTION_MODE, 3.0f) == 0);
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_MOTION_AMOUNT, 100.0f) == 0);
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_STEREO_MOTION, 0.0f) == 0);
  CHECK(hydra_dsp_commit(h) == 0);
  stepSamples(1680, maxJump);
  CHECK(hydra_dsp_get_motion_telemetry(h, &modL, &modR) == 0);
  CHECK(std::fabs(modL - modR) < 1e-6f);

  // Restore Stereo Motion 0 -> 100 and return to Classic M0
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_STEREO_MOTION, 100.0f) == 0);
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_MOTION_MODE, 0.0f) == 0);
  CHECK(hydra_dsp_commit(h) == 0);
  stepSamples(9600, maxJump);

  std::printf("[AmountStereoContract] maxPosJumpAcrossAutomation=%.5f samp\n", maxJump);
  CHECK(maxJump < 0.35f);
  hydra_dsp_destroy(h);
}

void testUnifiedMotionRuntimeStabilityStress() {
  // Gate: UNIFIED_MOTION_RUNTIME_STABILITY_PASS
  const float rates[] = { 44100.0f, 48000.0f };
  const float feedbacks[] = { 85.0f, 95.0f };
  constexpr uint32_t kBlock = 128;
  constexpr uint32_t kBlocksPerRun = 1500; // ~4 seconds per run

  float worstAudioPeak = 0.0f;
  float worstModDisp = 0.0f;
  double worstBlockUs = 0.0;

  for (float sr : rates) {
    for (float fbk : feedbacks) {
      hydra_dsp_handle* h = nullptr;
      CHECK(hydra_dsp_create_v1_4(sr, 2000.0f, 4000.0f, &h) == 0);
      CHECK(hydra_dsp_prepare(h, kBlock, HYDRA_DSP_CHANNELS_STEREO) == 0);

      CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_DELAY_ACTIVE, 1.0f) == 0);
      CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_DELAY_MS, 260.0f) == 0);
      CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_FEEDBACK, fbk) == 0);
      CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_ACTIVE_HEADS, 7.0f) == 0);
      CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_REVERSE, 1.0f) == 0);
      CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_SPRING, 1.0f) == 0);
      CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_DRY_WET, 85.0f) == 0);
      CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_DELAY_WET, 80.0f) == 0);
      CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_WOW_DEPTH, 85.0f) == 0);
      CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_FLUTTER_DEPTH, 85.0f) == 0);
      CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_TAPE_AGE, 80.0f) == 0);
      CHECK(hydra_dsp_commit(h) == 0);

      std::vector<float> inL(kBlock), inR(kBlock), outL(kBlock), outR(kBlock);
      uint32_t rng = 0xCAFEBABEu;
      auto nextRand = [&]() -> float {
        rng = rng * 1664525u + 1013904223u;
        return static_cast<float>((rng >> 8) & 0xFFFFu) / 65535.0f;
      };

      uint32_t sampleIdx = 0;
      for (uint32_t b = 0; b < kBlocksPerRun; ++b) {
        // Intense deterministic automation every 3 blocks (~8 ms)
        if ((b % 3u) == 0u) {
          const int mode = static_cast<int>(nextRand() * 4.0f) % 4;
          const float amt = (b % 15u == 0u) ? 0.0f : (20.0f + 80.0f * nextRand());
          const float st = (b % 18u == 0u) ? 0.0f : (100.0f * nextRand());
          const float dly = 140.0f + 240.0f * nextRand();
          const float dynFbk = fbk - 10.0f * nextRand();
          CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_MOTION_MODE, static_cast<float>(mode)) == 0);
          CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_MOTION_AMOUNT, amt) == 0);
          CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_STEREO_MOTION, st) == 0);
          CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_DELAY_MS, dly) == 0);
          CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_FEEDBACK, dynFbk) == 0);
          CHECK(hydra_dsp_commit(h) == 0);
        }

        for (uint32_t i = 0; i < kBlock; ++i) {
          const float env = (b < 400) ? 0.30f : 0.0f;
          const float s = env * std::sin(kTwoPi * 261.63f * static_cast<float>(sampleIdx++) / sr);
          inL[i] = s;
          inR[i] = s;
        }

        const auto t0 = std::chrono::steady_clock::now();
        CHECK(hydra_dsp_process(h, inL.data(), inR.data(), outL.data(), outR.data(), kBlock) == 0);
        const auto t1 = std::chrono::steady_clock::now();
        const double us = std::chrono::duration<double, std::micro>(t1 - t0).count();
        worstBlockUs = std::max(worstBlockUs, us);

        for (uint32_t i = 0; i < kBlock; ++i) {
          CHECK(std::isfinite(outL[i]) && std::isfinite(outR[i]));
          CHECK(std::fabs(outL[i]) <= 2.5f && std::fabs(outR[i]) <= 2.5f);
          worstAudioPeak = std::max(worstAudioPeak, std::max(std::fabs(outL[i]), std::fabs(outR[i])));
        }
        float modL = 0.0f, modR = 0.0f;
        CHECK(hydra_dsp_get_motion_telemetry(h, &modL, &modR) == 0);
        CHECK(std::isfinite(modL) && std::isfinite(modR));
        worstModDisp = std::max(worstModDisp, std::max(std::fabs(modL), std::fabs(modR)));
      }

      // Confirm delay tail survived intense automation
      double tailEnergy = 0.0;
      for (uint32_t i = 0; i < kBlock; ++i) tailEnergy += outL[i] * outL[i];
      CHECK(tailEnergy > 1e-6);
      hydra_dsp_destroy(h);
    }
  }
  std::printf("[UNIFIED_MOTION_RUNTIME_STABILITY_PASS] worstPeak=%.4f, worstDisp=%.2f samp, worstBlockUs=%.2f us\n",
              worstAudioPeak, worstModDisp, worstBlockUs);
}

} // namespace

int main() {
  testParamContract();
  testModesAndControlsDistinct();
  testClicklessSwitchingHighFeedbackMultiHeadReverseSpring();
  testRapidTransitionDerivativeAndContinuity();
  testTransitionContinuityMatrix();
  testMotionAmountAndStereoMotionDuringTransitions();
  testUnifiedMotionRuntimeStabilityStress();
  return 0;
}
