#include "tape_core.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#ifndef HYDRA_PHASE74B_MOTION_MODE
#define HYDRA_PHASE74B_MOTION_MODE 0
#endif

namespace {

uint64_t fnv1a(const float* data, size_t count) {
  uint64_t h = 14695981039346656037ull;
  const auto* bytes = reinterpret_cast<const uint8_t*>(data);
  const size_t n = count * sizeof(float);
  for (size_t i = 0; i < n; ++i) {
    h ^= bytes[i];
    h *= 1099511628211ull;
  }
  return h;
}

bool checkDeterminismAndFinite(float sampleRate) {
  constexpr uint32_t kBlock = 128;
  const uint32_t totalFrames = static_cast<uint32_t>(sampleRate * 3.0f);

  hydra::dsp::TapeCore core(sampleRate, 1600.0f, 3200.0f);
  if (!core.isValid()) return false;

  hydra::dsp::TapeParams p{};
  p.delayActive = true;
  p.delayTimeMs = 320.0f;
  p.feedback = 65.0f;
  p.delayWet = 60.0f;
  p.dryWet = 35.0f;
  p.wowRate = 0.9f;
  p.wowDepth = 25.0f;
  p.flutterRate = 6.5f;
  p.flutterDepth = 22.0f;
  p.tapeAge = 50.0f;
  p.activeHeads = 7;

  auto runOnce = [&](std::vector<float>& outAllL, std::vector<float>& outAllR) {
    core.updateParams(p);
    core.reset();
    outAllL.clear();
    outAllR.clear();
    std::vector<float> inL(kBlock), inR(kBlock), outL(kBlock), outR(kBlock);
    for (uint32_t off = 0; off < totalFrames; off += kBlock) {
      for (uint32_t i = 0; i < kBlock; ++i) {
        const float t = static_cast<float>(off + i) / sampleRate;
        const float env = (t < 1.0f) ? std::exp(-3.0f * t) : 0.0f;
        const float s = env * 0.45f * std::sin(2.0f * 3.14159265f * 220.0f * t);
        inL[i] = s;
        inR[i] = s * 0.92f;
      }
      core.processStereoBlock(inL.data(), inR.data(), outL.data(), outR.data(), kBlock);
      for (uint32_t i = 0; i < kBlock; ++i) {
        if (!std::isfinite(outL[i]) || !std::isfinite(outR[i])) return false;
      }
      outAllL.insert(outAllL.end(), outL.begin(), outL.end());
      outAllR.insert(outAllR.end(), outR.begin(), outR.end());
    }
    return true;
  };

  std::vector<float> l1, r1, l2, r2;
  if (!runOnce(l1, r1) || !runOnce(l2, r2)) {
    std::fprintf(stderr, "Non-finite output at %.0f Hz\n", sampleRate);
    return false;
  }
  if (fnv1a(l1.data(), l1.size()) != fnv1a(l2.data(), l2.size()) ||
      fnv1a(r1.data(), r1.size()) != fnv1a(r2.data(), r2.size())) {
    std::fprintf(stderr, "Determinism failed at %.0f Hz\n", sampleRate);
    return false;
  }
  return true;
}

bool checkHighFeedbackAndModes(float sampleRate) {
  constexpr uint32_t kBlock = 128;
  const uint32_t totalFrames = static_cast<uint32_t>(sampleRate * 8.0f);

  hydra::dsp::TapeCore core(sampleRate, 1600.0f, 3200.0f);
  if (!core.isValid()) return false;

  struct ModeCase {
    const char* name;
    bool pingPong;
    bool reverse;
    bool warp;
    int activeHeads;
    float feedback;
  };
  const ModeCase cases[] = {
      {"high_fb_single", false, false, false, 4, 95.0f},
      {"high_fb_multi", false, false, false, 7, 90.0f},
      {"ping_pong", true, false, false, 4, 78.0f},
      {"reverse_fb", false, true, false, 4, 75.0f},
      {"warp_active", false, false, true, 4, 65.0f},
  };

  std::vector<float> inL(kBlock), inR(kBlock), outL(kBlock), outR(kBlock);
  for (const auto& c : cases) {
    hydra::dsp::TapeParams p{};
    p.delayActive = true;
    p.delayTimeMs = 280.0f;
    p.feedback = c.feedback;
    p.delayWet = 75.0f;
    p.dryWet = 20.0f;
    p.wowRate = 1.1f;
    p.wowDepth = 45.0f;
    p.flutterRate = 7.5f;
    p.flutterDepth = 40.0f;
    p.tapeAge = 65.0f;
    p.pingPong = c.pingPong;
    p.reverse = c.reverse;
    p.warpActive = c.warp;
    p.activeHeads = c.activeHeads;
    core.updateParams(p);
    core.reset();

    float maxAbs = 0.0f;
    for (uint32_t off = 0; off < totalFrames; off += kBlock) {
      for (uint32_t i = 0; i < kBlock; ++i) {
        const float t = static_cast<float>(off + i) / sampleRate;
        const float s = (t < 0.6f) ? (0.4f * std::sin(2.0f * 3.14159265f * 330.0f * t) * std::exp(-4.0f * t)) : 0.0f;
        inL[i] = s;
        inR[i] = s;
      }
      core.processStereoBlock(inL.data(), inR.data(), outL.data(), outR.data(), kBlock);
      for (uint32_t i = 0; i < kBlock; ++i) {
        if (!std::isfinite(outL[i]) || !std::isfinite(outR[i])) {
          std::fprintf(stderr, "Non-finite in %s at %.0f Hz\n", c.name, sampleRate);
          return false;
        }
        maxAbs = std::max(maxAbs, std::max(std::fabs(outL[i]), std::fabs(outR[i])));
      }
    }
    if (maxAbs > 1.25f) {
      std::fprintf(stderr, "Runaway amplitude %.4f in %s at %.0f Hz\n", maxAbs, c.name, sampleRate);
      return false;
    }
  }
  return true;
}

bool checkAutomationNoGlitch(float sampleRate) {
  constexpr uint32_t kBlock = 128;
  const uint32_t blocks = static_cast<uint32_t>((sampleRate * 4.0f) / kBlock);

  hydra::dsp::TapeCore core(sampleRate, 1600.0f, 3200.0f);
  if (!core.isValid()) return false;

  hydra::dsp::TapeParams p{};
  p.delayActive = true;
  p.delayTimeMs = 260.0f;
  p.feedback = 55.0f;
  p.delayWet = 70.0f;
  p.dryWet = 0.0f;
  p.wowRate = 0.5f;
  p.wowDepth = 10.0f;
  p.flutterRate = 4.0f;
  p.flutterDepth = 10.0f;
  p.tapeAge = 35.0f;
  core.updateParams(p);
  core.reset();

  std::vector<float> inL(kBlock), inR(kBlock), outL(kBlock), outR(kBlock);
  float prevL = 0.0f;
  float maxJump = 0.0f;

  for (uint32_t b = 0; b < blocks; ++b) {
    const float u = static_cast<float>(b) / static_cast<float>(blocks);
    p.wowRate = 0.2f + 3.5f * u;
    p.wowDepth = 5.0f + 75.0f * u;
    p.flutterRate = 2.0f + 12.0f * u;
    p.flutterDepth = 5.0f + 70.0f * u;
    p.tapeAge = 20.0f + 70.0f * u;
    core.updateParams(p);

    for (uint32_t i = 0; i < kBlock; ++i) {
      const float t = static_cast<float>(b * kBlock + i) / sampleRate;
      const float s = 0.35f * std::sin(2.0f * 3.14159265f * 220.0f * t);
      inL[i] = s;
      inR[i] = s;
    }
    core.processStereoBlock(inL.data(), inR.data(), outL.data(), outR.data(), kBlock);
    for (uint32_t i = 0; i < kBlock; ++i) {
      if (!std::isfinite(outL[i]) || !std::isfinite(outR[i])) return false;
      const float diff = std::fabs(outL[i] - prevL);
      if (b > 20) maxJump = std::max(maxJump, diff);
      prevL = outL[i];
    }
  }
  if (maxJump > 0.35f) {
    std::fprintf(stderr, "Excessive step discontinuity %.4f during automation at %.0f Hz\n", maxJump, sampleRate);
    return false;
  }
  return true;
}

void benchmarkCpu44k() {
  constexpr float kSampleRate = 44100.0f;
  constexpr uint32_t kBlock = 128;
  constexpr uint32_t kBlocks = 2000;

  hydra::dsp::TapeCore core(kSampleRate, 1600.0f, 3200.0f);
  hydra::dsp::TapeParams p{};
  p.delayActive = true;
  p.delayTimeMs = 340.0f;
  p.feedback = 65.0f;
  p.delayWet = 50.0f;
  p.dryWet = 50.0f;
  p.activeHeads = 7;
  p.spring = true;
  p.wowDepth = 25.0f;
  p.flutterDepth = 25.0f;
  core.updateParams(p);
  core.reset();

  std::vector<float> inL(kBlock, 0.1f), inR(kBlock, 0.1f), outL(kBlock), outR(kBlock);
  const auto t0 = std::chrono::steady_clock::now();
  for (uint32_t b = 0; b < kBlocks; ++b) {
    core.processStereoBlock(inL.data(), inR.data(), outL.data(), outR.data(), kBlock);
  }
  const auto t1 = std::chrono::steady_clock::now();
  const double usTotal = std::chrono::duration<double, std::micro>(t1 - t0).count();
  std::printf("Mode M%d: %.3f us/block (128 frames @ 44.1 kHz, host x86_64)\n",
              HYDRA_PHASE74B_MOTION_MODE, usTotal / static_cast<double>(kBlocks));
}

}  // namespace

int main() {
  for (float fs : {44100.0f, 48000.0f}) {
    if (!checkDeterminismAndFinite(fs)) return 1;
    if (!checkHighFeedbackAndModes(fs)) return 2;
    if (!checkAutomationNoGlitch(fs)) return 3;
  }
  benchmarkCpu44k();
  std::printf("PASS phase74b_organic_motion_test (mode=M%d)\n", HYDRA_PHASE74B_MOTION_MODE);
  return 0;
}
