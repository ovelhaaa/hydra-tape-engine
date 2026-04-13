#pragma once

#include <cstdint>

namespace hydra::dsp {

struct TapeParams {
  float flutterDepth = 20.0f;
  float wowDepth = 15.0f;
  float dropoutSeverity = 8.0f;
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
};

class TapeCore {
public:
  TapeCore(float sampleRate, float maxDelayMs = 2000.0f);
  ~TapeCore();

  bool isValid() const;
  void reset();
  void updateParams(const TapeParams& newParams);
  const TapeParams& params() const;
  float process(float input);
  void processStereo(float inL, float inR, float* outL, float* outR);

private:
  struct Impl;
  Impl* impl_;
};

} // namespace hydra::dsp
