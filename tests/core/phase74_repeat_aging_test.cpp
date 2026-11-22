#include "tape_core.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

void check(bool ok, const char* msg) {
  if (!ok) {
    std::fprintf(stderr, "phase74 repeat aging FAIL: %s\n", msg);
    std::exit(1);
  }
}

void runScenario(hydra::dsp::TapeCore& core,
                 const hydra::dsp::TapeParams& params,
                 const std::vector<float>& inL,
                 const std::vector<float>& inR,
                 std::vector<float>& outL,
                 std::vector<float>& outR) {
  core.updateParams(params);
  constexpr uint32_t kBlock = 128;
  const uint32_t total = static_cast<uint32_t>(inL.size());
  outL.resize(total);
  outR.resize(total);
  for (uint32_t i = 0; i < total; i += kBlock) {
    const uint32_t n = std::min(kBlock, total - i);
    core.processStereoBlock(inL.data() + i, inR.data() + i,
                            outL.data() + i, outR.data() + i, n);
  }
}

void verifyFiniteAndBounded(const std::vector<float>& outL,
                            const std::vector<float>& outR,
                            const char* label) {
  double sumL = 0.0, sumR = 0.0;
  float peak = 0.0f;
  for (size_t i = 0; i < outL.size(); ++i) {
    if (!std::isfinite(outL[i]) || !std::isfinite(outR[i])) {
      std::fprintf(stderr, "Non-finite sample in %s at %zu\n", label, i);
      std::exit(1);
    }
    sumL += outL[i];
    sumR += outR[i];
    peak = std::max(peak, std::max(std::fabs(outL[i]), std::fabs(outR[i])));
  }
  check(peak <= 1.0f, label);
  const double meanL = std::fabs(sumL / static_cast<double>(outL.size()));
  const double meanR = std::fabs(sumR / static_cast<double>(outR.size()));
  check(meanL < 0.01 && meanR < 0.01, "DC offset exceeded 0.01");
}

}  // namespace

int main() {
  constexpr float kFs = 48000.0f;
  hydra::dsp::TapeCore core(kFs, 2000.0f, 8100.0f);
  check(core.isValid(), "TapeCore allocation");

  const size_t N = static_cast<size_t>(kFs * 2.0f);
  std::vector<float> impulseL(N, 0.0f), impulseR(N, 0.0f);
  for (size_t i = 0; i < 600; ++i) {
    const float env = std::exp(-static_cast<float>(i) / 150.0f);
    const float s = 0.6f * env * std::sin(2.0f * 3.14159265f * 440.0f * i / kFs);
    impulseL[i + 128] = s;
    impulseR[i + 128] = s * 0.9f;
  }

  hydra::dsp::TapeParams base{};
  base.delayActive = true;
  base.delayTimeMs = 240.0f;
  base.feedback = 65.0f;
  base.dryWet = 50.0f;
  base.delayWet = 75.0f;
  base.tapeAge = 40.0f;
  base.tapeSpeed = 50.0f;
  base.noise = 0.0f;
  base.activeHeads = 7;

  // 1. Determinism (two fresh instances bit-exact) & reset() state-clearing check
  std::vector<float> run1L, run1R, run2L, run2R;
  runScenario(core, base, impulseL, impulseR, run1L, run1R);
  verifyFiniteAndBounded(run1L, run1R, "base_run1");
  {
    hydra::dsp::TapeCore freshCore(kFs, 2000.0f, 8100.0f);
    runScenario(freshCore, base, impulseL, impulseR, run2L, run2R);
    for (size_t i = 0; i < N; ++i) {
      check(run1L[i] == run2L[i] && run1R[i] == run2R[i], "fresh instance determinism");
    }
  }
  core.reset();
  {
    std::vector<float> zeroL(N, 0.0f), zeroR(N, 0.0f);
    runScenario(core, base, zeroL, zeroR, run2L, run2R);
    float postResetPeak = 0.0f;
    for (size_t i = 0; i < N; ++i) {
      postResetPeak = std::max(postResetPeak, std::max(std::fabs(run2L[i]), std::fabs(run2R[i])));
    }
    check(postResetPeak < 1e-6f, "reset must clear all delay and aging state");
  }

  // 2. High feedback + extreme tapeAge=100
  core.reset();
  auto highFb = base;
  highFb.feedback = 95.0f;
  highFb.drive = 80.0f;
  highFb.tapeAge = 100.0f;
  runScenario(core, highFb, impulseL, impulseR, run1L, run1R);
  verifyFiniteAndBounded(run1L, run1R, "high_fb_age100");

  // 3. Extreme tapeAge=0 + Ping-Pong + Reverse
  core.reset();
  auto revPp = base;
  revPp.tapeAge = 0.0f;
  revPp.pingPong = true;
  revPp.reverse = true;
  revPp.reverseMode = 1;
  revPp.feedback = 70.0f;
  runScenario(core, revPp, impulseL, impulseR, run1L, run1R);
  verifyFiniteAndBounded(run1L, run1R, "reverse_pingpong_age0");

  // 4. Silence with noise=0
  core.reset();
  std::vector<float> silL(N, 0.0f), silR(N, 0.0f);
  runScenario(core, base, silL, silR, run1L, run1R);
  float silPeak = 0.0f;
  for (size_t i = 0; i < N; ++i) {
    silPeak = std::max(silPeak, std::max(std::fabs(run1L[i]), std::fabs(run1R[i])));
  }
  check(silPeak < 1e-6f, "silence with noise=0 should remain silent");

  // 5. Parameter automation sweep (no clicks > 0.35 sample-to-sample during steady tone)
  core.reset();
  std::vector<float> toneL(N), toneR(N);
  for (size_t i = 0; i < N; ++i) {
    const float s = 0.25f * std::sin(2.0f * 3.14159265f * 330.0f * i / kFs);
    toneL[i] = s;
    toneR[i] = s;
  }
  auto autoP = base;
  float maxJump = 0.0f;
  float prevL = 0.0f;
  for (size_t i = 0; i < N; i += 128) {
    const float t = static_cast<float>(i) / static_cast<float>(N);
    autoP.tapeAge = 10.0f + 85.0f * t;
    autoP.feedback = 25.0f + 60.0f * t;
    autoP.tapeSpeed = 20.0f + 60.0f * (1.0f - t);
    core.updateParams(autoP);
    float oL[128], oR[128];
    const uint32_t n = static_cast<uint32_t>(std::min<size_t>(128, N - i));
    core.processStereoBlock(toneL.data() + i, toneR.data() + i, oL, oR, n);
    for (uint32_t k = 0; k < n; ++k) {
      check(std::isfinite(oL[k]) && std::isfinite(oR[k]), "automation finite");
      if (i + k > 256) {
        maxJump = std::max(maxJump, std::fabs(oL[k] - prevL));
      }
      prevL = oL[k];
    }
  }
  check(maxJump < 0.25f, "automation click threshold");

  std::puts("Phase 7.4A repeat aging safety test PASS");
  return 0;
}
