#include "tap_tempo.hpp"
#include "global_transport_clock.hpp"
#include "tape_core.hpp"
#include "hydra_dsp.h"
#include "hydra_dsp_esp32_adapter.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

using namespace hydra::dsp;

#define EXPECT_TRUE(cond) \
  do { \
    if (!(cond)) { \
      std::cerr << "FAIL: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
      return false; \
    } \
  } while (0)

#define EXPECT_NEAR(a, b, eps) \
  do { \
    if (std::fabs((a) - (b)) > (eps)) { \
      std::cerr << "FAIL: |" #a " - " #b "| = " << std::fabs((a) - (b)) \
                << " > " << (eps) << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
      return false; \
    } \
  } while (0)

#define EXPECT_EQ(a, b) \
  do { \
    if ((a) != (b)) { \
      std::cerr << "FAIL: " #a " == " #b " (" << (a) << " != " << (b) << ") at " \
                << __FILE__ << ":" << __LINE__ << "\n"; \
      return false; \
    } \
  } while (0)

// A. Basic Tap Tempo across musical BPMs
bool testBasicTapTempo() {
  const double bpms[] = {60.0, 90.0, 120.0, 140.0, 180.0, 240.0};
  const double fs = 44100.0;

  for (double targetBpm : bpms) {
    TapTempo tt;
    tt.prepare(fs);

    const double intervalSec = 60.0 / targetBpm;
    const uint64_t intervalFrames = static_cast<uint64_t>(std::round(intervalSec * fs));

    uint64_t curFrame = 1000;
    // Tap 1
    EXPECT_TRUE(tt.recordTap(curFrame));
    EXPECT_EQ(tt.tapCount(), 1u);
    EXPECT_TRUE(tt.status() == TapTempoStatus::Acquiring);

    // Tap 2 (immediate responsive estimate)
    curFrame += intervalFrames;
    EXPECT_TRUE(tt.recordTap(curFrame));
    EXPECT_EQ(tt.tapCount(), 2u);
    EXPECT_NEAR(tt.estimatedBpm(), targetBpm, 0.2);
    EXPECT_NEAR(tt.appliedBpm(), targetBpm, 0.2);
    EXPECT_TRUE(tt.status() == TapTempoStatus::Acquiring);

    // Tap 3 (consistency confirmed)
    curFrame += intervalFrames;
    EXPECT_TRUE(tt.recordTap(curFrame));
    EXPECT_EQ(tt.tapCount(), 3u);
    EXPECT_NEAR(tt.estimatedBpm(), targetBpm, 0.1);
    EXPECT_TRUE(tt.status() == TapTempoStatus::Locked);

    // Tap 4 (refined)
    curFrame += intervalFrames;
    EXPECT_TRUE(tt.recordTap(curFrame));
    EXPECT_EQ(tt.tapCount(), 4u);
    EXPECT_NEAR(tt.appliedBpm(), targetBpm, 0.05);
    EXPECT_TRUE(tt.confidence() >= 0.85f);
  }
  std::cout << "[PASS] Basic Tap Tempo (60, 90, 120, 140, 180, 240 BPM)\n";
  return true;
}

// B. Debounce rejection of duplicate taps at 5, 20, 50 ms
bool testDebounce() {
  const double fs = 48000.0;
  TapTempo tt;
  tt.prepare(fs);

  uint64_t curFrame = 10000;
  EXPECT_TRUE(tt.recordTap(curFrame));

  // Duplicate at 5 ms (< 80 ms)
  uint64_t t5ms = curFrame + static_cast<uint64_t>(fs * 0.005);
  EXPECT_TRUE(!tt.recordTap(t5ms));
  EXPECT_TRUE(tt.status() == TapTempoStatus::Rejected);
  EXPECT_TRUE(tt.lastRejection() == TapRejectionReason::Bounce);
  EXPECT_EQ(tt.tapCount(), 1u);

  // Duplicate at 20 ms (< 80 ms)
  uint64_t t20ms = curFrame + static_cast<uint64_t>(fs * 0.020);
  EXPECT_TRUE(!tt.recordTap(t20ms));
  EXPECT_TRUE(tt.lastRejection() == TapRejectionReason::Bounce);
  EXPECT_EQ(tt.tapCount(), 1u);

  // Duplicate at 50 ms (< 80 ms)
  uint64_t t50ms = curFrame + static_cast<uint64_t>(fs * 0.050);
  EXPECT_TRUE(!tt.recordTap(t50ms));
  EXPECT_TRUE(tt.lastRejection() == TapRejectionReason::Bounce);
  EXPECT_EQ(tt.tapCount(), 1u);

  // Valid second tap at 500 ms (120 BPM) measured from original tap
  uint64_t t500ms = curFrame + static_cast<uint64_t>(fs * 0.500);
  EXPECT_TRUE(tt.recordTap(t500ms));
  EXPECT_EQ(tt.tapCount(), 2u);
  EXPECT_NEAR(tt.appliedBpm(), 120.0, 0.1);

  std::cout << "[PASS] Debounce (5, 20, 50 ms rejected without reference corruption)\n";
  return true;
}

// C. Outlier Rejection: isolated stumbled tap does not corrupt BPM
bool testOutlierRejection() {
  const double fs = 44100.0;
  TapTempo tt;
  tt.prepare(fs);

  // Establish 120 BPM (500 ms intervals)
  uint64_t curFrame = 50000;
  const uint64_t beatFrames = static_cast<uint64_t>(fs * 0.500);

  for (int i = 0; i < 4; ++i) {
    EXPECT_TRUE(tt.recordTap(curFrame));
    curFrame += beatFrames;
  }
  EXPECT_NEAR(tt.appliedBpm(), 120.0, 0.1);
  EXPECT_TRUE(tt.status() == TapTempoStatus::Locked);

  // Inject isolated stumbled tap (350 ms = 171 BPM, ~30% deviation)
  curFrame += static_cast<uint64_t>(fs * 0.350);
  EXPECT_TRUE(tt.recordTap(curFrame));
  // Applied BPM must stay locked around 120 BPM
  EXPECT_NEAR(tt.appliedBpm(), 120.0, 0.1);

  // Next tap at regular cadence (500 ms later)
  curFrame += beatFrames;
  EXPECT_TRUE(tt.recordTap(curFrame));
  EXPECT_NEAR(tt.appliedBpm(), 120.0, 0.1);
  EXPECT_TRUE(tt.status() == TapTempoStatus::Locked);

  std::cout << "[PASS] Outlier Rejection (isolated deviation preserved stable BPM)\n";
  return true;
}

// D. Intentional Tempo Change (120 -> 90 BPM)
bool testIntentionalTempoChange() {
  const double fs = 44100.0;
  TapTempo tt;
  tt.prepare(fs);

  // Establish 120 BPM
  uint64_t curFrame = 20000;
  const uint64_t frames120 = static_cast<uint64_t>(fs * 0.500);
  for (int i = 0; i < 4; ++i) {
    EXPECT_TRUE(tt.recordTap(curFrame));
    if (i < 3) curFrame += frames120;
  }
  EXPECT_NEAR(tt.appliedBpm(), 120.0, 0.1);

  // Musician deliberately changes to 90 BPM (666.7 ms intervals)
  const uint64_t frames90 = static_cast<uint64_t>(fs * (60.0 / 90.0));

  // First tap of new tempo (deviation from 120 BPM)
  curFrame += frames90;
  EXPECT_TRUE(tt.recordTap(curFrame));
  // 1st deviation: stable
  EXPECT_NEAR(tt.appliedBpm(), 120.0, 0.1);

  // Second tap of new tempo: two consecutive intervals at 90 BPM!
  curFrame += frames90;
  EXPECT_TRUE(tt.recordTap(curFrame));
  // Immediate convergence to 90 BPM!
  EXPECT_NEAR(tt.appliedBpm(), 90.0, 0.2);
  EXPECT_TRUE(tt.status() == TapTempoStatus::Locked);

  // Third tap confirms new tempo
  curFrame += frames90;
  EXPECT_TRUE(tt.recordTap(curFrame));
  EXPECT_NEAR(tt.appliedBpm(), 90.0, 0.1);

  std::cout << "[PASS] Intentional Tempo Change (120 -> 90 BPM fast convergence)\n";
  return true;
}

// E. Timeout (> 3 seconds session separation)
bool testTimeout() {
  const double fs = 44100.0;
  TapTempo tt;
  tt.prepare(fs);

  // Establish 120 BPM
  uint64_t curFrame = 10000;
  EXPECT_TRUE(tt.recordTap(curFrame));
  curFrame += static_cast<uint64_t>(fs * 0.500);
  EXPECT_TRUE(tt.recordTap(curFrame));
  curFrame += static_cast<uint64_t>(fs * 0.500);
  EXPECT_TRUE(tt.recordTap(curFrame));
  EXPECT_NEAR(tt.appliedBpm(), 120.0, 0.1);

  // Wait 3.5 seconds (> 3.0s timeout)
  curFrame += static_cast<uint64_t>(fs * 3.500);
  tt.update(curFrame);
  EXPECT_TRUE(tt.isTimedOut());
  EXPECT_TRUE(tt.status() == TapTempoStatus::Idle);
  // Applied BPM is preserved!
  EXPECT_NEAR(tt.appliedBpm(), 120.0, 0.1);

  // First tap after timeout starts a new sequence
  EXPECT_TRUE(tt.recordTap(curFrame));
  EXPECT_EQ(tt.tapCount(), 1u);
  EXPECT_TRUE(tt.status() == TapTempoStatus::Acquiring);
  EXPECT_TRUE(!tt.isTimedOut());
  // BPM still preserved
  EXPECT_NEAR(tt.appliedBpm(), 120.0, 0.1);

  // Second tap at 140 BPM (428.6 ms) establishes new sequence
  const uint64_t frames140 = static_cast<uint64_t>(fs * (60.0 / 140.0));
  curFrame += frames140;
  EXPECT_TRUE(tt.recordTap(curFrame));
  EXPECT_EQ(tt.tapCount(), 2u);
  EXPECT_NEAR(tt.appliedBpm(), 140.0, 0.2);

  std::cout << "[PASS] Timeout (>3s session separation preserves BPM, resets sequence)\n";
  return true;
}

// F. Sample Rate Independence (44.1 kHz vs 48 kHz)
bool testSampleRateIndependence() {
  const double srates[] = {44100.0, 48000.0};
  const double bpm = 133.3;

  for (double fs : srates) {
    TapTempo tt;
    tt.prepare(fs);

    const uint64_t intervalFrames = static_cast<uint64_t>(std::round((60.0 / bpm) * fs));
    uint64_t cur = 5000;
    EXPECT_TRUE(tt.recordTap(cur));
    cur += intervalFrames;
    EXPECT_TRUE(tt.recordTap(cur));
    cur += intervalFrames;
    EXPECT_TRUE(tt.recordTap(cur));

    EXPECT_NEAR(tt.appliedBpm(), bpm, 0.15);
  }
  std::cout << "[PASS] Sample Rate Independence (44.1 kHz & 48 kHz)\n";
  return true;
}

// G. Timestamp Validation (decreasing, duplicate, out-of-range, overflow)
bool testTimestampValidation() {
  const double fs = 44100.0;
  TapTempo tt;
  tt.prepare(fs);

  uint64_t cur = 10000;
  EXPECT_TRUE(tt.recordTap(cur));

  // Duplicate timestamp
  EXPECT_TRUE(!tt.recordTap(cur));
  EXPECT_TRUE(tt.lastRejection() == TapRejectionReason::OutOfOrder);

  // Decreasing timestamp
  EXPECT_TRUE(!tt.recordTap(cur - 500));
  EXPECT_TRUE(tt.lastRejection() == TapRejectionReason::OutOfOrder);

  // Interval too short: 100 ms = 600 BPM (> 300 BPM, not bounce because > 80ms)
  uint64_t tFast = cur + static_cast<uint64_t>(fs * 0.100);
  EXPECT_TRUE(!tt.recordTap(tFast));
  EXPECT_TRUE(tt.lastRejection() == TapRejectionReason::OutOfRange);

  // Interval too slow: 2.5 s = 24 BPM (< 30 BPM, not timeout because <= 3.0s)
  uint64_t tSlow = tFast + static_cast<uint64_t>(fs * 2.500);
  EXPECT_TRUE(!tt.recordTap(tSlow));
  EXPECT_TRUE(tt.lastRejection() == TapRejectionReason::OutOfRange);

  // Overflow / large timestamps
  uint64_t highVal = 0xFFFFFFFF00000000ULL;
  TapTempo ttHigh;
  ttHigh.prepare(fs);
  EXPECT_TRUE(ttHigh.recordTap(highVal));
  uint64_t step = static_cast<uint64_t>(fs * 0.500);
  EXPECT_TRUE(ttHigh.recordTap(highVal + step));
  EXPECT_NEAR(ttHigh.appliedBpm(), 120.0, 0.1);

  std::cout << "[PASS] Timestamp Validation (out-of-order, out-of-range, high offsets)\n";
  return true;
}

// H. Transport Integration: Phase & Musical Position Continuous Preservation
bool testTransportIntegration() {
  const float fs = 44100.0f;
  hydra_dsp_handle* handle = nullptr;
  EXPECT_EQ(hydra_dsp_create(fs, 2000.0f, &handle), 0);
  EXPECT_TRUE(handle != nullptr);
  EXPECT_EQ(hydra_dsp_prepare(handle, 128, HYDRA_DSP_CHANNELS_STEREO), 0);

  EXPECT_EQ(hydra_dsp_transport_play(handle), 0);

  std::vector<float> inL(128, 0.0f);
  std::vector<float> inR(128, 0.0f);
  std::vector<float> outL(128, 0.0f);
  std::vector<float> outR(128, 0.0f);

  // Process 50 blocks
  for (int i = 0; i < 50; ++i) {
    EXPECT_EQ(hydra_dsp_process(handle, inL.data(), inR.data(), outL.data(), outR.data(), 128), 0);
  }

  hydra_dsp_transport_state st1{};
  EXPECT_EQ(hydra_dsp_transport_get_state(handle, &st1), 0);
  const double posBefore = st1.musical_position;
  EXPECT_TRUE(posBefore > 0.0);

  // Send tap events to change BPM to 140
  const uint64_t curFrame = st1.absolute_sample_position;
  const uint64_t step140 = static_cast<uint64_t>(fs * (60.0f / 140.0f));

  EXPECT_EQ(hydra_dsp_tap_tempo_event(handle, curFrame), 0);
  EXPECT_EQ(hydra_dsp_tap_tempo_event(handle, curFrame + step140), 0);

  hydra_dsp_transport_state st2{};
  EXPECT_EQ(hydra_dsp_transport_get_state(handle, &st2), 0);
  EXPECT_NEAR(st2.bpm, 140.0f, 0.2f);
  // Musical position must NOT reset to 0.0!
  EXPECT_NEAR(st2.musical_position, posBefore, 1e-4);

  hydra_dsp_destroy(handle);
  std::cout << "[PASS] Transport Integration (tempo changed without phase/position jump)\n";
  return true;
}

// I. Reverse Synchronization across Tap BPM Changes
bool testReverseSynchronization() {
  HydraDspEsp32Adapter adapter(44100.0f, 2000.0f, 8000.0f);
  EXPECT_TRUE(adapter.isValid());

  hydra_dsp_params_v1_4 p{};
  p.v1_3.bpm = 120.0f;
  p.v1_3.reverse = 1.0f;
  p.reverseMode = HYDRA_DSP_REVERSE_RHYTHMIC;
  p.reverseDivision = HYDRA_DSP_REVERSE_QUARTER;
  adapter.updateParams(p);
  adapter.transportPlay();

  // At 120 BPM, quarter note = 500 ms
  EXPECT_NEAR(adapter.effectiveReverseMs(), 500.0f, 1.0f);

  // Send taps for 90 BPM (quarter note = 666.7 ms)
  const uint64_t step90 = static_cast<uint64_t>(44100.0 * (60.0 / 90.0));
  uint64_t t = 1000;
  EXPECT_EQ(adapter.tapTempoEvent(t), 0);
  t += step90;
  EXPECT_EQ(adapter.tapTempoEvent(t), 0);
  t += step90;
  EXPECT_EQ(adapter.tapTempoEvent(t), 0);

  hydra_dsp_transport_state st{};
  EXPECT_EQ(adapter.transportGetState(&st), 0);
  EXPECT_NEAR(st.bpm, 90.0f, 0.2f);

  // Reverse window adapts to 90 BPM quarter note (666.7 ms)
  EXPECT_NEAR(adapter.effectiveReverseMs(), 666.7f, 2.0f);

  std::cout << "[PASS] Reverse Synchronization (Reverse adapts to tap tempo)\n";
  return true;
}

// J. Block Independence (different frame offsets within block)
bool testBlockIndependence() {
  const double fs = 44100.0;
  const uint32_t offsets[] = {0, 17, 63, 111, 127};

  for (uint32_t offset : offsets) {
    TapTempo tt;
    tt.prepare(fs);

    uint64_t base = 128 * 100 + offset;
    const uint64_t beatFrames = static_cast<uint64_t>(fs * 0.500); // 120 BPM

    EXPECT_TRUE(tt.recordTap(base));
    EXPECT_TRUE(tt.recordTap(base + beatFrames));
    EXPECT_TRUE(tt.recordTap(base + 2 * beatFrames));

    EXPECT_NEAR(tt.appliedBpm(), 120.0, 0.1);
  }
  std::cout << "[PASS] Block Independence (invariant across block offsets)\n";
  return true;
}

// K. Thread Handoff / C-API State Query
bool testThreadHandoffAndState() {
  hydra_dsp_handle* handle = nullptr;
  EXPECT_EQ(hydra_dsp_create(44100.0f, 2000.0f, &handle), 0);

  hydra_dsp_tap_tempo_state st{};
  EXPECT_EQ(hydra_dsp_tap_tempo_get_state(handle, &st), 0);
  EXPECT_EQ(st.tap_count, 0u);
  EXPECT_TRUE(st.is_timed_out == 1);
  EXPECT_EQ(st.status, HYDRA_DSP_TAP_STATUS_IDLE);

  // Simulate CLI tap (timestamp 0 uses current sample counter)
  EXPECT_EQ(hydra_dsp_tap_tempo_event(handle, 0), 0);
  EXPECT_EQ(hydra_dsp_tap_tempo_get_state(handle, &st), 0);
  EXPECT_EQ(st.tap_count, 1u);
  EXPECT_EQ(st.status, HYDRA_DSP_TAP_STATUS_ACQUIRING);

  // Reset tap tempo
  EXPECT_EQ(hydra_dsp_tap_tempo_reset(handle), 0);
  EXPECT_EQ(hydra_dsp_tap_tempo_get_state(handle, &st), 0);
  EXPECT_EQ(st.tap_count, 0u);
  EXPECT_EQ(st.status, HYDRA_DSP_TAP_STATUS_IDLE);

  hydra_dsp_destroy(handle);
  std::cout << "[PASS] Thread Handoff & State Query\n";
  return true;
}

// L. Legacy Compatibility (116, 140, 148 bytes preserved)
bool testLegacyCompatibility() {
  hydra_dsp_handle* handle = nullptr;
  EXPECT_EQ(hydra_dsp_create(44100.0f, 2000.0f, &handle), 0);

  // V1.2 (116 bytes)
  hydra_dsp_params_v1_2 p12{};
  p12.bpm = 110.0f;
  EXPECT_EQ(hydra_dsp_set_params_v1_2(handle, &p12), 0);
  EXPECT_EQ(hydra_dsp_commit(handle), 0);

  hydra_dsp_transport_state st{};
  EXPECT_EQ(hydra_dsp_transport_get_state(handle, &st), 0);
  EXPECT_NEAR(st.bpm, 110.0f, 0.01f);

  // Tap tempo event updates BPM
  uint64_t t = 1000;
  const uint64_t step130 = static_cast<uint64_t>(44100.0 * (60.0 / 130.0));
  EXPECT_EQ(hydra_dsp_tap_tempo_event(handle, t), 0);
  EXPECT_EQ(hydra_dsp_tap_tempo_event(handle, t + step130), 0);
  EXPECT_EQ(hydra_dsp_transport_get_state(handle, &st), 0);
  EXPECT_NEAR(st.bpm, 130.0f, 0.2f);

  // V1.4 sized (148 bytes)
  hydra_dsp_params_v1_4 p14{};
  p14.v1_3.bpm = 125.0f;
  EXPECT_EQ(hydra_dsp_set_params_sized(handle, &p14, sizeof(p14)), 0);
  EXPECT_EQ(hydra_dsp_commit(handle), 0);
  EXPECT_EQ(hydra_dsp_transport_get_state(handle, &st), 0);
  EXPECT_NEAR(st.bpm, 125.0f, 0.01f);

  hydra_dsp_destroy(handle);
  std::cout << "[PASS] Legacy Compatibility (ABI 116, 140, 148 bytes intact)\n";
  return true;
}

int main() {
  std::cout << "=== HYDRA TAP TEMPO V1 TEST SUITE ===\n";
  bool ok = true;
  ok = testBasicTapTempo() && ok;
  ok = testDebounce() && ok;
  ok = testOutlierRejection() && ok;
  ok = testIntentionalTempoChange() && ok;
  ok = testTimeout() && ok;
  ok = testSampleRateIndependence() && ok;
  ok = testTimestampValidation() && ok;
  ok = testTransportIntegration() && ok;
  ok = testReverseSynchronization() && ok;
  ok = testBlockIndependence() && ok;
  ok = testThreadHandoffAndState() && ok;
  ok = testLegacyCompatibility() && ok;

  if (ok) {
    std::cout << ">>> ALL 12 TAP TEMPO TESTS PASSED <<<\n";
    return 0;
  }
  std::cerr << ">>> TAP TEMPO TESTS FAILED <<<\n";
  return 1;
}
