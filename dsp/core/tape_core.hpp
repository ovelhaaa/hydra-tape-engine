#pragma once

#include <cstdint>
#include "global_transport_clock.hpp"
#include "midi_clock_sync.hpp"
#include "tap_tempo.hpp"

namespace hydra::dsp {

struct TapeParams {
  float flutterDepth = 14.0f;
  float wowDepth = 10.0f;
  float dropoutSeverity = 5.0f;
  float drive = 40.0f;
  float noise = 30.0f;
  float tapeSpeed = 50.0f;
  float tapeAge = 40.0f;
  float headBumpAmount = 30.0f;
  float azimuthError = 10.0f;
  float flutterRate = 6.0f;
  float wowRate = 0.8f;
  bool delayActive = false;
  float delayTimeMs = 500.0f;
  float feedback = 40.0f;
  float dryWet = 50.0f;
  float delayWet = 50.0f;
  int activeHeads = 4;
  float bpm = 120.0f;
  bool headsMusical = false;
  bool guitarFocus = false;
  float tone = 50.0f;
  bool pingPong = false;
  bool freeze = false;
  bool reverse = false;
  bool reverseSmear = false;
  bool spring = false;
  float springDecay = 60.0f;
  float springDamping = 45.0f;
  float springMix = 50.0f;
  bool warpActive = false;
  float warpSpeed = 1.0f;
  float warpGrainMs = 80.0f;
  float warpStereo = 0.5f;
  float warpFeedback = 0.0f;
  int warpMode = 0;
  int reverseMode = 0; // 0 Rhythmic, 1 Free, 2 V1
  int reverseDivision = 0; // quarter note
  int motionMode = 0; // 0 Classic, 1 Motor Drift, 2 Capstan Flutter, 3 Worn Transport
  float motionAmount = 100.0f;
  float stereoMotion = 100.0f;
  float motionInertia = 50.0f;
  float motionDriftChar = 50.0f;
  float motionTexture = 50.0f;
  float motionIrregularity = 50.0f;
  float motionInstability = 50.0f;
  float motionTensionRec = 50.0f;
  int signalPathMode = 0; // Classic remains the default
  float studioWetLevel = 100.0f; // linear output trim, never feedback
};

class TapeCore {
public:
  TapeCore(float sampleRate, float maxDelayMs = 2000.0f);
  TapeCore(float sampleRate, float maxDelayMs, float historyMs);
  ~TapeCore();

  bool isValid() const;
  void reset();
  void updateParams(const TapeParams& newParams);
  const TapeParams& params() const;
  bool isReverseClamped() const;
  float effectiveReverseMs() const;
  float requestedReverseMs() const;
  float maxHistoryMs() const;
  void getMotionTelemetry(float* outModL, float* outModR) const;
  GlobalTransportClock& transport();
  const GlobalTransportClock& transport() const;
  TapTempo& tapTempo();
  const TapTempo& tapTempo() const;
  MidiClockSync& midiClock();
  const MidiClockSync& midiClock() const;
  TempoSource tempoSource() const;
  int setTempoSource(TempoSource source);
  float manualBpm() const;
  void setManualBpm(float bpm);
  double effectiveBpm() const;
  double tapEstimatedBpm() const;
  double midiEstimatedBpm() const;

  static constexpr uint64_t kTapTempoUseCurrentTime = UINT64_MAX;
  int tapTempoEvent(uint64_t timestamp_frames = kTapTempoUseCurrentTime);
  int tapTempoEventNow();
  int tapTempoReset();
  int midiClockEvent(uint8_t statusByte, uint64_t timestamp_frames = kTapTempoUseCurrentTime);
  int midiClockReset();
  float process(float input);
  void processStereo(float inL, float inR, float* outL, float* outR);
  void processStereoBlock(const float* inL, const float* inR, float* outL, float* outR, uint32_t frames);

private:
  struct Impl;
  Impl* impl_;
};

} // namespace hydra::dsp
