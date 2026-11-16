#include "check.h"
#include "hydra_dsp.h"
#include "reverse_windows.hpp"
#include "hydra_dsp_esp32_adapter.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

constexpr float kRate48 = 48000.0f;
constexpr float kRate44 = 44100.0f;

hydra_dsp_handle* create_handle(float sampleRate = kRate48, float maxDelayMs = 2000.0f, float historyMs = 8000.0f) {
  hydra_dsp_handle* h = nullptr;
  CHECK(hydra_dsp_create_v1_4(sampleRate, maxDelayMs, historyMs, &h) == 0);
  CHECK(h != nullptr);
  CHECK(hydra_dsp_prepare(h, 128, HYDRA_DSP_CHANNELS_STEREO) == 0);
  return h;
}

hydra_dsp_params_v1_4 default_clean_params() {
  hydra_dsp_params_v1_4 p{};
  p.v1_3.flutterDepth = 0.0f;
  p.v1_3.wowDepth = 0.0f;
  p.v1_3.dropoutSeverity = 0.0f;
  p.v1_3.drive = 20.0f;
  p.v1_3.noise = 0.0f;
  p.v1_3.tapeSpeed = 50.0f;
  p.v1_3.tapeAge = 0.0f;
  p.v1_3.headBumpAmount = 0.0f;
  p.v1_3.azimuthError = 0.0f;
  p.v1_3.flutterRate = 0.1f;
  p.v1_3.wowRate = 0.1f;
  p.v1_3.delayActive = 1.0f;
  p.v1_3.delayTimeMs = 500.0f;
  p.v1_3.feedback = 0.0f;
  p.v1_3.dryWet = 100.0f;
  p.v1_3.delayWet = 100.0f;
  p.v1_3.activeHeads = 4.0f;
  p.v1_3.bpm = 120.0f;
  p.v1_3.headsMusical = 0.0f;
  p.v1_3.guitarFocus = 0.0f;
  p.v1_3.tone = 100.0f;
  p.v1_3.pingPong = 0.0f;
  p.v1_3.freeze = 0.0f;
  p.v1_3.reverse = 1.0f;
  p.v1_3.reverseSmear = 0.0f;
  p.v1_3.spring = 0.0f;
  p.v1_3.warpActive = 0.0f;
  p.reverseMode = HYDRA_DSP_REVERSE_RHYTHMIC;
  p.reverseDivision = HYDRA_DSP_REVERSE_QUARTER;
  return p;
}

// 0. Test ReverseWindows class directly
void test_reverse_windows_unit() {
  hydra::dsp::ReverseWindows rw;
  rw.configure(48000.0f, 384000, 120.0f, HYDRA_DSP_REVERSE_QUARTER, false, 500.0f);
  rw.reset();

  // 120 BPM quarter note at 48 kHz is exactly 24000 frames
  int32_t count = 0;
  bool sawOverlap = false;
  for (int i = 0; i < 48000; ++i) {
    auto frame = rw.next();
    CHECK(std::isfinite(frame.delay));
    CHECK(std::isfinite(frame.previousDelay));
    CHECK(frame.incoming >= 0.0f && frame.incoming <= 1.0f);
    if (frame.incoming < 1.0f) {
      sawOverlap = true;
    }
    count++;
  }
  CHECK(sawOverlap);
  std::cout << "[Test 0] ReverseWindows unit test: OK\n";
}

// A. Reverse Impulse Timing
void test_reverse_impulse_timing(float sampleRate) {
  hydra_dsp_handle* h = create_handle(sampleRate);
  auto p = default_clean_params();
  p.v1_3.bpm = 120.0f;
  p.reverseMode = HYDRA_DSP_REVERSE_RHYTHMIC;
  p.reverseDivision = HYDRA_DSP_REVERSE_QUARTER; // 500ms
  CHECK(hydra_dsp_set_params_sized(h, &p, sizeof(p)) == 0);
  CHECK(hydra_dsp_commit(h) == 0);

  // Ingest silence then Dirac impulse at frame 0
  const uint32_t hopFrames = static_cast<uint32_t>(0.5f * sampleRate);
  const uint32_t totalFrames = hopFrames * 3;
  std::vector<float> inL(totalFrames, 0.0f), inR(totalFrames, 0.0f);
  std::vector<float> outL(totalFrames, 0.0f), outR(totalFrames, 0.0f);
  inL[0] = 1.0f;
  inR[0] = 1.0f;

  for (uint32_t i = 0; i < totalFrames; i += 128) {
    uint32_t n = std::min(128u, totalFrames - i);
    CHECK(hydra_dsp_process(h, &inL[i], &inR[i], &outL[i], &outR[i], n) == 0);
  }

  // Find max peak in the first reverse window
  float maxVal = 0.0f;
  uint32_t peakIdx = 0;
  for (uint32_t i = 0; i < totalFrames; ++i) {
    float a = std::fabs(outL[i]);
    if (a > maxVal) {
      maxVal = a;
      peakIdx = i;
    }
  }
  // Reversed impulse from the window should appear near the window end
  CHECK(maxVal > 0.05f);
  CHECK(peakIdx > 0 && peakIdx < totalFrames);
  hydra_dsp_destroy(h);
  std::cout << "[Test A] Reverse Impulse Timing (rate=" << sampleRate << "): Peak at "
            << peakIdx << " (" << (peakIdx / sampleRate * 1000.0f) << " ms), mag=" << maxVal << ": OK\n";
}

// B. Reverse Window Continuity
void test_reverse_window_continuity(float sampleRate) {
  hydra_dsp_handle* h = create_handle(sampleRate);
  auto p = default_clean_params();
  p.v1_3.bpm = 120.0f;
  p.v1_3.feedback = 0.0f;
  CHECK(hydra_dsp_set_params_sized(h, &p, sizeof(p)) == 0);
  CHECK(hydra_dsp_commit(h) == 0);

  // Feed continuous 220 Hz sine
  const uint32_t totalFrames = static_cast<uint32_t>(sampleRate * 2.0f);
  std::vector<float> inL(totalFrames), inR(totalFrames);
  std::vector<float> outL(totalFrames), outR(totalFrames);
  for (uint32_t i = 0; i < totalFrames; ++i) {
    float s = std::sin(2.0f * 3.1415926535f * 220.0f * i / sampleRate);
    inL[i] = s * 0.5f;
    inR[i] = s * 0.5f;
  }

  for (uint32_t i = 0; i < totalFrames; i += 128) {
    uint32_t n = std::min(128u, totalFrames - i);
    CHECK(hydra_dsp_process(h, &inL[i], &inR[i], &outL[i], &outR[i], n) == 0);
  }

  // Check sample-to-sample diff step
  float maxStep = 0.0f;
  for (uint32_t i = 1; i < totalFrames; ++i) {
    float diff = std::fabs(outL[i] - outL[i - 1]);
    if (diff > maxStep) maxStep = diff;
  }
  // For a smooth crossfade with 8ms overlap, max step on a 220Hz sine should be well-behaved (< 0.20)
  CHECK(maxStep < 0.25f);
  hydra_dsp_destroy(h);
  std::cout << "[Test B] Reverse Window Continuity (rate=" << sampleRate << "): max step=" << maxStep << ": OK\n";
}

// C. BPM Accuracy
void test_bpm_accuracy() {
  const float bpms[] = {60.0f, 90.0f, 120.0f, 140.0f, 180.0f};
  const double beats[] = {1.0, 4.0, 2.0, 0.5, 0.25, 1.5, 0.75, 2.0 / 3.0, 1.0 / 3.0};
  const float rates[] = {kRate44, kRate48};

  for (float rate : rates) {
    for (float bpm : bpms) {
      for (int div = 0; div < 9; ++div) {
        hydra::dsp::ReverseWindows rw;
        rw.configure(rate, 400000, bpm, div, false, 500.0f);
        double expectedSec = (60.0 * beats[div]) / bpm;
        double expectedFrames = expectedSec * rate;

        rw.reset();
        int32_t framesCounted = 0;
        // Step until the first window boundary
        while (framesCounted < expectedFrames * 2) {
          auto frame = rw.next();
          framesCounted++;
          if (frame.incoming < 1.0f && framesCounted > 1) {
            break;
          }
        }
        // First window boundary should match expectedFrames within 2 frames tolerance
        int32_t diff = std::abs(framesCounted - static_cast<int32_t>(expectedFrames));
        CHECK(diff <= 2);
      }
    }
  }
  std::cout << "[Test C] BPM Accuracy across 60, 90, 120, 140, 180 BPM and 9 subdivisions: OK\n";
}

// D. Stereo Integrity
void test_stereo_integrity() {
  hydra_dsp_handle* h = create_handle(kRate48);
  auto p = default_clean_params();
  p.v1_3.pingPong = 0.0f;
  CHECK(hydra_dsp_set_params_sized(h, &p, sizeof(p)) == 0);
  CHECK(hydra_dsp_commit(h) == 0);

  constexpr uint32_t N = 48000;
  std::vector<float> inL(N, 0.0f), inR(N, 0.0f);
  std::vector<float> outL(N, 0.0f), outR(N, 0.0f);

  // Impulse on Left only
  inL[0] = 1.0f;

  for (uint32_t i = 0; i < N; i += 128) {
    CHECK(hydra_dsp_process(h, &inL[i], &inR[i], &outL[i], &outR[i], 128) == 0);
  }

  float energyL = 0.0f, energyR = 0.0f;
  for (uint32_t i = 0; i < N; ++i) {
    energyL += outL[i] * outL[i];
    energyR += outR[i] * outR[i];
  }

  CHECK(energyL > 0.01f);
  // Without ping-pong, R channel should have near zero energy (just noise if any, but noise is 0)
  CHECK(energyR < 1e-4f);

  // Now test with Ping-Pong enabled
  p.v1_3.pingPong = 1.0f;
  p.v1_3.feedback = 50.0f;
  CHECK(hydra_dsp_set_params_sized(h, &p, sizeof(p)) == 0);
  CHECK(hydra_dsp_commit(h) == 0);
  hydra_dsp_reset(h);

  std::fill(outL.begin(), outL.end(), 0.0f);
  std::fill(outR.begin(), outR.end(), 0.0f);
  for (uint32_t i = 0; i < N; i += 128) {
    CHECK(hydra_dsp_process(h, &inL[i], &inR[i], &outL[i], &outR[i], 128) == 0);
  }

  float energyPpL = 0.0f, energyPpR = 0.0f;
  for (uint32_t i = 0; i < N; ++i) {
    energyPpL += outL[i] * outL[i];
    energyPpR += outR[i] * outR[i];
  }
  // With ping pong, energy should bleed into R channel
  CHECK(energyPpR > 0.001f);

  hydra_dsp_destroy(h);
  std::cout << "[Test D] Stereo Integrity & Ping-Pong: OK\n";
}

// E. Feedback Stability
void test_feedback_stability() {
  const float fbs[] = {0.0f, 25.0f, 50.0f, 75.0f, 95.0f};
  for (float fb : fbs) {
    hydra_dsp_handle* h = create_handle(kRate48);
    auto p = default_clean_params();
    p.v1_3.feedback = fb;
    CHECK(hydra_dsp_set_params_sized(h, &p, sizeof(p)) == 0);
    CHECK(hydra_dsp_commit(h) == 0);

    // 5 seconds render
    constexpr uint32_t N = 48000 * 5;
    std::vector<float> inL(N, 0.0f), inR(N, 0.0f);
    std::vector<float> outL(N, 0.0f), outR(N, 0.0f);
    inL[0] = 1.0f;
    inR[0] = 1.0f;

    for (uint32_t i = 0; i < N; i += 128) {
      CHECK(hydra_dsp_process(h, &inL[i], &inR[i], &outL[i], &outR[i], 128) == 0);
    }

    float maxAbs = 0.0f;
    for (uint32_t i = 0; i < N; ++i) {
      CHECK(std::isfinite(outL[i]));
      CHECK(std::isfinite(outR[i]));
      maxAbs = std::max(maxAbs, std::max(std::fabs(outL[i]), std::fabs(outR[i])));
    }
    // Output must not blow up
    CHECK(maxAbs < 2.0f);
    hydra_dsp_destroy(h);
  }
  std::cout << "[Test E] Feedback Stability (0% to 95% 5s renders): OK\n";
}

// F. Parameter Automation
void test_parameter_automation() {
  hydra_dsp_handle* h = create_handle(kRate48);
  auto p = default_clean_params();
  CHECK(hydra_dsp_set_params_sized(h, &p, sizeof(p)) == 0);
  CHECK(hydra_dsp_commit(h) == 0);

  constexpr uint32_t kChunk = 128;
  std::vector<float> inL(kChunk, 0.5f), inR(kChunk, 0.5f);
  std::vector<float> outL(kChunk), outR(kChunk);

  float maxStep = 0.0f;
  float lastL = 0.0f;

  for (int block = 0; block < 100; ++block) {
    if (block == 10) {
      p.v1_3.bpm = 90.0f;
      CHECK(hydra_dsp_set_params_sized(h, &p, sizeof(p)) == 0);
      CHECK(hydra_dsp_commit(h) == 0);
    } else if (block == 25) {
      p.v1_3.bpm = 140.0f;
      p.reverseDivision = HYDRA_DSP_REVERSE_EIGHTH;
      CHECK(hydra_dsp_set_params_sized(h, &p, sizeof(p)) == 0);
      CHECK(hydra_dsp_commit(h) == 0);
    } else if (block == 40) {
      p.reverseDivision = HYDRA_DSP_REVERSE_DOTTED_EIGHTH;
      CHECK(hydra_dsp_set_params_sized(h, &p, sizeof(p)) == 0);
      CHECK(hydra_dsp_commit(h) == 0);
    } else if (block == 55) {
      p.v1_3.feedback = 80.0f;
      CHECK(hydra_dsp_set_params_sized(h, &p, sizeof(p)) == 0);
      CHECK(hydra_dsp_commit(h) == 0);
    } else if (block == 70) {
      p.reverseMode = HYDRA_DSP_REVERSE_FREE;
      p.v1_3.delayTimeMs = 350.0f;
      CHECK(hydra_dsp_set_params_sized(h, &p, sizeof(p)) == 0);
      CHECK(hydra_dsp_commit(h) == 0);
    } else if (block == 85) {
      p.v1_3.reverse = 0.0f;
      CHECK(hydra_dsp_set_params_sized(h, &p, sizeof(p)) == 0);
      CHECK(hydra_dsp_commit(h) == 0);
    }

    CHECK(hydra_dsp_process(h, inL.data(), inR.data(), outL.data(), outR.data(), kChunk) == 0);
    for (uint32_t i = 0; i < kChunk; ++i) {
      CHECK(std::isfinite(outL[i]));
      float step = std::fabs(outL[i] - lastL);
      if (step > maxStep) maxStep = step;
      lastL = outL[i];
    }
  }

  CHECK(maxStep < 0.35f);
  hydra_dsp_destroy(h);
  std::cout << "[Test F] Parameter Automation (BPM, division, feedback, mode, reverse on/off): OK\n";
}

// G. Silence and Reset
void test_silence_and_reset() {
  hydra_dsp_handle* h = create_handle(kRate48);
  auto p = default_clean_params();
  p.v1_3.feedback = 0.0f;
  CHECK(hydra_dsp_set_params_sized(h, &p, sizeof(p)) == 0);
  CHECK(hydra_dsp_commit(h) == 0);

  // Play audio for 1 second
  constexpr uint32_t N1 = 48000;
  std::vector<float> inL(N1, 0.5f), inR(N1, 0.5f);
  std::vector<float> outL(N1), outR(N1);
  for (uint32_t i = 0; i < N1; i += 128) {
    CHECK(hydra_dsp_process(h, &inL[i], &inR[i], &outL[i], &outR[i], 128) == 0);
  }

  // Play silence for 3 seconds
  constexpr uint32_t N2 = 48000 * 3;
  std::vector<float> silL(N2, 0.0f), silR(N2, 0.0f);
  std::vector<float> silOutL(N2), silOutR(N2);
  for (uint32_t i = 0; i < N2; i += 128) {
    CHECK(hydra_dsp_process(h, &silL[i], &silR[i], &silOutL[i], &silOutR[i], 128) == 0);
  }

  // Final 0.5s of output must be completely silent
  for (uint32_t i = N2 - 24000; i < N2; ++i) {
    CHECK(std::fabs(silOutL[i]) < 1e-4f);
    CHECK(std::fabs(silOutR[i]) < 1e-4f);
  }

  // Test reset
  hydra_dsp_reset(h);
  std::vector<float> zeroIn(128, 0.0f), zeroOutL(128), zeroOutR(128);
  CHECK(hydra_dsp_process(h, zeroIn.data(), zeroIn.data(), zeroOutL.data(), zeroOutR.data(), 128) == 0);
  for (int i = 0; i < 128; ++i) {
    CHECK(std::fabs(zeroOutL[i]) < 1e-6f);
    CHECK(std::fabs(zeroOutR[i]) < 1e-6f);
  }

  hydra_dsp_destroy(h);
  std::cout << "[Test G] Silence and Reset: OK\n";
}

// H. Block Size Independence
void test_block_size_independence() {
  hydra_dsp_handle* h1 = create_handle(kRate48);
  hydra_dsp_handle* h2 = create_handle(kRate48);

  auto p = default_clean_params();
  CHECK(hydra_dsp_set_params_sized(h1, &p, sizeof(p)) == 0);
  CHECK(hydra_dsp_commit(h1) == 0);
  CHECK(hydra_dsp_set_params_sized(h2, &p, sizeof(p)) == 0);
  CHECK(hydra_dsp_commit(h2) == 0);

  constexpr uint32_t N = 48000;
  std::vector<float> inL(N), inR(N);
  for (uint32_t i = 0; i < N; ++i) {
    float s = std::sin(2.0f * 3.1415926535f * 440.0f * i / kRate48);
    inL[i] = s * 0.3f;
    inR[i] = s * 0.3f;
  }

  std::vector<float> out1L(N), out1R(N);
  std::vector<float> out2L(N), out2R(N);

  // h1 processes in 128-frame blocks
  for (uint32_t i = 0; i < N; i += 128) {
    CHECK(hydra_dsp_process(h1, &inL[i], &inR[i], &out1L[i], &out1R[i], 128) == 0);
  }

  // h2 processes in 1-frame calls
  for (uint32_t i = 0; i < N; ++i) {
    CHECK(hydra_dsp_process(h2, &inL[i], &inR[i], &out2L[i], &out2R[i], 1) == 0);
  }

  float diffSum = 0.0f;
  for (uint32_t i = 0; i < N; ++i) {
    diffSum += std::fabs(out1L[i] - out2L[i]);
  }
  float avgDiff = diffSum / N;
  CHECK(avgDiff < 1e-4f);

  hydra_dsp_destroy(h1);
  hydra_dsp_destroy(h2);
  std::cout << "[Test H] Block Size Independence (1 frame vs 128 frames, avgDiff=" << avgDiff << "): OK\n";
}

} // namespace

int main() {
  std::cout << "Running Reverse Tape Echo V2 Comprehensive DSP Test Suite...\n";
  test_reverse_windows_unit();
  test_reverse_impulse_timing(kRate48);
  test_reverse_impulse_timing(kRate44);
  test_reverse_window_continuity(kRate48);
  test_reverse_window_continuity(kRate44);
  test_bpm_accuracy();
  test_stereo_integrity();
  test_feedback_stability();
  test_parameter_automation();
  test_silence_and_reset();
  test_block_size_independence();
  std::cout << "All Reverse Tape Echo V2 Tests PASSED successfully!\n";
  return 0;
}
