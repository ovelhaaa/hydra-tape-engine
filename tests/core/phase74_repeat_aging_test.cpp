#include "repeat_aging_experiment.hpp"
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

void testTransparencyConditions() {
  using namespace hydra::dsp::experimental;
  constexpr float kFs = 48000.0f;
  RepeatAgingCoeffs coeffs;
  coeffs.update(kFs);

  // Condition A: Zero intensity must return exact input even after internal state warmup
  {
    RepeatAgingChannelState st1, st2;
    st1.reset();
    st2.reset();
    for (int i = 0; i < 4800; ++i) {
      const float x = 0.7f * std::sin(2.0f * 3.14159265f * 2800.0f * i / kFs);
      (void)processDynamicSpectralWear(x, st1, coeffs, 0.3f, 0.5f, 1.0f);
      (void)processTransientSoftening(x, st2, coeffs, 0.5f, 1.0f);
    }
    check(st1.envSlow > 0.1f && st1.hfEnv > 0.1f, "C1 warmup state should be non-zero");

    float c1Amt = -1.0f, c2Mod = -1.0f;
    for (int i = 0; i < 2048; ++i) {
      const float x = 0.45f * std::sin(2.0f * 3.14159265f * 4500.0f * i / kFs);
      const float y1 = processDynamicSpectralWear(x, st1, coeffs, 0.3f, 0.5f, 0.0f, &c1Amt);
      const float y2 = processTransientSoftening(x, st2, coeffs, 0.5f, 0.0f, &c2Mod);
      check(c1Amt == 0.0f && c2Mod == 0.0f, "zero depthScale must yield zero effective amount");
      check(y1 == x, "C1 zero-intensity must be bit-exact passthrough after warmup");
      check(y2 == x, "C2 zero-intensity must be bit-exact passthrough after warmup");
    }
    // Detector must keep tracking signal even when depthScale == 0.0f
    check(st1.envSlow > 0.1f && st1.hfEnv > 0.1f, "C1 detector must remain active at zero intensity");
    check(st2.slowEnv > 0.1f, "C2 detector must remain active at zero intensity");
  }

  // Condition B: Sustained content must not receive permanent softening in C2
  {
    RepeatAgingChannelState st;
    st.reset();
    const int total = static_cast<int>(kFs * 1.0f);
    const int settle = static_cast<int>(kFs * 0.40f);
    float maxModAfterSettle = 0.0f;
    float maxDiffAfterSettle = 0.0f;
    for (int i = 0; i < total; ++i) {
      const float ramp = std::min(1.0f, static_cast<float>(i) / (0.05f * kFs));
      const float x = ramp * 0.35f * (
          std::sin(2.0f * 3.14159265f * 330.0f * i / kFs) +
          0.4f * std::sin(2.0f * 3.14159265f * 660.0f * i / kFs) +
          0.2f * std::sin(2.0f * 3.14159265f * 2640.0f * i / kFs));
      float mod = 0.0f;
      const float y = processTransientSoftening(x, st, coeffs, 0.5f, 1.0f, &mod);
      if (i >= settle) {
        maxModAfterSettle = std::max(maxModAfterSettle, mod);
        maxDiffAfterSettle = std::max(maxDiffAfterSettle, std::fabs(y - x));
      }
    }
    if (!(maxModAfterSettle < 1e-4f)) {
      std::fprintf(stderr, "maxModAfterSettle=%g maxDiffAfterSettle=%g\n",
                   maxModAfterSettle, maxDiffAfterSettle);
    }
    check(maxModAfterSettle < 1e-4f, "C2 must return to zero modulation on sustained tone");
    check(maxDiffAfterSettle < 1e-4f, "C2 must be transparent on sustained tone");
  }

  // Condition C: Isolated transient produces measurable modification during attack, then recovers
  {
    RepeatAgingChannelState st;
    st.reset();
    const int total = static_cast<int>(kFs * 0.5f);
    const int hitSample = static_cast<int>(kFs * 0.05f);
    float peakAttackMod = 0.0f;
    float peakAttackDiff = 0.0f;
    float lateMod = 0.0f;
    float lateDiff = 0.0f;
    for (int i = 0; i < total; ++i) {
      float x = 0.0f;
      if (i >= hitSample && i < hitSample + 2400) {
        const int k = i - hitSample;
        const float env = std::exp(-static_cast<float>(k) / 400.0f);
        x = 0.6f * env * (
            std::sin(2.0f * 3.14159265f * 880.0f * k / kFs) +
            0.6f * std::sin(2.0f * 3.14159265f * 3520.0f * k / kFs));
      } else if (i >= hitSample + 2400) {
        // Low-level steady probe after transient to verify transparency recovery
        x = 0.08f * std::sin(2.0f * 3.14159265f * 3000.0f * i / kFs);
      }
      float mod = 0.0f;
      const float y = processTransientSoftening(x, st, coeffs, 0.5f, 1.0f, &mod);
      if (i >= hitSample && i < hitSample + static_cast<int>(0.025f * kFs)) {
        peakAttackMod = std::max(peakAttackMod, mod);
        peakAttackDiff = std::max(peakAttackDiff, std::fabs(y - x));
      }
      if (i >= total - 1024) {
        lateMod = std::max(lateMod, mod);
        lateDiff = std::max(lateDiff, std::fabs(y - x));
      }
    }
    check(peakAttackMod > 0.25f, "C2 must detect isolated transient attack");
    check(peakAttackDiff > 0.02f, "C2 must measurably soften isolated transient");
    check(lateMod < 1e-3f, "C2 must recover to transparency after transient");
    check(lateDiff < 1e-3f, "C2 probe must be transparent after recovery");
  }
}

}  // namespace

int main() {
  testTransparencyConditions();

  constexpr float kFs = 48000.0f;
  hydra::dsp::TapeCore core(kFs, 2000.0f, 8100.0f);
  check(core.isValid(), "TapeCore allocation");

  const size_t N = static_cast<size_t>(kFs * 2.0f);
  std::vector<float> impulseL(N, 0.0f), impulseR(N, 0.0f);
  for (size_t i = 0; i < 600; ++i) {
    const float env = std::exp(-static_cast<float>(i) / 150.0f);
    const float s = 0.6f * env * (
        std::sin(2.0f * 3.14159265f * 440.0f * i / kFs) +
        0.5f * std::sin(2.0f * 3.14159265f * 2200.0f * i / kFs));
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

  // 5. Parameter automation sweep (rapid Tape Age, Tape Speed, Feedback)
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

  std::puts("Phase 7.4A repeat aging safety & transparency test PASS");
  return 0;
}
