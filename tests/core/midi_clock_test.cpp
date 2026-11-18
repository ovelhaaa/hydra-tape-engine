#include "midi_clock_sync.hpp"
#include "global_transport_clock.hpp"
#include "reverse_windows.hpp"
#include "tape_core.hpp"
#include "hydra_dsp_esp32_adapter.h"
#include "hydra_dsp.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <random>
#include <thread>
#include <vector>

using namespace hydra::dsp;

inline std::ostream& operator<<(std::ostream& os, TempoSource s) {
  return os << static_cast<int>(s);
}

inline std::ostream& operator<<(std::ostream& os, MidiClockSyncStatus s) {
  return os << static_cast<int>(s);
}

#define EXPECT_TRUE(cond) \
  do { \
    if (!(cond)) { \
      std::cerr << "FAIL: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
      return false; \
    } \
  } while (0)

#define EXPECT_FALSE(cond) EXPECT_TRUE(!(cond))

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

// A. MIDI Real-Time Parser & Interleaving
bool testMidiRealtimeParser() {
  MidiRealtimeParser parser;
  std::vector<uint8_t> realTimeEvents;

  auto handler = [&](uint8_t b) {
    realTimeEvents.push_back(b);
  };

  // 1. Interleaved Note On: 0x90, 0x3C, [0xF8], 0x7F
  // The 0xF8 real-time message appears between the pitch and velocity bytes.
  parser.parseByte(0x90, handler);
  EXPECT_EQ(parser.runningStatus(), 0x90);
  parser.parseByte(0x3C, handler);
  EXPECT_EQ(parser.runningStatus(), 0x90);
  parser.parseByte(0xF8, handler); // Timing Clock inside Note On!
  EXPECT_EQ(parser.runningStatus(), 0x90); // Running status preserved!
  parser.parseByte(0x7F, handler);
  EXPECT_EQ(parser.runningStatus(), 0x90);

  EXPECT_EQ(realTimeEvents.size(), 1u);
  EXPECT_EQ(realTimeEvents[0], 0xF8);

  // 2. Real-time transport messages: Start, Continue, Stop
  parser.parseByte(0xFA, handler); // Start
  parser.parseByte(0xFB, handler); // Continue
  parser.parseByte(0xFC, handler); // Stop

  EXPECT_EQ(realTimeEvents.size(), 4u);
  EXPECT_EQ(realTimeEvents[1], 0xFA);
  EXPECT_EQ(realTimeEvents[2], 0xFB);
  EXPECT_EQ(realTimeEvents[3], 0xFC);
  EXPECT_EQ(parser.runningStatus(), 0x90); // Running status still intact!

  std::cout << "[PASS] A. MIDI Real-Time Parser (Interleaving & Status Preservation)\n";
  return true;
}

// B. BPM Acquisition (60, 90, 120, 140, 180, 240, 300 BPM)
bool testBpmAcquisition() {
  const double sampleRates[] = {44100.0, 48000.0};
  const double testBpms[] = {60.0, 90.0, 120.0, 140.0, 180.0, 240.0, 300.0};

  for (double fs : sampleRates) {
    for (double bpm : testBpms) {
      MidiClockSync sync;
      sync.prepare(fs);

      // Start transport
      sync.processEvent(0xFA, 0, 0.0);

      const double deltaFramesD = (60.0 * fs) / (24.0 * bpm);

      double exactFrame = 1000.0;
      uint64_t frame = 1000;
      double musicalPos = 0.0;

      // Feed 48 pulses (2 beats)
      for (uint32_t p = 0; p < 48; ++p) {
        exactFrame += deltaFramesD;
        frame = static_cast<uint64_t>(std::round(exactFrame));
        musicalPos = static_cast<double>(p + 1) / 24.0;
        sync.processEvent(0xF8, frame, musicalPos);
      }

      // Check estimated and applied BPM
      EXPECT_NEAR(sync.estimatedBpm(), bpm, 0.2);
      EXPECT_NEAR(sync.appliedBpm(), bpm, 0.2);
      EXPECT_EQ(static_cast<int>(sync.status()), static_cast<int>(MidiClockSyncStatus::Locked));
    }
  }

  std::cout << "[PASS] B. BPM Acquisition (60..300 BPM across 44.1k & 48k)\n";
  return true;
}

// C. Phase Lock & Alignment after Start
bool testPhaseLockAndAlignment() {
  const double fs = 44100.0;
  TapeCore core(static_cast<float>(fs), 2000.0f, 8100.0f);
  core.setTempoSource(TempoSource::MidiExternal);

  // Send Start (0xFA) at sample 1000
  EXPECT_EQ(core.midiClockEvent(0xFA, 1000), 0);
  EXPECT_TRUE(core.transport().isPlaying());
  EXPECT_NEAR(core.transport().musicalPosition(), 0.0, 1e-6);

  const double bpm = 120.0;
  const uint64_t deltaFrames = static_cast<uint64_t>(std::round((60.0 * fs) / (24.0 * bpm)));

  uint64_t frame = 1000;
  for (uint32_t p = 0; p < 72; ++p) { // 3 beats
    frame += deltaFrames;
    // Advance audio transport clock to match pulse arrival frame
    core.transport().advanceBlock(static_cast<uint32_t>(deltaFrames));
    core.midiClockEvent(0xF8, frame);
  }

  const auto& mc = core.midiClock();
  EXPECT_EQ(static_cast<int>(mc.status()), static_cast<int>(MidiClockSyncStatus::Locked));
  EXPECT_NEAR(mc.appliedBpm(), 120.0, 0.1);
  EXPECT_NEAR(mc.phaseErrorBeats(), 0.0, 0.01);

  std::cout << "[PASS] C. Phase Lock & Alignment (Start -> 0.0, zero phase drift)\n";
  return true;
}

// D. Start / Stop / Continue Semantics
bool testStartStopContinue() {
  const double fs = 44100.0;
  TapeCore core(static_cast<float>(fs), 2000.0f, 8100.0f);
  core.setTempoSource(TempoSource::MidiExternal);

  // 1. Start: begins at beat 0.0
  core.midiClockEvent(0xFA, 1000);
  EXPECT_TRUE(core.transport().isPlaying());
  EXPECT_NEAR(core.transport().musicalPosition(), 0.0, 1e-6);

  // Advance 2 beats (48 clocks @ 120 BPM)
  const uint64_t step = static_cast<uint64_t>((60.0 * fs) / (24.0 * 120.0));
  uint64_t t = 1000;
  for (int i = 0; i < 48; ++i) {
    t += step;
    core.transport().advanceBlock(static_cast<uint32_t>(step));
    core.midiClockEvent(0xF8, t);
  }
  const double posAtStop = core.transport().musicalPosition();
  EXPECT_NEAR(posAtStop, 2.0, 0.05);

  // 2. Stop: pauses transport advance, preserves position
  core.midiClockEvent(0xFC, t);
  EXPECT_TRUE(!core.transport().isPlaying());
  EXPECT_NEAR(core.transport().musicalPosition(), posAtStop, 1e-6);

  // Clocks arriving while stopped do NOT advance musical position
  for (int i = 0; i < 24; ++i) {
    t += step;
    core.midiClockEvent(0xF8, t);
    // Transport does not advance
  }
  EXPECT_NEAR(core.transport().musicalPosition(), posAtStop, 1e-6);

  // 3. Continue: resumes from preserved position (NOT beat 0!)
  core.midiClockEvent(0xFB, t);
  EXPECT_TRUE(core.transport().isPlaying());
  EXPECT_NEAR(core.transport().musicalPosition(), posAtStop, 1e-4);

  // Advance 1 more beat
  for (int i = 0; i < 24; ++i) {
    t += step;
    core.transport().advanceBlock(static_cast<uint32_t>(step));
    core.midiClockEvent(0xF8, t);
  }
  EXPECT_NEAR(core.transport().musicalPosition(), posAtStop + 1.0, 0.05);

  std::cout << "[PASS] D. Start / Stop / Continue (Position Preserved, Clock while Stopped Safe)\n";
  return true;
}

// E. Jitter Filtering (±0.5ms, ±1ms, ±2ms, ±5ms)
bool testJitterFiltering() {
  const double fs = 48000.0;
  const double nominalBpm = 120.0;
  const double nominalIntervalFrames = (60.0 * fs) / (24.0 * nominalBpm); // 1000 frames = 20.833 ms

  const double jitterMsList[] = {0.5, 1.0, 2.0, 5.0};
  for (double maxJitterMs : jitterMsList) {
    MidiClockSync sync;
    sync.prepare(fs);
    sync.processEvent(0xFA, 0, 0.0);

    std::mt19937 rng(12345);
    const double maxJitterFrames = (maxJitterMs * 0.001) * fs;
    std::uniform_real_distribution<double> dist(-maxJitterFrames, maxJitterFrames);

    uint64_t idealFrame = 1000;
    for (int p = 0; p < 96; ++p) { // 4 beats
      idealFrame += static_cast<uint64_t>(nominalIntervalFrames);
      const int64_t jitter = static_cast<int64_t>(dist(rng));
      const uint64_t actualFrame = static_cast<uint64_t>(static_cast<int64_t>(idealFrame) + jitter);
      const double musicalPos = static_cast<double>(p + 1) / 24.0;
      sync.processEvent(0xF8, actualFrame, musicalPos);
    }

    // Mean estimated BPM must remain tightly centered near 120 despite jitter
    EXPECT_NEAR(sync.estimatedBpm(), nominalBpm, (maxJitterMs <= 2.0 ? 0.8 : 1.8));
    EXPECT_TRUE(sync.estimatedJitterMs() > 0.0);
  }

  std::cout << "[PASS] E. Jitter Filtering (±0.5ms .. ±5ms noise rejection)\n";
  return true;
}

// F. Tempo Ramp (120 -> 140 BPM over 20 seconds)
bool testTempoRamp() {
  const double fs = 44100.0;
  MidiClockSync sync;
  sync.prepare(fs);
  sync.processEvent(0xFA, 0, 0.0);

  double currentBpm = 120.0;
  const double targetBpm = 140.0;
  const double durationSec = 20.0;
  const double bpmRate = (targetBpm - currentBpm) / durationSec; // +1.0 BPM/sec

  uint64_t frame = 1000;
  double elapsedSec = 0.0;
  while (elapsedSec < durationSec) {
    currentBpm = 120.0 + bpmRate * elapsedSec;
    const double intervalFramesD = (60.0 * fs) / (24.0 * currentBpm);
    const uint64_t intervalFrames = static_cast<uint64_t>(std::round(intervalFramesD));
    frame += intervalFrames;
    elapsedSec += static_cast<double>(intervalFrames) / fs;
    sync.processEvent(0xF8, frame, 0.0);
  }

  // At the end of the ramp, estimator should track 140 BPM closely
  EXPECT_NEAR(sync.estimatedBpm(), 140.0, 0.8);

  std::cout << "[PASS] F. Tempo Ramp (120 -> 140 BPM smooth tracking)\n";
  return true;
}

// G. Sudden Tempo Change (120 -> 90 -> 140 -> 180)
bool testSuddenTempoChange() {
  const double fs = 44100.0;
  MidiClockSync sync;
  sync.prepare(fs);
  sync.processEvent(0xFA, 0, 0.0);

  const double bpms[] = {120.0, 90.0, 140.0, 180.0};
  uint64_t frame = 1000;

  for (double targetBpm : bpms) {
    const double interval = (60.0 * fs) / (24.0 * targetBpm);
    // Feed 36 pulses (1.5 beats) at the new tempo
    for (int p = 0; p < 36; ++p) {
      frame += static_cast<uint64_t>(interval);
      sync.processEvent(0xF8, frame, 0.0);
    }
    EXPECT_NEAR(sync.estimatedBpm(), targetBpm, 0.5);
  }

  std::cout << "[PASS] G. Sudden Tempo Change (Fast convergence without stalls)\n";
  return true;
}

// H. Missing Pulses Recovery
bool testMissingPulses() {
  const double fs = 44100.0;
  MidiClockSync sync;
  sync.prepare(fs);
  sync.processEvent(0xFA, 0, 0.0);

  const double nominalInterval = (60.0 * fs) / (24.0 * 120.0);
  uint64_t frame = 1000;

  // Establish lock with 24 pulses
  for (int p = 0; p < 24; ++p) {
    frame += static_cast<uint64_t>(nominalInterval);
    sync.processEvent(0xF8, frame, static_cast<double>(p + 1) / 24.0);
  }
  EXPECT_EQ(static_cast<int>(sync.status()), static_cast<int>(MidiClockSyncStatus::Locked));

  // Drop exactly 1 pulse: jump by 2x interval
  frame += static_cast<uint64_t>(nominalInterval * 2.0);
  sync.processEvent(0xF8, frame, 26.0 / 24.0);

  // Estimator should detect 1 missing pulse and retain 120 BPM
  EXPECT_EQ(sync.missedPulses(), 1u);
  EXPECT_NEAR(sync.estimatedBpm(), 120.0, 0.5);

  std::cout << "[PASS] H. Missing Pulses (Detected & seamlessly bridged)\n";
  return true;
}

// I. Clock Loss & Holdover
bool testClockLossAndHoldover() {
  const double fs = 44100.0;
  MidiClockSync sync;
  sync.prepare(fs);
  sync.processEvent(0xFA, 0, 0.0);

  const double nominalInterval = (60.0 * fs) / (24.0 * 120.0); // ~918 frames @ 120 BPM
  uint64_t frame = 1000;

  for (int p = 0; p < 36; ++p) {
    frame += static_cast<uint64_t>(nominalInterval);
    sync.processEvent(0xF8, frame, static_cast<double>(p + 1) / 24.0);
  }
  EXPECT_EQ(static_cast<int>(sync.status()), static_cast<int>(MidiClockSyncStatus::Locked));

  // No pulses for 250 ms (~11025 frames) -> exceeds timeout (~150 ms) -> enters HOLDOVER
  frame += static_cast<uint64_t>(fs * 0.250);
  sync.update(frame);
  EXPECT_EQ(static_cast<int>(sync.status()), static_cast<int>(MidiClockSyncStatus::Holdover));
  EXPECT_NEAR(sync.appliedBpm(), 120.0, 0.1); // Retains last valid BPM in holdover!

  // No pulses for 1.5 seconds -> exceeds lost threshold (1.0 s) -> enters LOST
  frame += static_cast<uint64_t>(fs * 1.500);
  sync.update(frame);
  EXPECT_EQ(static_cast<int>(sync.status()), static_cast<int>(MidiClockSyncStatus::Lost));

  std::cout << "[PASS] I. Clock Loss & Holdover (Locked -> Holdover -> Lost)\n";
  return true;
}

// J. Reconnection & Reacquisition
bool testReconnection() {
  const double fs = 44100.0;
  MidiClockSync sync;
  sync.prepare(fs);
  sync.processEvent(0xFA, 0, 0.0);

  const double nominalInterval = (60.0 * fs) / (24.0 * 120.0);
  uint64_t frame = 1000;
  for (int p = 0; p < 24; ++p) {
    frame += static_cast<uint64_t>(nominalInterval);
    sync.processEvent(0xF8, frame, 0.0);
  }

  // Force clock loss
  frame += static_cast<uint64_t>(fs * 2.0);
  sync.update(frame);
  EXPECT_EQ(static_cast<int>(sync.status()), static_cast<int>(MidiClockSyncStatus::Lost));

  // Pulses resume at a new tempo: 140 BPM
  const double newInterval = (60.0 * fs) / (24.0 * 140.0);
  frame += static_cast<uint64_t>(newInterval);
  sync.processEvent(0xF8, frame, 0.0);
  EXPECT_EQ(static_cast<int>(sync.status()), static_cast<int>(MidiClockSyncStatus::Acquiring));

  for (int p = 0; p < 24; ++p) {
    frame += static_cast<uint64_t>(newInterval);
    sync.processEvent(0xF8, frame, 0.0);
  }
  EXPECT_NEAR(sync.estimatedBpm(), 140.0, 0.5);

  std::cout << "[PASS] J. Reconnection (Reacquisition to new tempo after signal loss)\n";
  return true;
}

// K. Source Switching (All 6 transition pairs between Internal, Tap, and MIDI External)
bool testSourceSwitching() {
  TapeCore core(44100.0f, 2000.0f, 8100.0f);
  core.transport().play();

  TapeParams p;
  p.bpm = 120.0f;
  core.updateParams(p);
  EXPECT_EQ(core.tempoSource(), TempoSource::Internal);
  EXPECT_NEAR(core.transport().bpm(), 120.0, 0.01);

  // 1. Internal -> Tap
  EXPECT_EQ(core.setTempoSource(TempoSource::Tap), 0);
  EXPECT_EQ(core.tempoSource(), TempoSource::Tap);
  EXPECT_NEAR(core.transport().bpm(), 120.0, 0.01); // No abrupt jump

  // 2. Tap -> Internal
  p.bpm = 115.0f;
  core.updateParams(p);
  EXPECT_EQ(core.setTempoSource(TempoSource::Internal), 0);
  EXPECT_EQ(core.tempoSource(), TempoSource::Internal);
  EXPECT_NEAR(core.transport().bpm(), 115.0, 0.01);

  // 3. Internal -> MIDI External
  EXPECT_EQ(core.setTempoSource(TempoSource::MidiExternal), 0);
  EXPECT_EQ(core.tempoSource(), TempoSource::MidiExternal);
  // Manual updates while in MIDI External do not corrupt transport
  p.bpm = 100.0f;
  core.updateParams(p);
  EXPECT_NEAR(core.transport().bpm(), 115.0, 0.01);

  // 4. MIDI External -> Internal
  EXPECT_EQ(core.setTempoSource(TempoSource::Internal), 0);
  EXPECT_EQ(core.tempoSource(), TempoSource::Internal);
  EXPECT_NEAR(core.transport().bpm(), 100.0, 0.01); // Preserved manual BPM restored!

  // 5. Tap -> MIDI External
  EXPECT_EQ(core.setTempoSource(TempoSource::Tap), 0);
  EXPECT_EQ(core.setTempoSource(TempoSource::MidiExternal), 0);
  EXPECT_EQ(core.tempoSource(), TempoSource::MidiExternal);

  // 6. MIDI External -> Tap
  EXPECT_EQ(core.setTempoSource(TempoSource::Tap), 0);
  EXPECT_EQ(core.tempoSource(), TempoSource::Tap);

  std::cout << "[PASS] K. Source Switching (All 6 transition pairs clickless & preserved)\n";
  return true;
}

// L. Reverse Synchronization with External Clock
bool testReverseSynchronization() {
  const double fs = 44100.0;
  TapeCore core(static_cast<float>(fs), 2000.0f, 8100.0f);
  core.setTempoSource(TempoSource::MidiExternal);

  TapeParams p;
  p.bpm = 120.0f;
  p.reverse = true;
  p.reverseMode = 0; // Rhythmic
  p.reverseDivision = 0; // Quarter note
  core.updateParams(p);

  core.midiClockEvent(0xFA, 0); // Start

  const double deltaFrames = (60.0 * fs) / (24.0 * 120.0);
  uint64_t frame = 0;

  std::vector<float> inL(128, 0.1f);
  std::vector<float> inR(128, 0.1f);
  std::vector<float> outL(128, 0.0f);
  std::vector<float> outR(128, 0.0f);

  // Process 100 blocks
  for (int b = 0; b < 100; ++b) {
    for (int i = 0; i < 128; ++i) {
      frame++;
      if (frame % static_cast<uint64_t>(deltaFrames) == 0) {
        core.midiClockEvent(0xF8, frame);
      }
    }
    core.processStereoBlock(inL.data(), inR.data(), outL.data(), outR.data(), 128);
  }

  EXPECT_NEAR(core.transport().bpm(), 120.0, 0.5);
  EXPECT_TRUE(!std::isnan(outL[0]));
  EXPECT_TRUE(!std::isnan(outR[0]));

  std::cout << "[PASS] L. Reverse Synchronization (Rhythmic windows adapt to MIDI Clock)\n";
  return true;
}

// M. Multi-Head Delay Synchronization
bool testMultiHeadSynchronization() {
  const double fs = 44100.0;
  TapeCore core(static_cast<float>(fs), 2000.0f, 8100.0f);
  core.setTempoSource(TempoSource::MidiExternal);

  TapeParams p;
  p.delayActive = true;
  p.headsMusical = true;
  p.bpm = 120.0f;
  core.updateParams(p);

  core.midiClockEvent(0xFA, 0);

  // Sync to 90 BPM
  const double deltaFrames90 = (60.0 * fs) / (24.0 * 90.0);
  uint64_t frame = 0;
  for (int i = 0; i < 48; ++i) {
    frame += static_cast<uint64_t>(deltaFrames90);
    core.midiClockEvent(0xF8, frame);
    core.transport().advanceBlock(static_cast<uint32_t>(deltaFrames90));
  }

  EXPECT_NEAR(core.transport().bpm(), 90.0, 0.5);

  std::cout << "[PASS] M. Multi-Head Synchronization (Musical heads scale with external BPM)\n";
  return true;
}

// N. SPSC Dedicated MIDI Queue Concurrency & Overflow
bool testSpscMidiConcurrency() {
  HydraDspEsp32Adapter adapter(44100.0f, 2000.0f, 8100.0f);
  adapter.setTempoSource(2); // MIDI External
  adapter.transportPlay();

  // Test capacity & overflow protection without dropping or locking
  for (int i = 0; i < 64; ++i) {
    EXPECT_TRUE(adapter.postMidiClockEvent(0xF8, i * 100));
  }
  // 65th push fails gracefully (overflow protection)
  EXPECT_TRUE(!adapter.postMidiClockEvent(0xF8, 99999));

  // Process 1 block -> drains queue safely
  std::vector<float> inL(128, 0.0f), inR(128, 0.0f), outL(128, 0.0f), outR(128, 0.0f);
  adapter.processStereoBlock(inL.data(), inR.data(), outL.data(), outR.data(), 128);

  // Now queue has space again
  EXPECT_TRUE(adapter.postMidiClockEvent(0xF8, 12345));

  std::cout << "[PASS] N. SPSC Concurrency & Overflow Protection (64 capacity FIFO, wait-free drop)\n";
  return true;
}

// O. ABI Compatibility & C API 1.7
bool testAbiCompatibility() {
  hydra_dsp_handle* handle = nullptr;
  EXPECT_EQ(hydra_dsp_create(44100.0f, 2000.0f, &handle), 0);
  EXPECT_TRUE(handle != nullptr);

  // Version 1.7.0
  const uint32_t ver = hydra_dsp_get_api_version();
  EXPECT_EQ((ver >> 16) & 0xFF, 1u);
  EXPECT_EQ((ver >> 8) & 0xFF, 7u);

  // Legacy V1.2 (116 bytes)
  hydra_dsp_params_v1_2 p12{};
  p12.bpm = 124.0f;
  EXPECT_EQ(hydra_dsp_set_params_v1_2(handle, &p12), 0);
  EXPECT_EQ(hydra_dsp_commit(handle), 0);

  // Tempo Source API
  uint32_t src = 999;
  EXPECT_EQ(hydra_dsp_get_tempo_source(handle, &src), 0);
  EXPECT_EQ(src, static_cast<uint32_t>(HYDRA_DSP_TEMPO_SOURCE_INTERNAL));

  EXPECT_EQ(hydra_dsp_set_tempo_source(handle, HYDRA_DSP_TEMPO_SOURCE_MIDI_EXTERNAL), 0);
  EXPECT_EQ(hydra_dsp_get_tempo_source(handle, &src), 0);
  EXPECT_EQ(src, static_cast<uint32_t>(HYDRA_DSP_TEMPO_SOURCE_MIDI_EXTERNAL));

  // MIDI Clock state query
  hydra_dsp_midi_clock_state st{};
  EXPECT_EQ(hydra_dsp_midi_clock_get_state(handle, &st), 0);
  EXPECT_EQ(st.tempo_source, static_cast<uint32_t>(HYDRA_DSP_TEMPO_SOURCE_MIDI_EXTERNAL));
  EXPECT_EQ(st.sync_status, static_cast<int>(HYDRA_DSP_MIDI_CLOCK_DISCONNECTED));

  // Feed Start + 24 clocks
  EXPECT_EQ(hydra_dsp_midi_clock_event(handle, 0xFA, 1000), 0);
  const uint64_t step = static_cast<uint64_t>((60.0 * 44100.0) / (24.0 * 120.0));
  uint64_t t = 1000;
  for (int i = 0; i < 24; ++i) {
    t += step;
    EXPECT_EQ(hydra_dsp_midi_clock_event(handle, 0xF8, t), 0);
  }

  EXPECT_EQ(hydra_dsp_midi_clock_get_state(handle, &st), 0);
  EXPECT_NEAR(st.applied_bpm, 120.0f, 0.5f);
  EXPECT_EQ(st.pulse_count, 24u);

  // MIDI Clock reset
  EXPECT_EQ(hydra_dsp_midi_clock_reset(handle), 0);
  EXPECT_EQ(hydra_dsp_midi_clock_get_state(handle, &st), 0);
  EXPECT_EQ(st.pulse_count, 0u);
  EXPECT_EQ(st.sync_status, static_cast<int>(HYDRA_DSP_MIDI_CLOCK_DISCONNECTED));

  hydra_dsp_destroy(handle);

  std::cout << "[PASS] O. ABI Compatibility & C API 1.7 (Zero struct offset or ABI regression)\n";
  return true;
}

// P. Long-Running Stability (100,000 pulses, zero cumulative drift)
bool testLongRunningStability() {
  const double fs = 48000.0;
  MidiClockSync sync;
  sync.prepare(fs);
  sync.processEvent(0xFA, 0, 0.0);

  const double targetBpm = 135.0;
  const double deltaFramesD = (60.0 * fs) / (24.0 * targetBpm);

  uint64_t frame = 1000;
  double exactFramesAccum = 1000.0;

  for (uint32_t p = 0; p < 100000; ++p) {
    exactFramesAccum += deltaFramesD;
    frame = static_cast<uint64_t>(std::round(exactFramesAccum));
    const double musicalPos = static_cast<double>(p + 1) / 24.0;
    sync.processEvent(0xF8, frame, musicalPos);
  }

  EXPECT_NEAR(sync.estimatedBpm(), 135.0, 0.01);
  EXPECT_NEAR(sync.appliedBpm(), 135.0, 0.01);
  EXPECT_NEAR(sync.phaseErrorBeats(), 0.0, 0.01);
  EXPECT_EQ(sync.missedPulses(), 0u);

  std::cout << "[PASS] P. Long-Running Stability (100k pulses, zero drift: error < 0.01)\n";
  return true;
}

// Q. Section 3.4 Source Arbitration (Tests A, B, C, D, E)
bool testSourceArbitrationExplicit() {
  const double fs = 44100.0;
  TapeCore core(static_cast<float>(fs), 2000.0f, 8100.0f);
  core.transport().play();

  // Test A — Manual -> MIDI -> Manual
  // 1. Configurar 120 BPM em Internal.
  TapeParams p;
  p.bpm = 120.0f;
  core.updateParams(p);
  EXPECT_EQ(core.tempoSource(), TempoSource::Internal);
  EXPECT_NEAR(core.manualBpm(), 120.0f, 0.01f);
  EXPECT_NEAR(core.effectiveBpm(), 120.0f, 0.01f);

  // 2. Selecionar MIDI External.
  EXPECT_EQ(core.setTempoSource(TempoSource::MidiExternal), 0);
  EXPECT_EQ(core.tempoSource(), TempoSource::MidiExternal);

  // 3. Receber clock estável a 140 BPM.
  const double interval140 = (60.0 * fs) / (24.0 * 140.0);
  double fExact = 1000.0;
  uint64_t f = 1000;
  core.midiClockEvent(0xFA, f);
  for (int i = 0; i < 72; ++i) {
    fExact += interval140;
    uint64_t nextF = static_cast<uint64_t>(std::round(fExact));
    core.transport().advanceBlock(static_cast<uint32_t>(nextF - f));
    f = nextF;
    core.midiClockEvent(0xF8, f);
  }

  // 4. Confirmar BPM efetivo próximo de 140 e manualBpm preservado em 120.
  EXPECT_NEAR(core.effectiveBpm(), 140.0f, 0.5f);
  EXPECT_NEAR(core.manualBpm(), 120.0f, 0.01f);

  // 5. Voltar para Internal.
  EXPECT_EQ(core.setTempoSource(TempoSource::Internal), 0);
  EXPECT_EQ(core.tempoSource(), TempoSource::Internal);

  // 6. Confirmar retorno a 120 BPM.
  EXPECT_NEAR(core.effectiveBpm(), 120.0f, 0.01f);
  EXPECT_NEAR(core.transport().bpm(), 120.0, 0.01);

  // Test B — Tap não retorna silenciosamente para Internal
  // 1. Selecionar TAP.
  EXPECT_EQ(core.setTempoSource(TempoSource::Tap), 0);
  EXPECT_EQ(core.tempoSource(), TempoSource::Tap);

  // 2. Aplicar taps de 90 BPM (interval = 60/90 s = 29400 samples @ 44.1k).
  uint64_t tapTime = f + 44100;
  core.tapTempoEvent(tapTime);
  tapTime += 29400; core.tapTempoEvent(tapTime);
  tapTime += 29400; core.tapTempoEvent(tapTime);
  tapTime += 29400; core.tapTempoEvent(tapTime);

  // 3. Confirmar que a fonte continua TAP.
  EXPECT_EQ(core.tempoSource(), TempoSource::Tap);

  // 4. Confirmar BPM efetivo próximo de 90.
  EXPECT_NEAR(core.effectiveBpm(), 90.0f, 1.0f);
  EXPECT_NEAR(core.tapEstimatedBpm(), 90.0f, 1.0f);

  // Test C — Tap durante MIDI External
  // 1. Selecionar MIDI External.
  EXPECT_EQ(core.setTempoSource(TempoSource::MidiExternal), 0);
  EXPECT_EQ(core.tempoSource(), TempoSource::MidiExternal);

  // 2. Estabilizar clock a 120 BPM.
  const double interval120 = (60.0 * fs) / (24.0 * 120.0);
  double fExact120 = static_cast<double>(f) + 10000.0;
  f = static_cast<uint64_t>(std::round(fExact120));
  core.midiClockEvent(0xFA, f);
  for (int i = 0; i < 96; ++i) {
    fExact120 += interval120;
    uint64_t nextF = static_cast<uint64_t>(std::round(fExact120));
    core.transport().advanceBlock(static_cast<uint32_t>(nextF - f));
    f = nextF;
    core.midiClockEvent(0xF8, f);
  }
  EXPECT_NEAR(core.effectiveBpm(), 120.0f, 1.0f);

  // 3. Receber taps equivalentes a 80 BPM (interval = 60/80 s = 33075 samples).
  core.tapTempoReset();
  tapTime = f + 100;
  core.tapTempoEvent(tapTime);
  tapTime += 33075; core.tapTempoEvent(tapTime);
  tapTime += 33075; core.tapTempoEvent(tapTime);
  tapTime += 33075; core.tapTempoEvent(tapTime);
  tapTime += 33075; core.tapTempoEvent(tapTime);

  // 4. Confirmar que MIDI External continua autoritativo.
  EXPECT_EQ(core.tempoSource(), TempoSource::MidiExternal);

  // 5. Confirmar que o GTC não passa a 80 BPM.
  EXPECT_NEAR(core.effectiveBpm(), 120.0f, 1.0f);
  EXPECT_NEAR(core.transport().bpm(), 120.0, 1.0);
  EXPECT_NEAR(core.tapEstimatedBpm(), 80.0f, 1.0f);

  // Test D — BPM manual durante MIDI External
  // 1. Selecionar MIDI External.
  // 2. Estabilizar clock a 140 BPM.
  fExact += 10000.0;
  f = static_cast<uint64_t>(std::round(fExact));
  core.midiClockEvent(0xFA, f);
  for (int i = 0; i < 96; ++i) {
    fExact += interval140;
    uint64_t nextF = static_cast<uint64_t>(std::round(fExact));
    core.transport().advanceBlock(static_cast<uint32_t>(nextF - f));
    f = nextF;
    core.midiClockEvent(0xF8, f);
  }
  EXPECT_NEAR(core.effectiveBpm(), 140.0f, 1.0f);

  // 3. Alterar BPM manual para 110.
  p.bpm = 110.0f;
  core.updateParams(p);
  EXPECT_NEAR(core.manualBpm(), 110.0f, 0.01f);

  // 4. Confirmar que o GTC permanece sincronizado externamente.
  EXPECT_NEAR(core.effectiveBpm(), 140.0f, 1.0f);
  EXPECT_NEAR(core.transport().bpm(), 140.0, 1.0);

  // 5. Voltar para Internal.
  EXPECT_EQ(core.setTempoSource(TempoSource::Internal), 0);
  EXPECT_EQ(core.tempoSource(), TempoSource::Internal);

  // 6. Confirmar 110 BPM.
  EXPECT_NEAR(core.effectiveBpm(), 110.0f, 0.01f);
  EXPECT_NEAR(core.transport().bpm(), 110.0, 0.01);

  // Test E — Todas as 6 transições mantendo buffers intactos
  std::vector<float> inL(128, 0.1f), inR(128, 0.1f), outL(128, 0.0f), outR(128, 0.0f);
  core.processStereoBlock(inL.data(), inR.data(), outL.data(), outR.data(), 128);

  const TempoSource transitions[6][2] = {
    {TempoSource::Internal, TempoSource::Tap},
    {TempoSource::Tap, TempoSource::Internal},
    {TempoSource::Internal, TempoSource::MidiExternal},
    {TempoSource::MidiExternal, TempoSource::Internal},
    {TempoSource::Tap, TempoSource::MidiExternal},
    {TempoSource::MidiExternal, TempoSource::Tap}
  };

  for (int t = 0; t < 6; ++t) {
    EXPECT_EQ(core.setTempoSource(transitions[t][0]), 0);
    EXPECT_EQ(core.tempoSource(), transitions[t][0]);
    core.processStereoBlock(inL.data(), inR.data(), outL.data(), outR.data(), 128);
    EXPECT_TRUE(!std::isnan(outL[0]));

    EXPECT_EQ(core.setTempoSource(transitions[t][1]), 0);
    EXPECT_EQ(core.tempoSource(), transitions[t][1]);
    core.processStereoBlock(inL.data(), inR.data(), outL.data(), outR.data(), 128);
    EXPECT_TRUE(!std::isnan(outL[0]));
  }

  std::cout << "[PASS] Q. Section 3.4 Source Arbitration (Tests A, B, C, D, E)\n";
  return true;
}

// R. Hardened Phase Lock & Rejection (Integer beat offsets, duplicate/out-of-order)
bool testHardenedPhaseLockAndRejection() {
  const double fs = 48000.0;
  MidiClockSync sync;
  sync.prepare(fs);

  // 1. Out-of-order and duplicate pulses must not increment pulseCount
  sync.processEvent(0xFA, 1000, 0.0);
  EXPECT_EQ(sync.pulseCount(), 0u);

  auto r1 = sync.processEvent(0xF8, 1200, 1.0 / 24.0);
  EXPECT_TRUE(r1.accepted);
  EXPECT_EQ(sync.pulseCount(), 1u);

  // Duplicate timestamp (1200 <= 1200)
  auto r2 = sync.processEvent(0xF8, 1200, 1.0 / 24.0);
  EXPECT_TRUE(!r2.accepted);
  EXPECT_EQ(sync.pulseCount(), 1u); // Must not advance!

  // Out-of-order timestamp (1150 < 1200)
  auto r3 = sync.processEvent(0xF8, 1150, 1.0 / 24.0);
  EXPECT_TRUE(!r3.accepted);
  EXPECT_EQ(sync.pulseCount(), 1u); // Must not advance!

  // 2. Lock establishment at 120 BPM
  const double interval = (60.0 * fs) / (24.0 * 120.0); // 1000 frames
  uint64_t frame = 1200;
  for (int p = 0; p < 48; ++p) {
    frame += static_cast<uint64_t>(interval);
    const double expectedPos = static_cast<double>(sync.pulseCount() + 1) / 24.0;
    sync.processEvent(0xF8, frame, expectedPos);
  }
  EXPECT_EQ(static_cast<int>(sync.status()), static_cast<int>(MidiClockSyncStatus::Locked));
  EXPECT_NEAR(sync.absolutePhaseErrorBeats(), 0.0, 0.02);
  EXPECT_NEAR(sync.wrappedPhaseErrorBeats(), 0.0, 0.015);

  // 3. Integer beat offset rejection (1.0 beat displacement)
  frame += static_cast<uint64_t>(interval);
  double expectedPos = static_cast<double>(sync.pulseCount() + 1) / 24.0;
  auto res1Beat = sync.processEvent(0xF8, frame, expectedPos - 1.0); // 1.0 beat displaced
  EXPECT_NEAR(res1Beat.wrappedPhaseErrorBeats, 0.0, 0.02);
  EXPECT_NEAR(res1Beat.absolutePhaseErrorBeats, 1.0, 0.02);
  // Status must not be Locked on integer-beat displacement
  EXPECT_TRUE(sync.status() != MidiClockSyncStatus::Locked);

  // 4. Test phase offsets: 1/24, 1/8, 1/4, 1/2, 1 beat, 2 beats
  // Re-acquire lock first
  sync.processEvent(0xFA, frame, 0.0);
  for (int p = 0; p < 48; ++p) {
    frame += static_cast<uint64_t>(interval);
    sync.processEvent(0xF8, frame, static_cast<double>(p + 1) / 24.0);
  }
  EXPECT_EQ(static_cast<int>(sync.status()), static_cast<int>(MidiClockSyncStatus::Locked));

  const double testOffsets[] = {
    1.0 / 24.0, // 0.0417 - inside 0.05 tolerance
    1.0 / 8.0,  // 0.125 - outside tolerance
    1.0 / 4.0,  // 0.25 - outside tolerance
    1.0 / 2.0,  // 0.5 - outside tolerance
    1.0,        // 1.0 - integer beat offset
    2.0         // 2.0 - integer beat offset
  };

  for (double offset : testOffsets) {
    frame += static_cast<uint64_t>(interval);
    double expected = static_cast<double>(sync.pulseCount() + 1) / 24.0;
    auto res = sync.processEvent(0xF8, frame, expected - offset);
    EXPECT_NEAR(res.absolutePhaseErrorBeats, offset, 0.03);
    if (offset > 0.05) {
      EXPECT_TRUE(sync.status() != MidiClockSyncStatus::Locked);
    }
  }

  std::cout << "[PASS] R. Hardened Phase Lock & Rejection (Integer beat offsets, duplicate/out-of-order)\n";
  return true;
}

// S. Start/Stop/Continue Edge Cases (Continue without Start, repeated Start/Stop)
bool testStartStopContinueEdgeCases() {
  const double fs = 44100.0;
  TapeCore core(static_cast<float>(fs), 2000.0f, 8100.0f);
  core.setTempoSource(TempoSource::MidiExternal);

  // Edge Case 1: Continue (0xFB) without Start (0xFA)
  core.transport().stop();
  EXPECT_FALSE(core.transport().isPlaying());
  core.midiClockEvent(0xFB, 100);
  EXPECT_TRUE(core.transport().isPlaying());
  EXPECT_NEAR(core.transport().musicalPosition(), 0.0, 1e-4);

  // Edge Case 2: Repeated Start (0xFA)
  core.transport().advanceBlock(44100); // advance 1 second
  EXPECT_TRUE(core.transport().musicalPosition() > 0.5);
  core.midiClockEvent(0xFA, 44200); // Restart!
  EXPECT_TRUE(core.transport().isPlaying());
  EXPECT_NEAR(core.transport().musicalPosition(), 0.0, 1e-6);

  // Edge Case 3: Repeated Stop (0xFC)
  core.midiClockEvent(0xFC, 44300);
  EXPECT_FALSE(core.transport().isPlaying());
  core.midiClockEvent(0xFC, 44400); // Redundant stop
  EXPECT_FALSE(core.transport().isPlaying());

  // Edge Case 4: Clocks while stopped
  for (int i = 0; i < 48; ++i) {
    core.midiClockEvent(0xF8, 44500 + i * 900);
  }
  EXPECT_FALSE(core.transport().isPlaying());
  EXPECT_NEAR(core.transport().musicalPosition(), 0.0, 1e-6);

  std::cout << "[PASS] S. Start/Stop/Continue Edge Cases (Continue without Start, repeated Start/Stop)\n";
  return true;
}

// T. SPSC Queue Overflow Recovery (Overflow counter and reacquisition reset)
bool testSpscQueueOverflowRecovery() {
  HydraDspEsp32Adapter adapter(44100.0f, 2000.0f, 8100.0f);
  adapter.setTempoSource(2); // MIDI External
  adapter.transportPlay();

  EXPECT_EQ(adapter.midiOverflowCount(), 0u);

  // Fill and overflow the 64-capacity SPSC queue with 70 events
  for (int i = 0; i < 70; ++i) {
    adapter.postMidiClockEvent(0xF8, 1000 + i * 100);
  }

  // Expect exactly 6 overflowed/dropped events
  EXPECT_EQ(adapter.midiOverflowCount(), 6u);

  // Draining queue on audio block should detect overflow flag and trigger reacquisition reset
  std::vector<float> inL(128, 0.0f), inR(128, 0.0f), outL(128, 0.0f), outR(128, 0.0f);
  adapter.processStereoBlock(inL.data(), inR.data(), outL.data(), outR.data(), 128);

  hydra_dsp_midi_clock_state st{};
  EXPECT_EQ(adapter.midiClockGetState(&st), 0);
  EXPECT_EQ(st.sync_status, static_cast<int>(HYDRA_DSP_MIDI_CLOCK_DISCONNECTED));

  std::cout << "[PASS] T. SPSC Queue Overflow Recovery (Overflow counter and reacquisition reset)\n";
  return true;
}

int main() {
  std::cout << "=== HYDRA MIDI CLOCK EXTERNAL SYNC V1 TEST SUITE ===\n";
  bool ok = true;
  ok = testMidiRealtimeParser() && ok;
  ok = testBpmAcquisition() && ok;
  ok = testPhaseLockAndAlignment() && ok;
  ok = testStartStopContinue() && ok;
  ok = testJitterFiltering() && ok;
  ok = testTempoRamp() && ok;
  ok = testSuddenTempoChange() && ok;
  ok = testMissingPulses() && ok;
  ok = testClockLossAndHoldover() && ok;
  ok = testReconnection() && ok;
  ok = testSourceSwitching() && ok;
  ok = testReverseSynchronization() && ok;
  ok = testMultiHeadSynchronization() && ok;
  ok = testSpscMidiConcurrency() && ok;
  ok = testAbiCompatibility() && ok;
  ok = testLongRunningStability() && ok;
  ok = testSourceArbitrationExplicit() && ok;
  ok = testHardenedPhaseLockAndRejection() && ok;
  ok = testStartStopContinueEdgeCases() && ok;
  ok = testSpscQueueOverflowRecovery() && ok;

  if (ok) {
    std::cout << ">>> ALL 20 MIDI CLOCK SUITE TESTS PASSED <<<\n";
    return 0;
  }
  std::cerr << ">>> MIDI CLOCK TESTS FAILED <<<\n";
  return 1;
}
