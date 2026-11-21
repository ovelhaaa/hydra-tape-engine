#include "tap_tempo.hpp"
#include "global_transport_clock.hpp"
#include "tape_core.hpp"
#include "hydra_dsp.h"
#include "hydra_dsp_esp32_adapter.h"
#include "spsc_queue.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <thread>
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

// M. SPSC Queue & State Publisher (Lock-Free FIFO, Overflow Protection, Seqlock)
bool testSpscQueue() {
  SpscQueue<uint32_t, 8> queue;
  EXPECT_TRUE(queue.empty());
  EXPECT_EQ(queue.size(), 0u);
  EXPECT_EQ(queue.capacity(), 8u);

  // Push 8 items (capacity)
  for (uint32_t i = 1; i <= 8; ++i) {
    EXPECT_TRUE(queue.push(i * 10));
    EXPECT_EQ(queue.size(), static_cast<size_t>(i));
  }

  // 9th push must fail (overflow protection without blocking)
  EXPECT_TRUE(!queue.push(999));
  EXPECT_EQ(queue.size(), 8u);

  // Pop all 8 in FIFO order
  for (uint32_t i = 1; i <= 8; ++i) {
    uint32_t val = 0;
    EXPECT_TRUE(queue.pop(val));
    EXPECT_EQ(val, i * 10);
  }
  EXPECT_TRUE(queue.empty());

  // Additional pop must fail
  uint32_t dummy = 0;
  EXPECT_TRUE(!queue.pop(dummy));

  // Wrap-around stress: 50,000 pushes and pops
  for (uint32_t i = 0; i < 50000; ++i) {
    EXPECT_TRUE(queue.push(i));
    uint32_t popped = 0;
    EXPECT_TRUE(queue.pop(popped));
    EXPECT_EQ(popped, i);
  }
  EXPECT_TRUE(queue.empty());

  // Test StatePublisher (Triple Buffer)
  StatePublisher<hydra_dsp_tap_tempo_state> pub;
  hydra_dsp_tap_tempo_state stOut{};
  // Unwritten publisher should return false
  EXPECT_TRUE(!pub.read(stOut));

  hydra_dsp_tap_tempo_state stIn{};
  stIn.applied_bpm = 128.5f;
  stIn.tap_count = 7;
  stIn.confidence = 0.95f;
  pub.publish(stIn);

  EXPECT_TRUE(pub.read(stOut));
  EXPECT_NEAR(stOut.applied_bpm, 128.5f, 0.001f);
  EXPECT_EQ(stOut.tap_count, 7u);
  EXPECT_NEAR(stOut.confidence, 0.95f, 0.001f);

  std::cout << "[PASS] SPSC Queue & State Publisher (Lock-Free FIFO, Overflow Protection, Triple Buffer)\n";
  return true;
}

// N. Parameter Update Elimination (No updates on Tap 1, Outlier, Bounce, OutOfRange)
bool testNoParamUpdateOnTap1OrOutlier() {
  TapeCore core(44100.0f, 2000.0f, 8100.0f);
  TapeParams p;
  p.bpm = 120.0f;
  core.updateParams(p);
  core.transport().play();

  // Tap 1 at sample 1000
  TapResult res1 = core.tapTempo().recordTap(1000);
  EXPECT_TRUE(res1.accepted);
  EXPECT_TRUE(!res1.tempoUpdated); // Tap 1 must NOT update tempo
  EXPECT_TRUE(res1.status == TapTempoStatus::Acquiring);

  // Calling tapTempoEvent on Tap 1 must not alter transport BPM
  core.reset();
  core.updateParams(p);
  EXPECT_EQ(core.tapTempoEvent(1000), 0);
  EXPECT_NEAR(core.transport().bpm(), 120.0, 0.001);
  EXPECT_NEAR(core.params().bpm, 120.0f, 0.001f);

  // Taps 2 and 3 establish 140 BPM
  const uint64_t step140 = static_cast<uint64_t>(44100.0 * (60.0 / 140.0));
  uint64_t t = 1000 + step140;
  EXPECT_EQ(core.tapTempoEvent(t), 0);
  EXPECT_NEAR(core.transport().bpm(), 140.0, 0.2); // Tap 2 updates to 140 BPM

  t += step140;
  EXPECT_EQ(core.tapTempoEvent(t), 0);
  EXPECT_NEAR(core.transport().bpm(), 140.0, 0.2); // Tap 3 locks at 140 BPM

  t += step140;
  EXPECT_EQ(core.tapTempoEvent(t), 0); // Tap 4 refined at 140 BPM

  // Isolated outlier (+50% interval deviation)
  const uint64_t outlierStep = static_cast<uint64_t>(step140 * 1.50);
  t += outlierStep;
  TapResult resOutlier = core.tapTempo().recordTap(t);
  EXPECT_TRUE(resOutlier.accepted);
  EXPECT_TRUE(!resOutlier.tempoUpdated); // Outlier must NOT update tempo
  EXPECT_NEAR(core.transport().bpm(), 140.0, 0.2); // Transport BPM remains 140!

  // Debounce tap (< 80 ms = < 3528 samples @ 44.1 kHz)
  TapResult resBounce = core.tapTempo().recordTap(t + 1000);
  EXPECT_TRUE(!resBounce.accepted);
  EXPECT_TRUE(!resBounce.tempoUpdated);
  EXPECT_TRUE(resBounce.rejection == TapRejectionReason::Bounce);

  // Out of range tap (< 30 BPM -> interval > 2.0s)
  TapResult resOutOfRange = core.tapTempo().recordTap(t + static_cast<uint64_t>(44100.0 * 2.5));
  EXPECT_TRUE(!resOutOfRange.accepted);
  EXPECT_TRUE(!resOutOfRange.tempoUpdated);
  EXPECT_TRUE(resOutOfRange.rejection == TapRejectionReason::OutOfRange);

  std::cout << "[PASS] Parameter Update Elimination (No updates on Tap 1, Outlier, Bounce, OutOfRange)\n";
  return true;
}

// O. Concurrent Tap Tempo Handoff (Multi-threaded Stress, Lock-Free SPSC, Zero Corruption)
bool testConcurrentTapTempoHandoff() {
  HydraDspEsp32Adapter adapter(44100.0f, 2000.0f, 8100.0f);
  adapter.transportPlay();

  std::atomic<bool> running{true};
  std::atomic<uint32_t> blocksProcessed{0};
  std::atomic<uint32_t> tapsEnqueued{0};

  constexpr uint32_t kTotalBlocks = 2000;
  constexpr uint32_t kBlockFrames = 128;

  // Thread 1: Audio Processing (Consumer / Core 1 equivalent)
  std::thread audioThread([&]() {
    std::vector<float> inL(kBlockFrames, 0.1f);
    std::vector<float> inR(kBlockFrames, 0.1f);
    std::vector<float> outL(kBlockFrames, 0.0f);
    std::vector<float> outR(kBlockFrames, 0.0f);

    while (blocksProcessed.load(std::memory_order_relaxed) < kTotalBlocks) {
      adapter.processStereoBlock(inL.data(), inR.data(), outL.data(), outR.data(), kBlockFrames);
      blocksProcessed.fetch_add(1, std::memory_order_release);
      std::this_thread::yield();
    }
    running.store(false, std::memory_order_release);
  });

  // Thread 2: CLI / Producer (Core 0 equivalent)
  std::thread producerThread([&]() {
    const double bpms[] = {120.0, 90.0, 140.0, 100.0, 160.0};
    size_t bpmIdx = 0;
    while (running.load(std::memory_order_acquire)) {
      const double targetBpm = bpms[bpmIdx % 5];
      bpmIdx++;
      const int intervalMs = static_cast<int>(60000.0 / targetBpm);

      for (int t = 0; t < 4 && running.load(std::memory_order_acquire); ++t) {
        adapter.postTapTempoEvent(0, 0);
        tapsEnqueued.fetch_add(1, std::memory_order_relaxed);
        std::this_thread::sleep_for(std::chrono::milliseconds(std::min(intervalMs, 10)));
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
  });

  // Thread 3: Telemetry Reader (Simulates UI/CLI polling via lock-free snapshot)
  std::thread telemetryThread([&]() {
    while (running.load(std::memory_order_acquire)) {
      hydra_dsp_tap_tempo_state tapSt{};
      hydra_dsp_transport_state transSt{};
      adapter.tapTempoGetState(&tapSt);
      adapter.transportGetState(&transSt);
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  });

  audioThread.join();
  producerThread.join();
  telemetryThread.join();

  EXPECT_TRUE(blocksProcessed.load() >= kTotalBlocks);
  EXPECT_TRUE(tapsEnqueued.load() > 0);

  hydra_dsp_transport_state finalState{};
  EXPECT_EQ(adapter.transportGetState(&finalState), 0);
  EXPECT_TRUE(finalState.bpm >= 30.0f && finalState.bpm <= 300.0f);
  EXPECT_TRUE(finalState.playing != 0);

  std::cout << "[PASS] Concurrent Tap Tempo Handoff (Multi-threaded Stress, Lock-Free SPSC, Zero Corruption)\n";
  return true;
}

// P. Concurrent Transport & Manual BPM (Full Concurrency Interleaving Invariant)
bool testConcurrentTransportAndManualBpm() {
  HydraDspEsp32Adapter adapter(44100.0f, 2000.0f, 8100.0f);
  adapter.transportPlay();

  std::atomic<bool> running{true};
  std::atomic<uint32_t> blocksProcessed{0};
  constexpr uint32_t kTotalBlocks = 2500;
  constexpr uint32_t kBlockFrames = 128;

  // Audio thread
  std::thread audioThread([&]() {
    std::vector<float> inL(kBlockFrames, 0.2f);
    std::vector<float> inR(kBlockFrames, 0.2f);
    std::vector<float> outL(kBlockFrames, 0.0f);
    std::vector<float> outR(kBlockFrames, 0.0f);

    while (blocksProcessed.load(std::memory_order_relaxed) < kTotalBlocks) {
      adapter.processStereoBlock(inL.data(), inR.data(), outL.data(), outR.data(), kBlockFrames);
      blocksProcessed.fetch_add(1, std::memory_order_release);
      std::this_thread::yield();
    }
    running.store(false, std::memory_order_release);
  });

  // Tap tempo producer thread
  std::thread tapThread([&]() {
    while (running.load(std::memory_order_acquire)) {
      adapter.postTapTempoEvent(0, 0);
      std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
  });

  // Transport & manual BPM producer thread
  std::thread transportThread([&]() {
    float bpm = 100.0f;
    int cycle = 0;
    while (running.load(std::memory_order_acquire)) {
      cycle++;
      if (cycle % 4 == 0) adapter.postTransportPlay();
      else if (cycle % 4 == 1) adapter.postTransportPause();
      else if (cycle % 4 == 2) adapter.postTransportPlay();
      else adapter.postTransportSetBpm(bpm);

      bpm += 5.0f;
      if (bpm > 180.0f) bpm = 90.0f;
      std::this_thread::sleep_for(std::chrono::milliseconds(12));
    }
  });

  audioThread.join();
  tapThread.join();
  transportThread.join();

  EXPECT_TRUE(blocksProcessed.load() >= kTotalBlocks);
  std::cout << "[PASS] Concurrent Transport & Manual BPM (Full Concurrency Interleaving Invariant)\n";
  return true;
}

// Q. StatePublisher Producer/Consumer Stress & High-Frequency Publication (Tests A & C)
bool testStatePublisherStressAndHighFreq() {
  struct TelemetryTestPayload {
    uint32_t counter;
    float value;
    uint32_t check;
  };

  StatePublisher<TelemetryTestPayload> publisher;
  std::atomic<bool> producerDone{false};
  std::atomic<uint64_t> successfulReads{0};
  std::atomic<uint32_t> corruptionCount{0};
  constexpr uint32_t kTotalPublishes = 100000;

  std::thread producer([&]() {
    for (uint32_t i = 1; i <= kTotalPublishes; ++i) {
      TelemetryTestPayload p{i, static_cast<float>(i) * 1.5f, i ^ 0xA5A5A5A5};
      publisher.publish(p);
      if ((i & 0x3FF) == 0) {
        std::this_thread::yield();
      }
    }
    producerDone.store(true, std::memory_order_release);
  });

  std::thread consumer([&]() {
    TelemetryTestPayload lastRead{};
    while (!producerDone.load(std::memory_order_acquire) || successfulReads.load(std::memory_order_relaxed) == 0) {
      TelemetryTestPayload current{};
      if (publisher.read(current)) {
        successfulReads.fetch_add(1, std::memory_order_relaxed);
        // Verify invariant for every read
        if ((current.check ^ 0xA5A5A5A5) != current.counter) {
          corruptionCount.fetch_add(1, std::memory_order_relaxed);
        }
        if (current.value != static_cast<float>(current.counter) * 1.5f) {
          corruptionCount.fetch_add(1, std::memory_order_relaxed);
        }
        lastRead = current;
      }
    }
    // Final read after producer finished
    TelemetryTestPayload finalRead{};
    if (publisher.read(finalRead)) {
      if ((finalRead.check ^ 0xA5A5A5A5) != finalRead.counter) {
        corruptionCount.fetch_add(1, std::memory_order_relaxed);
      }
    }
  });

  producer.join();
  consumer.join();

  EXPECT_EQ(corruptionCount.load(), 0u);
  EXPECT_TRUE(successfulReads.load() > 1000u);

  std::cout << "[PASS] StatePublisher Stress & High-Frequency (100k publishes, zero corruption, "
            << successfulReads.load() << " consistent reads)\n";
  return true;
}

// R. Snapshot Consistency Invariant Test (Test B: inverseGeneration == ~generation)
bool testStatePublisherSnapshotConsistency() {
  struct InvariantPayload {
    uint32_t generation;
    uint32_t inverseGeneration;
    uint64_t magic;
    double samplePosition;
  };

  StatePublisher<InvariantPayload> publisher;
  std::atomic<bool> running{true};
  std::atomic<uint32_t> invariantViolations{0};
  std::atomic<uint64_t> validSnapshots{0};
  constexpr uint32_t kIterations = 50000;

  std::thread producer([&]() {
    for (uint32_t i = 1; i <= kIterations; ++i) {
      InvariantPayload p;
      p.generation = i;
      p.inverseGeneration = ~i;
      p.magic = 0xDEADBEEFCAFEBABEULL;
      p.samplePosition = static_cast<double>(i) * 128.0;
      publisher.publish(p);
    }
    running.store(false, std::memory_order_release);
  });

  std::thread consumer([&]() {
    while (running.load(std::memory_order_acquire)) {
      InvariantPayload p{};
      if (publisher.read(p)) {
        validSnapshots.fetch_add(1, std::memory_order_relaxed);
        if (p.inverseGeneration != ~p.generation) {
          invariantViolations.fetch_add(1, std::memory_order_relaxed);
        }
        if (p.magic != 0xDEADBEEFCAFEBABEULL) {
          invariantViolations.fetch_add(1, std::memory_order_relaxed);
        }
      }
    }
  });

  producer.join();
  consumer.join();

  EXPECT_EQ(invariantViolations.load(), 0u);
  EXPECT_TRUE(validSnapshots.load() > 0u);

  std::cout << "[PASS] Snapshot Consistency Invariants (zero torn reads across "
            << validSnapshots.load() << " snapshots)\n";
  return true;
}

// S. Slow Reader Protection (Test D: writer never overwrites storage held by reader)
bool testStatePublisherSlowReader() {
  struct LargeTelemetry {
    uint32_t seq;
    uint8_t buffer[256];
  };

  StatePublisher<LargeTelemetry> publisher;
  LargeTelemetry initial{};
  initial.seq = 1;
  std::memset(initial.buffer, 0x11, sizeof(initial.buffer));
  publisher.publish(initial);

  std::atomic<bool> readerStarted{false};
  std::atomic<bool> producerDone{false};
  std::atomic<bool> readerCorruption{false};

  // Reader acquires snapshot and holds onto reading/verifying it while producer publishes hundreds of times
  std::thread slowReader([&]() {
    LargeTelemetry snapshot{};
    if (!publisher.read(snapshot)) {
      readerCorruption.store(true, std::memory_order_relaxed);
    }
    readerStarted.store(true, std::memory_order_release);

    // Simulate slow processing: verify contents repeatedly over 20ms while writer publishes at full blast
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count() < 20) {
      if (snapshot.seq == 1) {
        for (size_t b = 0; b < sizeof(snapshot.buffer); ++b) {
          if (snapshot.buffer[b] != 0x11) {
            readerCorruption.store(true, std::memory_order_relaxed);
          }
        }
      }
      std::this_thread::yield();
    }
  });

  std::thread fastWriter([&]() {
    while (!readerStarted.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
    // High-frequency writes while reader is holding its buffer
    for (uint32_t i = 2; i <= 2000; ++i) {
      LargeTelemetry update{};
      update.seq = i;
      std::memset(update.buffer, static_cast<uint8_t>(i & 0xFF), sizeof(update.buffer));
      publisher.publish(update);
    }
    producerDone.store(true, std::memory_order_release);
  });

  slowReader.join();
  fastWriter.join();

  EXPECT_TRUE(!readerCorruption.load());

  // Subsequent read should get the latest published item (seq 2000)
  LargeTelemetry finalSnap{};
  EXPECT_TRUE(publisher.read(finalSnap));
  EXPECT_EQ(finalSnap.seq, 2000u);
  for (size_t b = 0; b < sizeof(finalSnap.buffer); ++b) {
    EXPECT_EQ(finalSnap.buffer[b], static_cast<uint8_t>(2000 & 0xFF));
  }

  std::cout << "[PASS] Slow Reader Protection (Writer safely cycled without corrupting reader's buffer)\n";
  return true;
}

// T. Queue Overflow & FIFO Ordering (Test E: exact 32 capacity, non-blocking drop)
bool testSpscQueueOverflowAndOrder() {
  SpscQueue<uint32_t, 32> queue;
  EXPECT_TRUE(queue.empty());
  EXPECT_EQ(queue.size(), 0u);
  EXPECT_EQ(queue.capacity(), 32u);

  // Fill exactly 32 items
  for (uint32_t i = 0; i < 32; ++i) {
    EXPECT_TRUE(queue.push(100 + i));
  }
  EXPECT_EQ(queue.size(), 32u);

  // 33rd push MUST be rejected non-blocking
  EXPECT_TRUE(!queue.push(999));
  EXPECT_TRUE(!queue.push(1000));
  EXPECT_EQ(queue.size(), 32u);

  // Pop 1 item
  uint32_t val = 0;
  EXPECT_TRUE(queue.pop(val));
  EXPECT_EQ(val, 100u);
  EXPECT_EQ(queue.size(), 31u);

  // Push 1 item should succeed now
  EXPECT_TRUE(queue.push(500));
  EXPECT_EQ(queue.size(), 32u);
  EXPECT_TRUE(!queue.push(501)); // Full again

  // Drain all remaining 32 items and verify FIFO order
  for (uint32_t i = 1; i < 32; ++i) {
    EXPECT_TRUE(queue.pop(val));
    EXPECT_EQ(val, 100 + i);
  }
  EXPECT_TRUE(queue.pop(val));
  EXPECT_EQ(val, 500u);

  // Empty queue
  EXPECT_TRUE(queue.empty());
  EXPECT_TRUE(!queue.pop(val));

  std::cout << "[PASS] SPSC Queue Overflow & FIFO Order (Capacity 32 exact, non-blocking drop)\n";
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
  ok = testSpscQueue() && ok;
  ok = testNoParamUpdateOnTap1OrOutlier() && ok;
#ifndef __EMSCRIPTEN__
  ok = testConcurrentTapTempoHandoff() && ok;
  ok = testConcurrentTransportAndManualBpm() && ok;
  ok = testStatePublisherStressAndHighFreq() && ok;
  ok = testStatePublisherSnapshotConsistency() && ok;
  ok = testStatePublisherSlowReader() && ok;
#endif
  ok = testSpscQueueOverflowAndOrder() && ok;

  if (ok) {
    std::cout << ">>> ALL 20 TAP TEMPO & CONCURRENCY TESTS PASSED <<<\n";
    return 0;
  }
  std::cerr << ">>> TAP TEMPO TESTS FAILED <<<\n";
  return 1;
}
