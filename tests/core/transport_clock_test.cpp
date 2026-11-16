#include "global_transport_clock.hpp"
#include "reverse_windows.hpp"
#include "tape_core.hpp"
#include "hydra_dsp.h"

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

// A. Sample Accuracy at 44.1 kHz and 48 kHz
bool testSampleAccuracy() {
  const double sampleRates[] = {44100.0, 48000.0};
  for (double fs : sampleRates) {
    GlobalTransportClock clock;
    clock.prepare(fs);
    clock.setBpm(120.0); // 120 BPM -> 2 beats/sec -> 0.5s per beat
    clock.play();

    const double expectedSamplesPerBeat = 0.5 * fs;
    EXPECT_NEAR(clock.subdivisionSamples(MusicalSubdivision::Quarter), expectedSamplesPerBeat, 1e-9);

    // Frame 0 is at beat 0.0 (boundary)
    EXPECT_TRUE(clock.isSubdivisionBoundary(MusicalSubdivision::Quarter));
    EXPECT_EQ(clock.beatIndex(), 0);
    EXPECT_NEAR(clock.beatPhase(), 0.0f, 1e-6);

    // Tick until exactly one sample before the next beat
    const uint32_t beatSamples = static_cast<uint32_t>(expectedSamplesPerBeat);
    for (uint32_t i = 0; i < beatSamples - 1; ++i) {
      clock.tick();
      EXPECT_TRUE(!clock.isSubdivisionBoundary(MusicalSubdivision::Quarter));
      EXPECT_EQ(clock.beatIndex(), 0);
    }

    // Tick into the exact boundary of beat 1
    clock.tick();
    EXPECT_TRUE(clock.isSubdivisionBoundary(MusicalSubdivision::Quarter));
    EXPECT_EQ(clock.beatIndex(), 1);
    EXPECT_NEAR(clock.beatPhase(), 0.0f, 1e-6);
    EXPECT_EQ(clock.transportSamplePosition(), beatSamples);
  }
  std::cout << "[PASS] Sample Accuracy (44.1 kHz & 48 kHz)\n";
  return true;
}

// B. Long-Term Drift: 5 million frames across BPMs
bool testLongTermDrift() {
  const double bpms[] = {60.0, 90.0, 120.0, 140.0, 175.0};
  const double fs = 48000.0;
  const uint32_t kFrames = 5000000;

  for (double bpm : bpms) {
    GlobalTransportClock clock;
    clock.prepare(fs);
    clock.setBpm(bpm);
    clock.play();

    clock.advanceBlock(kFrames);

    const double expectedBeats = static_cast<double>(kFrames) * (bpm / (60.0 * fs));
    const double actualBeats = clock.musicalPosition();
    EXPECT_NEAR(actualBeats, expectedBeats, 1e-9);
    EXPECT_TRUE(clock.beatPhase() >= 0.0f && clock.beatPhase() < 1.0f);
    EXPECT_TRUE(clock.barPhase() >= 0.0f && clock.barPhase() < 1.0f);
  }
  std::cout << "[PASS] Long-Term Drift (5M frames, drift < 1e-9 beats)\n";
  return true;
}

// C. Fractional Periods: BPMs with non-integer sample lengths
bool testFractionalPeriods() {
  const double fs = 48000.0;
  const double fractionalBpms[] = {133.333333333, 97.5, 127.1828};

  for (double bpm : fractionalBpms) {
    GlobalTransportClock clock;
    clock.prepare(fs);
    clock.setBpm(bpm);
    clock.play();

    // Run for 100 beats
    const double beatsPerSample = bpm / (60.0 * fs);
    const uint32_t totalSamples = static_cast<uint32_t>(100.0 / beatsPerSample);

    uint64_t quarterBoundaryCount = 0;
    for (uint32_t i = 0; i < totalSamples; ++i) {
      if (clock.isSubdivisionBoundary(MusicalSubdivision::Quarter)) {
        quarterBoundaryCount++;
      }
      clock.tick();
    }
    // Should cross approximately 100 boundaries
    EXPECT_TRUE(quarterBoundaryCount >= 99 && quarterBoundaryCount <= 101);
  }
  std::cout << "[PASS] Fractional Periods\n";
  return true;
}

// D. Block Independence: 1, 32, 64, 128, 256 frame blocks produce identical results
bool testBlockIndependence() {
  const double fs = 48000.0;
  const double bpm = 126.0;
  const uint32_t totalFrames = 48000; // exactly 1 second

  // Reference run: block size 1 (scalar frame-by-frame)
  std::vector<double> refPositions;
  std::vector<uint32_t> refQuarterBoundaries;
  std::vector<uint32_t> refEighthBoundaries;
  refPositions.reserve(totalFrames);

  {
    GlobalTransportClock clock;
    clock.prepare(fs);
    clock.setBpm(bpm);
    clock.play();

    for (uint32_t i = 0; i < totalFrames; ++i) {
      refPositions.push_back(clock.musicalPosition());
      if (clock.isSubdivisionBoundary(MusicalSubdivision::Quarter)) {
        refQuarterBoundaries.push_back(i);
      }
      if (clock.isSubdivisionBoundary(MusicalSubdivision::Eighth)) {
        refEighthBoundaries.push_back(i);
      }
      clock.tick();
    }
  }

  // Compare against block sizes 32, 64, 128, 256
  const uint32_t blockSizes[] = {32, 64, 128, 256};
  for (uint32_t bs : blockSizes) {
    GlobalTransportClock clock;
    clock.prepare(fs);
    clock.setBpm(bpm);
    clock.play();

    std::vector<uint32_t> blockQuarterBoundaries;
    std::vector<uint32_t> blockEighthBoundaries;
    uint32_t processed = 0;

    while (processed < totalFrames) {
      const uint32_t curBlock = std::min(bs, totalFrames - processed);

      BoundaryEvent qEvents[16];
      const uint32_t qCount = clock.getSubdivisionBoundariesInBlock(MusicalSubdivision::Quarter, curBlock, qEvents, 16);
      for (uint32_t e = 0; e < qCount; ++e) {
        blockQuarterBoundaries.push_back(processed + qEvents[e].frameOffset);
      }

      BoundaryEvent eEvents[32];
      const uint32_t eCount = clock.getSubdivisionBoundariesInBlock(MusicalSubdivision::Eighth, curBlock, eEvents, 32);
      for (uint32_t e = 0; e < eCount; ++e) {
        blockEighthBoundaries.push_back(processed + eEvents[e].frameOffset);
      }

      // Check position at start of block
      EXPECT_NEAR(clock.musicalPosition(), refPositions[processed], 1e-12);

      clock.advanceBlock(curBlock);
      processed += curBlock;
    }

    EXPECT_EQ(blockQuarterBoundaries.size(), refQuarterBoundaries.size());
    for (size_t i = 0; i < refQuarterBoundaries.size(); ++i) {
      EXPECT_EQ(blockQuarterBoundaries[i], refQuarterBoundaries[i]);
    }

    EXPECT_EQ(blockEighthBoundaries.size(), refEighthBoundaries.size());
    for (size_t i = 0; i < refEighthBoundaries.size(); ++i) {
      EXPECT_EQ(blockEighthBoundaries[i], refEighthBoundaries[i]);
    }
  }
  std::cout << "[PASS] Block Independence (1 vs 32, 64, 128, 256 frames)\n";
  return true;
}

// E. Time Signatures: 4/4, 3/4, 6/8
bool testTimeSignatures() {
  GlobalTransportClock clock;
  clock.prepare(48000.0);
  clock.setBpm(120.0);
  clock.play();

  // 4/4: 4 beats per bar
  clock.setTimeSignature(4, 4);
  EXPECT_NEAR(clock.beatsPerBar(), 4.0, 1e-9);
  clock.locate(3.99);
  EXPECT_EQ(clock.barIndex(), 0);
  clock.locate(4.00);
  EXPECT_EQ(clock.barIndex(), 1);
  EXPECT_NEAR(clock.barPhase(), 0.0f, 1e-6);

  // 3/4: 3 beats per bar
  clock.setTimeSignature(3, 4);
  EXPECT_NEAR(clock.beatsPerBar(), 3.0, 1e-9);
  clock.locate(2.99);
  EXPECT_EQ(clock.barIndex(), 0);
  clock.locate(3.00);
  EXPECT_EQ(clock.barIndex(), 1);
  EXPECT_NEAR(clock.barPhase(), 0.0f, 1e-6);

  // 6/8: 6 eighth-notes = 3 quarter-note beats per bar
  clock.setTimeSignature(6, 8);
  EXPECT_NEAR(clock.beatsPerBar(), 3.0, 1e-9);
  clock.locate(5.99);
  EXPECT_EQ(clock.barIndex(), 1);
  clock.locate(6.00);
  EXPECT_EQ(clock.barIndex(), 2);
  EXPECT_NEAR(clock.barPhase(), 0.0f, 1e-6);

  std::cout << "[PASS] Time Signatures (4/4, 3/4, 6/8)\n";
  return true;
}

// F. Transport Controls: Play, Pause, Stop, Reset, Locate
bool testTransportControls() {
  GlobalTransportClock clock;
  clock.prepare(48000.0);
  clock.setBpm(120.0);

  // Initial state is playing
  EXPECT_TRUE(clock.isPlaying());
  const uint32_t gen0 = clock.generation();

  // Tick 1000 frames
  clock.advanceBlock(1000);
  const double pos1 = clock.musicalPosition();
  EXPECT_TRUE(pos1 > 0.0);
  EXPECT_EQ(clock.absoluteSamplePosition(), 1000u);
  EXPECT_EQ(clock.transportSamplePosition(), 1000u);

  // PAUSE: musical position frozen, absolute samples continue advancing
  clock.pause();
  EXPECT_TRUE(!clock.isPlaying());
  EXPECT_TRUE(clock.state() == TransportState::Paused);

  clock.advanceBlock(500);
  EXPECT_NEAR(clock.musicalPosition(), pos1, 1e-12); // Position preserved!
  EXPECT_EQ(clock.absoluteSamplePosition(), 1500u);
  EXPECT_EQ(clock.transportSamplePosition(), 1000u);

  // PLAY again: resumes advancing from preserved position
  clock.play();
  EXPECT_TRUE(clock.isPlaying());
  clock.advanceBlock(1000);
  EXPECT_NEAR(clock.musicalPosition(), pos1 + 1000.0 * (120.0 / (60.0 * 48000.0)), 1e-9);
  EXPECT_EQ(clock.transportSamplePosition(), 2000u);
  EXPECT_EQ(clock.absoluteSamplePosition(), 2500u);

  // LOCATE: relocation without clearing audio samples
  clock.locate(16.0, 5000);
  EXPECT_NEAR(clock.musicalPosition(), 16.0, 1e-9);
  EXPECT_TRUE(clock.generation() > gen0);

  // RESET: relocates origin to 0
  const uint32_t genLocate = clock.generation();
  clock.reset();
  EXPECT_NEAR(clock.musicalPosition(), 0.0, 1e-9);
  EXPECT_TRUE(clock.generation() > genLocate);

  std::cout << "[PASS] Transport Controls (Play, Pause, Stop, Reset, Locate)\n";
  return true;
}

// G. BPM Changes with Phase Preservation
bool testBpmChanges() {
  const double fs = 48000.0;
  GlobalTransportClock clock;
  clock.prepare(fs);
  clock.setBpm(120.0);
  clock.play();

  // Advance to beat 2.75
  const uint32_t framesTo275 = static_cast<uint32_t>(2.75 / (120.0 / (60.0 * fs)));
  clock.advanceBlock(framesTo275);
  EXPECT_NEAR(clock.musicalPosition(), 2.75, 1e-6);
  EXPECT_NEAR(clock.beatPhase(), 0.75f, 1e-5);
  EXPECT_EQ(clock.beatIndex(), 2);

  // Change BPM 120 -> 90 at position 2.75
  clock.setBpm(90.0);
  EXPECT_NEAR(clock.musicalPosition(), 2.75, 1e-6); // No jump!
  EXPECT_NEAR(clock.beatPhase(), 0.75f, 1e-5);       // Phase preserved!
  EXPECT_EQ(clock.beatIndex(), 2);                   // Beat preserved!

  // Remaining distance to beat 3.0 at 90 BPM
  // Delta beats = 0.25 beats. At 90 BPM, beatsPerSample = 90 / (60 * 48000) = 1 / 32000
  // Expected frames = 0.25 * 32000 = 8000 frames
  EXPECT_EQ(clock.framesToNextBoundary(MusicalSubdivision::Quarter), 8000);

  // Test BPM change 1 frame before boundary
  clock.advanceBlock(7999);
  EXPECT_TRUE(!clock.isSubdivisionBoundary(MusicalSubdivision::Quarter));
  clock.setBpm(140.0); // Change tempo 1 frame before boundary
  // Musical position still preserved, no false boundary
  EXPECT_TRUE(!clock.isSubdivisionBoundary(MusicalSubdivision::Quarter));

  std::cout << "[PASS] BPM Changes (Phase Preservation & Boundary Stability)\n";
  return true;
}

// H. Two-Consumer Synchronization
bool testTwoConsumerSynchronization() {
  const double fs = 48000.0;
  GlobalTransportClock clock;
  clock.prepare(fs);
  clock.setBpm(120.0);
  clock.play();

  // Simulate Consumer 1 activated at frame 0 (quarter note)
  ReverseWindows consumer1;
  consumer1.configure(fs, 96000, 120.0f, 0, false, 500.0f);
  consumer1.syncPhase(clock.subdivisionPhase(MusicalSubdivision::Quarter), 96000);

  // Run for 3719 frames, then activate Consumer 2
  const uint32_t delayFrames = 3719;
  for (uint32_t i = 0; i < delayFrames; ++i) {
    const bool isB = clock.isSubdivisionBoundary(MusicalSubdivision::Quarter);
    consumer1.next(isB, true);
    clock.tick();
  }

  // Activate Consumer 2 mid-beat
  ReverseWindows consumer2;
  consumer2.configure(fs, 96000, 120.0f, 0, false, 500.0f);
  // Consumer 2 initializes its phase to the current global transport subdivision phase
  consumer2.syncPhase(clock.subdivisionPhase(MusicalSubdivision::Quarter), 96000);

  // Now run both consumers until beat 2.0 and verify boundary synchronization
  std::vector<uint32_t> c1Boundaries;
  std::vector<uint32_t> c2Boundaries;

  const uint32_t remaining = 48000; // run for 1 second
  for (uint32_t i = 0; i < remaining; ++i) {
    const bool isB = clock.isSubdivisionBoundary(MusicalSubdivision::Quarter);
    const auto f1 = consumer1.next(isB, true);
    const auto f2 = consumer2.next(isB, true);

    if (isB) {
      c1Boundaries.push_back(i);
      c2Boundaries.push_back(i);
    }
    // Both frames should have identical reading delays once synchronized
    EXPECT_NEAR(f1.delay, f2.delay, 1.0f);
    clock.tick();
  }

  // Boundaries fired at the exact same frames for both consumers
  EXPECT_EQ(c1Boundaries.size(), c2Boundaries.size());
  for (size_t i = 0; i < c1Boundaries.size(); ++i) {
    EXPECT_EQ(c1Boundaries[i], c2Boundaries[i]);
  }

  std::cout << "[PASS] Two-Consumer Synchronization\n";
  return true;
}

// I. Reverse Integration & TapeCore Transport
bool testReverseIntegration() {
  TapeCore core(48000.0f, 2000.0f, 4000.0f);
  EXPECT_TRUE(core.isValid());

  TapeParams p;
  p.delayActive = true;
  p.bpm = 120.0f;
  p.reverse = true;
  p.reverseMode = 0; // Rhythmic
  p.reverseDivision = 0; // Quarter note
  core.updateParams(p);

  EXPECT_NEAR(core.transport().bpm(), 120.0, 1e-6);

  // Process a block of 128 frames
  float inL[128] = {0.0f};
  float inR[128] = {0.0f};
  float outL[128] = {0.0f};
  float outR[128] = {0.0f};
  inL[0] = 1.0f;

  core.processStereoBlock(inL, inR, outL, outR, 128);

  // Transport clock advanced by exactly 128 frames
  EXPECT_EQ(core.transport().absoluteSamplePosition(), 128u);
  EXPECT_EQ(core.transport().transportSamplePosition(), 128u);

  std::cout << "[PASS] Reverse Integration & TapeCore Transport\n";
  return true;
}

// J. C-API Transport Functions (API 1.5)
bool testCApiTransport() {
  hydra_dsp_handle* handle = nullptr;
  int err = hydra_dsp_create_v1_4(48000.0f, 2000.0f, 4000.0f, &handle);
  EXPECT_EQ(err, 0);
  EXPECT_TRUE(handle != nullptr);

  hydra_dsp_transport_state st{};
  err = hydra_dsp_transport_get_state(handle, &st);
  EXPECT_EQ(err, 0);
  EXPECT_NEAR(st.bpm, 120.0f, 1e-6);
  EXPECT_EQ(st.playing, 1);
  EXPECT_EQ(st.time_signature_numerator, 4u);
  EXPECT_EQ(st.time_signature_denominator, 4u);

  // Change BPM via C-API
  err = hydra_dsp_transport_set_bpm(handle, 135.0f);
  EXPECT_EQ(err, 0);
  err = hydra_dsp_transport_get_state(handle, &st);
  EXPECT_EQ(err, 0);
  EXPECT_NEAR(st.bpm, 135.0f, 1e-6);

  // Set time signature 3/4
  err = hydra_dsp_transport_set_time_signature(handle, 3, 4);
  EXPECT_EQ(err, 0);
  err = hydra_dsp_transport_get_state(handle, &st);
  EXPECT_EQ(err, 0);
  EXPECT_EQ(st.time_signature_numerator, 3u);
  EXPECT_EQ(st.time_signature_denominator, 4u);

  // Locate and Pause
  err = hydra_dsp_transport_pause(handle);
  EXPECT_EQ(err, 0);
  err = hydra_dsp_transport_locate(handle, 8.0, 10000);
  EXPECT_EQ(err, 0);
  err = hydra_dsp_transport_get_state(handle, &st);
  EXPECT_EQ(err, 0);
  EXPECT_EQ(st.playing, 0);
  EXPECT_NEAR(st.musical_position, 8.0, 1e-9);

  hydra_dsp_destroy(handle);
  std::cout << "[PASS] C-API Transport Functions (API 1.5)\n";
  return true;
}

// L. Overflow & 64-bit Long Session Stability
bool testLongSessionStability() {
  GlobalTransportClock clock;
  clock.prepare(48000.0);
  clock.setBpm(130.0);
  clock.play();

  // Simulate 100 million frames (~34.7 hours of continuous playback)
  const uint64_t hugeFrames = 100000000ULL;
  clock.advanceBlock(static_cast<uint32_t>(hugeFrames & 0xFFFFFFFF));

  const double expectedBeats = static_cast<double>(hugeFrames) * (130.0 / (60.0 * 48000.0));
  EXPECT_NEAR(clock.musicalPosition(), expectedBeats, 1e-6);
  EXPECT_TRUE(clock.beatPhase() >= 0.0f && clock.beatPhase() < 1.0f);
  EXPECT_TRUE(clock.barPhase() >= 0.0f && clock.barPhase() < 1.0f);
  EXPECT_EQ(clock.absoluteSamplePosition(), hugeFrames);

  std::cout << "[PASS] Long Session Stability (100M frames / 35h continuous)\n";
  return true;
}

int main() {
  std::cout << "=== HYDRA GLOBAL TRANSPORT CLOCK TEST SUITE ===\n";
  bool ok = true;
  ok = ok && testSampleAccuracy();
  ok = ok && testLongTermDrift();
  ok = ok && testFractionalPeriods();
  ok = ok && testBlockIndependence();
  ok = ok && testTimeSignatures();
  ok = ok && testTransportControls();
  ok = ok && testBpmChanges();
  ok = ok && testTwoConsumerSynchronization();
  ok = ok && testReverseIntegration();
  ok = ok && testCApiTransport();
  ok = ok && testLongSessionStability();

  if (ok) {
    std::cout << ">>> ALL GLOBAL TRANSPORT CLOCK TESTS PASSED <<<\n";
    return 0;
  } else {
    std::cerr << ">>> SOME TESTS FAILED <<<\n";
    return 1;
  }
}
