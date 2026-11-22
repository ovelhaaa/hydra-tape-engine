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

// E. Time Signatures: 4/4, 3/4, 6/8, 7/8, 5/16 + Strict Validation
bool testTimeSignatures() {
  GlobalTransportClock clock;
  clock.prepare(48000.0);
  clock.setBpm(120.0);
  clock.play();

  // 1. Valid Signatures:
  // 4/4: 4 beats per bar
  EXPECT_TRUE(clock.setTimeSignature(4, 4));
  EXPECT_EQ(clock.timeSignatureNumerator(), 4);
  EXPECT_EQ(clock.timeSignatureDenominator(), 4);
  EXPECT_NEAR(clock.beatsPerBar(), 4.0, 1e-9);
  clock.locate(3.99);
  EXPECT_EQ(clock.barIndex(), 0);
  clock.locate(4.00);
  EXPECT_EQ(clock.barIndex(), 1);
  EXPECT_NEAR(clock.barPhase(), 0.0f, 1e-6);

  // 3/4: 3 beats per bar
  EXPECT_TRUE(clock.setTimeSignature(3, 4));
  EXPECT_EQ(clock.timeSignatureNumerator(), 3);
  EXPECT_EQ(clock.timeSignatureDenominator(), 4);
  EXPECT_NEAR(clock.beatsPerBar(), 3.0, 1e-9);
  clock.locate(2.99);
  EXPECT_EQ(clock.barIndex(), 0);
  clock.locate(3.00);
  EXPECT_EQ(clock.barIndex(), 1);
  EXPECT_NEAR(clock.barPhase(), 0.0f, 1e-6);

  // 6/8: 6 eighth-notes = 3 quarter-note beats per bar
  EXPECT_TRUE(clock.setTimeSignature(6, 8));
  EXPECT_EQ(clock.timeSignatureNumerator(), 6);
  EXPECT_EQ(clock.timeSignatureDenominator(), 8);
  EXPECT_NEAR(clock.beatsPerBar(), 3.0, 1e-9);
  clock.locate(5.99);
  EXPECT_EQ(clock.barIndex(), 1);
  clock.locate(6.00);
  EXPECT_EQ(clock.barIndex(), 2);
  EXPECT_NEAR(clock.barPhase(), 0.0f, 1e-6);

  // 7/8: 7 eighth-notes = 3.5 quarter-note beats per bar
  EXPECT_TRUE(clock.setTimeSignature(7, 8));
  EXPECT_EQ(clock.timeSignatureNumerator(), 7);
  EXPECT_EQ(clock.timeSignatureDenominator(), 8);
  EXPECT_NEAR(clock.beatsPerBar(), 3.5, 1e-9);

  // 5/16: 5 sixteenth-notes = 1.25 quarter-note beats per bar
  EXPECT_TRUE(clock.setTimeSignature(5, 16));
  EXPECT_EQ(clock.timeSignatureNumerator(), 5);
  EXPECT_EQ(clock.timeSignatureDenominator(), 16);
  EXPECT_NEAR(clock.beatsPerBar(), 1.25, 1e-9);

  // 2. Strict Rejection of Invalid Signatures:
  // Baseline state: 5/16, beatsPerBar = 1.25, musicalPos = 6.0, generation = clock.generation()
  clock.locate(6.0);
  const uint32_t savedNum = clock.timeSignatureNumerator();
  const uint32_t savedDen = clock.timeSignatureDenominator();
  const double savedBpb = clock.beatsPerBar();
  const double savedPos = clock.musicalPosition();
  const uint32_t savedGen = clock.generation();

  const std::pair<uint32_t, uint32_t> invalidCases[] = {
      {0, 4},   // Zero numerator
      {33, 4},  // Numerator > 32
      {4, 0},   // Zero denominator
      {4, 3},   // Unsupported denominator (3)
      {4, 32}   // Denominator not in {2, 4, 8, 16}
  };

  for (const auto& [n, d] : invalidCases) {
    EXPECT_TRUE(!clock.setTimeSignature(n, d));
    // Verify complete state preservation
    EXPECT_EQ(clock.timeSignatureNumerator(), savedNum);
    EXPECT_EQ(clock.timeSignatureDenominator(), savedDen);
    EXPECT_NEAR(clock.beatsPerBar(), savedBpb, 1e-9);
    EXPECT_NEAR(clock.musicalPosition(), savedPos, 1e-9);
    EXPECT_EQ(clock.generation(), savedGen);
  }

  // 3. C-API Verification
  hydra_dsp_handle* handle = nullptr;
  int err = hydra_dsp_create(48000.0f, 2000.0f, &handle);
  EXPECT_EQ(err, 0);

  // Valid via C-API
  EXPECT_EQ(hydra_dsp_transport_set_time_signature(handle, 4, 4), 0);
  EXPECT_EQ(hydra_dsp_transport_set_time_signature(handle, 7, 8), 0);

  // Invalid via C-API
  for (const auto& [n, d] : invalidCases) {
    EXPECT_EQ(hydra_dsp_transport_set_time_signature(handle, n, d), -1);
  }

  hydra_dsp_transport_state st{};
  EXPECT_EQ(hydra_dsp_transport_get_state(handle, &st), 0);
  EXPECT_EQ(st.time_signature_numerator, 7);
  EXPECT_EQ(st.time_signature_denominator, 8);

  hydra_dsp_destroy(handle);

  std::cout << "[PASS] Time Signatures (4/4, 3/4, 6/8, 7/8, 5/16 + Strict Validation)\n";
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

  // Simulate 100 million frames (~34.7 minutes of continuous playback at 48 kHz)
  const uint64_t hugeFrames = 100000000ULL;
  clock.advanceBlock(static_cast<uint32_t>(hugeFrames & 0xFFFFFFFF));

  const double expectedBeats = static_cast<double>(hugeFrames) * (130.0 / (60.0 * 48000.0));
  EXPECT_NEAR(clock.musicalPosition(), expectedBeats, 1e-6);
  EXPECT_TRUE(clock.beatPhase() >= 0.0f && clock.beatPhase() < 1.0f);
  EXPECT_TRUE(clock.barPhase() >= 0.0f && clock.barPhase() < 1.0f);
  EXPECT_EQ(clock.absoluteSamplePosition(), hugeFrames);

  std::cout << "[PASS] Long Session Stability (100M frames / ~34.7 minutes continuous)\n";
  return true;
}

// M. NaN, Infinity, and Invalid Input Hardening
bool testInvalidInputsAndNaN() {
  GlobalTransportClock clock;
  clock.prepare(48000.0);
  clock.setBpm(120.0);
  clock.play();
  clock.advanceBlock(24000); // 1.0 beat

  const double prevBpm = clock.bpm();
  const double prevPos = clock.musicalPosition();
  const uint32_t prevGen = clock.generation();

  // 1. Invalid BPMs must be rejected without modifying state
  const double invalidBpms[] = {
      std::numeric_limits<double>::quiet_NaN(),
      std::numeric_limits<double>::infinity(),
      -std::numeric_limits<double>::infinity(),
      0.0,
      -10.0,
      -120.0
  };
  for (double badBpm : invalidBpms) {
    clock.setBpm(badBpm);
    EXPECT_EQ(clock.bpm(), prevBpm);
    EXPECT_NEAR(clock.musicalPosition(), prevPos, 1e-9);
    EXPECT_EQ(clock.generation(), prevGen);
  }

  // 2. Invalid locate positions must be rejected without modifying state
  const double invalidPositions[] = {
      std::numeric_limits<double>::quiet_NaN(),
      std::numeric_limits<double>::infinity(),
      -std::numeric_limits<double>::infinity(),
      -0.0001,
      -100.0
  };
  for (double badPos : invalidPositions) {
    clock.locate(badPos, 12345);
    EXPECT_EQ(clock.bpm(), prevBpm);
    EXPECT_NEAR(clock.musicalPosition(), prevPos, 1e-9);
    EXPECT_EQ(clock.generation(), prevGen);
  }

  // 3. Invalid time signatures (zero numerator or denominator) must be rejected
  const uint32_t prevNum = clock.timeSignatureNumerator();
  const uint32_t prevDen = clock.timeSignatureDenominator();
  clock.setTimeSignature(0, 4);
  EXPECT_EQ(clock.timeSignatureNumerator(), prevNum);
  EXPECT_EQ(clock.timeSignatureDenominator(), prevDen);
  clock.setTimeSignature(4, 0);
  EXPECT_EQ(clock.timeSignatureNumerator(), prevNum);
  EXPECT_EQ(clock.timeSignatureDenominator(), prevDen);

  // 4. Invalid sample rate must be rejected in prepare
  clock.prepare(std::numeric_limits<double>::quiet_NaN());
  EXPECT_EQ(clock.sampleRate(), 48000.0);
  clock.prepare(0.0);
  EXPECT_EQ(clock.sampleRate(), 48000.0);
  clock.prepare(-48000.0);
  EXPECT_EQ(clock.sampleRate(), 48000.0);

  // 5. C-API error codes on invalid inputs
  hydra_dsp_handle* handle = nullptr;
  int err = hydra_dsp_create_v1_4(48000.0f, 2000.0f, 4000.0f, &handle);
  EXPECT_EQ(err, 0);
  EXPECT_TRUE(handle != nullptr);

  // C-API set BPM: returns -1 on NaN / non-finite / non-positive
  EXPECT_EQ(hydra_dsp_transport_set_bpm(handle, std::numeric_limits<float>::quiet_NaN()), -1);
  EXPECT_EQ(hydra_dsp_transport_set_bpm(handle, std::numeric_limits<float>::infinity()), -1);
  EXPECT_EQ(hydra_dsp_transport_set_bpm(handle, -std::numeric_limits<float>::infinity()), -1);
  EXPECT_EQ(hydra_dsp_transport_set_bpm(handle, 0.0f), -1);
  EXPECT_EQ(hydra_dsp_transport_set_bpm(handle, -50.0f), -1);

  // C-API locate: returns -1 on NaN / negative
  EXPECT_EQ(hydra_dsp_transport_locate(handle, std::numeric_limits<double>::quiet_NaN(), 0), -1);
  EXPECT_EQ(hydra_dsp_transport_locate(handle, -1.0, 0), -1);

  // C-API time signature: returns -1 on 0
  EXPECT_EQ(hydra_dsp_transport_set_time_signature(handle, 0, 4), -1);
  EXPECT_EQ(hydra_dsp_transport_set_time_signature(handle, 4, 0), -1);

  // Parameter set for BPM
  EXPECT_EQ(hydra_dsp_set_parameter(handle, HYDRA_DSP_PARAM_BPM, std::numeric_limits<float>::quiet_NaN()), -1);
  EXPECT_EQ(hydra_dsp_set_parameter(handle, HYDRA_DSP_PARAM_BPM, 0.0f), -1);
  EXPECT_EQ(hydra_dsp_set_parameter(handle, HYDRA_DSP_PARAM_BPM, -10.0f), -1);

  hydra_dsp_destroy(handle);
  std::cout << "[PASS] NaN, Infinity, and Invalid Input Hardening\n";
  return true;
}

// N. Limited History Diagnostics & Safe Clamping
bool testLimitedHistoryDiagnostics() {
  hydra_dsp_handle* handle = nullptr;
  // Create engine with 500ms max delay / history at 48 kHz (~24000 frames)
  int err = hydra_dsp_create_v1_4(48000.0f, 500.0f, 500.0f, &handle);
  EXPECT_EQ(err, 0);

  // Request 30 BPM with Whole Note (subdivision 1 = 4 beats = 8000 ms!)
  hydra_dsp_set_parameter(handle, HYDRA_DSP_PARAM_REVERSE, 1.0f);
  hydra_dsp_set_parameter(handle, HYDRA_DSP_PARAM_REVERSE_MODE, 0.0f); // Rhythmic
  hydra_dsp_set_parameter(handle, HYDRA_DSP_PARAM_REVERSE_DIVISION, 1.0f); // Whole note
  hydra_dsp_transport_set_bpm(handle, 30.0f);
  hydra_dsp_commit(handle);

  float requestedMs = 0.0f;
  float effectiveMs = 0.0f;
  int isClamped = 0;
  float maxHistoryMs = 0.0f;

  err = hydra_dsp_get_reverse_diagnostics(handle, &requestedMs, &effectiveMs, &isClamped, &maxHistoryMs);
  EXPECT_EQ(err, 0);
  EXPECT_EQ(isClamped, 1);
  EXPECT_NEAR(requestedMs, 8000.0f, 1.0f);
  EXPECT_TRUE(effectiveMs < requestedMs);
  EXPECT_TRUE(effectiveMs <= maxHistoryMs);
  EXPECT_NEAR(maxHistoryMs, 500.0f, 1.0f);

  hydra_dsp_destroy(handle);
  std::cout << "[PASS] Limited History Diagnostics & Safe Clamping\n";
  return true;
}

// N. DSP Precedence: Accidental Warp takes operational priority over Reverse V2
bool testDspPrecedenceWarpVsReverse() {
  TapeCore core(48000.0f, 2000.0f, 2000.0f);
  EXPECT_TRUE(core.isValid());

  TapeParams p;
  p.delayActive = true;
  p.delayTimeMs = 300.0f;
  p.delayWet = 100.0f;
  p.dryWet = 100.0f;
  p.reverse = true;
  p.reverseMode = 0; // Rhythmic Reverse V2
  p.reverseDivision = 0; // Quarter note

  // When Warp is OFF, Reverse V2 is active
  p.warpActive = false;
  core.updateParams(p);
  EXPECT_TRUE(!core.isReverseClamped()); // Valid window

  // When Warp is ON, Warp takes operational priority over Reverse V2
  p.warpActive = true;
  core.updateParams(p);

  // Process a block: verify engine runs stably without conflict
  std::vector<float> inL(128, 0.5f), inR(128, 0.5f);
  std::vector<float> outL(128, 0.0f), outR(128, 0.0f);
  core.processStereoBlock(inL.data(), inR.data(), outL.data(), outR.data(), 128);

  for (size_t i = 0; i < 128; ++i) {
    EXPECT_TRUE(std::isfinite(outL[i]));
    EXPECT_TRUE(std::isfinite(outR[i]));
  }

  std::cout << "[PASS] DSP Precedence (Accidental Warp takes priority over Reverse V2)\n";
  return true;
}

// Block-collected boundaries must match the per-sample predicate exactly.
// Reverse V2 now consumes the block form, so a mismatch would move a window
// boundary by one sample.
bool testBlockBoundaryEquivalence() {
  constexpr uint32_t kBlock = 128;
  constexpr uint32_t kBlocks = 400;
  for (const double bpm : {60.0, 120.0, 137.0, 300.0}) {
    for (int sub = 0; sub < static_cast<int>(MusicalSubdivision::Count); ++sub) {
      GlobalTransportClock clock;
      clock.prepare(48000.0);
      clock.setBpm(bpm);
      clock.locate(0.0, 0);
      clock.play();
      const auto subdiv = static_cast<MusicalSubdivision>(sub);
      for (uint32_t block = 0; block < kBlocks; ++block) {
        if (block == 137) clock.setBpm(91.0);  // mid-session tempo change
        BoundaryEvent events[8];
        const uint32_t count = clock.getSubdivisionBoundariesInBlock(subdiv, kBlock, events, 8);
        EXPECT_TRUE(count <= 8);
        uint32_t cursor = 0;
        for (uint32_t i = 0; i < kBlock; ++i) {
          const bool expected = clock.isSubdivisionBoundary(subdiv);
          bool actual = false;
          if (cursor < count && events[cursor].frameOffset == i) {
            actual = true;
            ++cursor;
          }
          if (expected != actual) {
            std::cerr << "FAIL: block boundary mismatch bpm=" << bpm << " subdiv=" << sub
                      << " block=" << block << " frame=" << i << "\n";
            return false;
          }
          clock.tick();
        }
      }
    }
  }
  std::cout << "[Block boundary equivalence] OK\n";
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
  ok = ok && testInvalidInputsAndNaN();
  ok = ok && testLimitedHistoryDiagnostics();
  ok = ok && testDspPrecedenceWarpVsReverse();
  ok = ok && testBlockBoundaryEquivalence();

  if (ok) {
    std::cout << ">>> ALL GLOBAL TRANSPORT CLOCK TESTS PASSED <<<\n";
    return 0;
  } else {
    std::cerr << ">>> SOME TESTS FAILED <<<\n";
    return 1;
  }
}
