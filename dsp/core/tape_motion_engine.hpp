#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace hydra::dsp {

enum class TapeMotionMode : int {
  Classic = 0,
  MotorDrift = 1,
  CapstanFlutter = 2,
  WornTransport = 3
};

struct TapeMotionParams {
  float sampleRate = 48000.0f;
  float wowRate = 0.8f;
  float wowDepth = 10.0f;
  float flutterRate = 6.0f;
  float flutterDepth = 14.0f;
  float tapeAge = 40.0f;
  float scrapeAmount = 0.00015f * 0.18f;

  int motionMode = 0;
  float motionAmount = 100.0f;
  float stereoMotion = 100.0f;

  // Mode-specific advanced parameters (50.0f = approved Phase 7.4B nominal)
  float inertia = 50.0f;
  float driftChar = 50.0f;
  float capstanTexture = 50.0f;
  float capstanIrregularity = 50.0f;
  float wornInstability = 50.0f;
  float tensionRecovery = 50.0f;
};

namespace detail {

constexpr float kTwoPi = 6.28318530718f;
constexpr float kPi = 3.14159265359f;

inline float clampf(float v, float lo, float hi) {
  return std::max(lo, std::min(v, hi));
}

inline float fastSin(float x) {
  while (x > kPi) x -= kTwoPi;
  while (x < -kPi) x += kTwoPi;
  const float B = 4.0f / kPi;
  const float C = -4.0f / (kPi * kPi);
  float y = B * x + C * x * std::fabs(x);
  const float P = 0.225f;
  return P * (y * std::fabs(y) - y) + y;
}

inline float onePoleAlpha(float fs, float fcHz) {
  if (fs <= 0.0f || fcHz <= 0.0f) return 0.0f;
  const float x = kTwoPi * fcHz / fs;
  return clampf(x / (1.0f + x), 0.0f, 0.9999f);
}

inline float lpNormGain(float alpha) {
  return 1.0f / std::sqrt(std::max(1e-7f, alpha));
}

inline float leakCoeff(float fs, float tauSeconds) {
  if (fs <= 0.0f || tauSeconds <= 0.0f) return 0.0f;
  return clampf(std::exp(-1.0f / (tauSeconds * fs)), 0.0f, 0.999999f);
}

}  // namespace detail

struct TapeMotionCoeffs {
  float sampleRate = 48000.0f;
  float invSampleRate = 1.0f / 48000.0f;

  float amountNorm = 1.0f;
  float stereoNorm = 1.0f;
  bool unitAmountAndStereo = true;

  // M1: Motor drift & inertia
  float alphaTorque = 0.0001f;
  float gainTorque = 100.0f;
  float alphaWander = 0.00001f;
  float gainWander = 300.0f;
  float alphaInertia = 0.0005f;
  float alphaVel = 0.0003f;
  float dispLeak = 0.99998f;
  float motorVelScale = 0.0045f;
  float invMaxDispRef = 1.0f / 384.0f;
  float driftBlend = 0.35f;
  float alphaDiff = 0.0001f;
  float gainDiff = 100.0f;
  float diffLeak = 0.9998f;
  float diffVelScale = 0.0008f;

  // M2: Capstan flutter & mechanical texture
  float capstanInc = 0.0f;
  float rollerInc = 0.0f;
  float alphaFreqJit = 0.0001f;
  float gainFreqJit = 100.0f;
  float capstanJitDepth = 0.09f;
  float rollerJitDepth = 0.07f;
  float alphaBpHp = 0.001f;
  float alphaBpLp = 0.01f;
  float gainBp = 10.0f;
  float capstanToneWeight = 0.58f;
  float capstanNoiseWeight = 0.65f;
  float flutterDispLeak = 0.9998f;
  float flutterVelScale = 0.0038f;
  float m2WowAlpha = 0.0001f;
  float m2WowGain = 100.0f;
  float m2WowLeak = 0.99995f;
  float m2WowVelScale = 0.001f;

  // M3: Worn transport transient perturbations
  float eventProbPerSample = 0.0f;
  int32_t eventCooldownSamples = 16000;
  float sagAttackAlpha = 0.001f;
  float sagReleaseAlpha = 0.00005f;
  float tensionK = 0.000001f;
  float tensionC = 0.0001f;
  float tensionImpulseGain = 0.0005f;
  float eventDispLeak = 0.99992f;
  float eventVelScale = 0.006f;

  float scrapeDepthScale = 0.00015f * 0.18f;
  float transitionStep = 1.0f / 7200.0f;

  void update(const TapeMotionParams& p) {
    using namespace detail;
    sampleRate = std::max(1.0f, p.sampleRate);
    invSampleRate = 1.0f / sampleRate;
    scrapeDepthScale = p.scrapeAmount;

    amountNorm = clampf(p.motionAmount, 0.0f, 100.0f) * 0.01f;
    stereoNorm = clampf(p.stereoMotion, 0.0f, 100.0f) * 0.01f;
    unitAmountAndStereo = (amountNorm == 1.0f && stereoNorm == 1.0f);

    // 150 ms smooth transition window for clickless/chirp-free mode switching
    transitionStep = 1.0f / std::max(1.0f, 0.150f * sampleRate);

    const float wowRate = clampf(p.wowRate, 0.1f, 5.0f);
    const float wowRateNorm = (wowRate - 0.1f) / (5.0f - 0.1f);
    const float wowDepthNorm = clampf(p.wowDepth, 0.0f, 100.0f) * 0.01f;

    const float flutterRate = clampf(p.flutterRate, 0.1f, 20.0f);
    const float flutterRateNorm = (flutterRate - 0.1f) / (20.0f - 0.1f);
    const float flutterDepthNorm = clampf(p.flutterDepth, 0.0f, 100.0f) * 0.01f;

    const float ageNorm = clampf(p.tapeAge, 0.0f, 100.0f) * 0.01f;

    // --- M1 coefficients ---
    const float torqueFc = 0.20f + 0.85f * wowRateNorm;
    alphaTorque = onePoleAlpha(sampleRate, torqueFc);
    gainTorque = lpNormGain(alphaTorque);

    alphaWander = onePoleAlpha(sampleRate, 0.07f + 0.12f * wowRateNorm);
    gainWander = lpNormGain(alphaWander);

    const float inertiaNorm = clampf(p.inertia, 0.0f, 100.0f) * 0.01f;
    const float inertiaScale = (p.inertia == 50.0f) ? 1.0f : std::pow(2.0f, (0.5f - inertiaNorm) * 2.0f);

    alphaInertia = onePoleAlpha(sampleRate, (0.55f + 1.80f * wowRateNorm) * inertiaScale);
    alphaVel = onePoleAlpha(sampleRate, (0.45f + 1.40f * wowRateNorm) * inertiaScale);
    dispLeak = leakCoeff(sampleRate, 2.2f - 1.0f * wowRateNorm);

    const float driftCharNorm = clampf(p.driftChar, 0.0f, 100.0f) * 0.01f;
    if (p.driftChar == 50.0f) {
      driftBlend = 0.35f;
    } else if (driftCharNorm <= 0.5f) {
      driftBlend = 0.05f + 0.60f * driftCharNorm;
    } else {
      driftBlend = 0.35f + 0.90f * (driftCharNorm - 0.5f);
    }

    motorVelScale = 0.0020f + 0.0230f * wowDepthNorm;
    invMaxDispRef = 1.0f / std::max(32.0f, 0.010f * sampleRate);

    alphaDiff = onePoleAlpha(sampleRate, 0.35f + 0.45f * wowRateNorm);
    gainDiff = lpNormGain(alphaDiff);
    diffLeak = leakCoeff(sampleRate, 0.9f);
    diffVelScale = 0.0003f + 0.0030f * wowDepthNorm;

    // --- M2 coefficients ---
    capstanInc = kTwoPi * flutterRate * invSampleRate;
    rollerInc = kTwoPi * (flutterRate * 1.6180339f) * invSampleRate;
    alphaFreqJit = onePoleAlpha(sampleRate, 0.75f);
    gainFreqJit = lpNormGain(alphaFreqJit);

    const float irregNorm = clampf(p.capstanIrregularity, 0.0f, 100.0f) * 0.01f;
    const float irregScale = (p.capstanIrregularity == 50.0f) ? 1.0f : (2.0f * irregNorm);
    capstanJitDepth = 0.09f * irregScale;
    rollerJitDepth = 0.07f * irregScale;

    const float bpLow = std::max(0.5f, flutterRate * 0.45f);
    const float bpHigh = clampf(flutterRate * 2.6f, 2.0f, 38.0f);
    alphaBpHp = onePoleAlpha(sampleRate, bpLow);
    alphaBpLp = onePoleAlpha(sampleRate, bpHigh);
    gainBp = lpNormGain(alphaBpLp);

    const float texNorm = clampf(p.capstanTexture, 0.0f, 100.0f) * 0.01f;
    if (p.capstanTexture == 50.0f) {
      capstanToneWeight = 0.58f;
      capstanNoiseWeight = 0.65f;
    } else if (texNorm <= 0.5f) {
      capstanToneWeight = 1.00f - 0.84f * texNorm;
      capstanNoiseWeight = 0.15f + 1.00f * texNorm;
    } else {
      capstanToneWeight = 0.58f - 0.76f * (texNorm - 0.5f);
      capstanNoiseWeight = 0.65f + 0.90f * (texNorm - 0.5f);
    }

    flutterDispLeak = leakCoeff(sampleRate, 0.14f);
    flutterVelScale = 0.0010f + 0.0180f * flutterDepthNorm;

    m2WowAlpha = onePoleAlpha(sampleRate, 0.25f + 0.6f * wowRateNorm);
    m2WowGain = lpNormGain(m2WowAlpha);
    m2WowLeak = leakCoeff(sampleRate, 1.4f);
    m2WowVelScale = 0.0005f + 0.0060f * wowDepthNorm;

    // --- M3 coefficients ---
    const float instabNorm = clampf(p.wornInstability, 0.0f, 100.0f) * 0.01f;
    const float instabScale = (p.wornInstability == 50.0f) ? 1.0f : (0.25f + 1.50f * instabNorm);

    const float instability = clampf(0.30f + 0.45f * ageNorm + 0.25f * wowDepthNorm, 0.1f, 1.0f);
    const float eventsPerSec = (0.25f + 0.65f * instability) * instabScale;
    eventProbPerSample = eventsPerSec * invSampleRate;
    eventCooldownSamples = static_cast<int32_t>(0.38f * sampleRate);
    sagAttackAlpha = onePoleAlpha(sampleRate, 14.0f);

    const float tensionNorm = clampf(p.tensionRecovery, 0.0f, 100.0f) * 0.01f;
    float sagReleaseHz = 0.80f;
    float zeta = 0.34f;
    if (p.tensionRecovery != 50.0f) {
      if (tensionNorm <= 0.5f) {
        sagReleaseHz = 0.35f + 0.90f * tensionNorm;
        zeta = 0.18f + 0.32f * tensionNorm;
      } else {
        sagReleaseHz = 0.80f + 1.60f * (tensionNorm - 0.5f);
        zeta = 0.34f + 0.62f * (tensionNorm - 0.5f);
      }
    }
    sagReleaseAlpha = onePoleAlpha(sampleRate, sagReleaseHz);

    const float fTension = 2.6f + 1.4f * flutterRateNorm;
    const float omega = kTwoPi * fTension * invSampleRate;
    tensionK = omega * omega;
    tensionC = 2.0f * zeta * omega;
    tensionImpulseGain = 1.2f * omega;
    eventDispLeak = leakCoeff(sampleRate, 1.1f);
    eventVelScale = (0.0014f + 0.0068f * (0.55f * wowDepthNorm + 0.45f * ageNorm)) * instabScale;
  }
};

struct TapeMotionState {
  uint32_t seedA = 0x13579BDFu;
  uint32_t seedB = 0x2468ACE1u;
  uint32_t seedC = 0x56473829u;
  uint32_t seedD = 0x9E3779B9u;

  // M1 state
  float torqueLP = 0.0f;
  float driftWander = 0.0f;
  float motorAcc = 0.0f;
  float motorVel = 0.0f;
  float motorDisp = 0.0f;
  float diffNoise = 0.0f;
  float diffDisp = 0.0f;

  // M2 state
  float capstanPhase = 0.0f;
  float rollerPhase = 0.87f;
  float freqJitter = 0.0f;
  float bpHpState = 0.0f;
  float bpLpState = 0.0f;
  float flutterDisp = 0.0f;
  float flutterDispDiff = 0.0f;
  float m2WowLP = 0.0f;
  float m2WowDisp = 0.0f;

  // M3 transient state
  int32_t eventCooldown = 0;
  float sagTarget = 0.0f;
  float sagEnv = 0.0f;
  int32_t sagHold = 0;
  float tensionPos = 0.0f;
  float tensionVel = 0.0f;
  float eventDisp = 0.0f;
  float eventDispDiff = 0.0f;

  // Runtime mode switching & transition state
  int activeMode = 0;
  int prevMode = 0;
  bool transitioning = false;
  bool pendingSnapHold = false;
  float transitionPos = 1.0f;
  float holdOffsetL = 0.0f;
  float holdOffsetR = 0.0f;
  float lastOutL = 0.0f;
  float lastOutR = 0.0f;

  void reset() {
    seedA = 0x13579BDFu;
    seedB = 0x2468ACE1u;
    seedC = 0x56473829u;
    seedD = 0x9E3779B9u;

    torqueLP = 0.0f;
    driftWander = 0.0f;
    motorAcc = 0.0f;
    motorVel = 0.0f;
    motorDisp = 0.0f;
    diffNoise = 0.0f;
    diffDisp = 0.0f;

    capstanPhase = 0.0f;
    rollerPhase = 0.87f;
    freqJitter = 0.0f;
    bpHpState = 0.0f;
    bpLpState = 0.0f;
    flutterDisp = 0.0f;
    flutterDispDiff = 0.0f;
    m2WowLP = 0.0f;
    m2WowDisp = 0.0f;

    eventCooldown = 0;
    sagTarget = 0.0f;
    sagEnv = 0.0f;
    sagHold = 0;
    tensionPos = 0.0f;
    tensionVel = 0.0f;
    eventDisp = 0.0f;
    eventDispDiff = 0.0f;

    prevMode = activeMode;
    transitioning = false;
    pendingSnapHold = false;
    transitionPos = 1.0f;
    holdOffsetL = 0.0f;
    holdOffsetR = 0.0f;
    lastOutL = 0.0f;
    lastOutR = 0.0f;
  }

  void setTargetMode(int newMode) {
    newMode = std::max(0, std::min(3, newMode));
    if (newMode == activeMode) return;
    if (!transitioning) {
      prevMode = activeMode;
      activeMode = newMode;
      transitionPos = 0.0f;
      transitioning = true;
      pendingSnapHold = false;
      holdOffsetL = 0.0f;
      holdOffsetR = 0.0f;
    } else {
      // Rapid mid-transition switch: preserve exact continuity from lastOutL/R
      const float s = transitionPos * transitionPos * (3.0f - 2.0f * transitionPos);
      prevMode = (s >= 0.5f) ? activeMode : prevMode;
      activeMode = newMode;
      transitionPos = 0.0f;
      pendingSnapHold = true;
    }
  }

  static inline uint32_t nextLCG(uint32_t& s) {
    s = s * 1664525u + 1013904223u;
    return s;
  }

  static inline float randSigned(uint32_t& s) {
    return (float((nextLCG(s) >> 8) & 0x00FFFFFFu) * (1.0f / 8388607.5f)) - 1.0f;
  }

  static inline float rand01(uint32_t& s) {
    return float(nextLCG(s) >> 8) * (1.0f / 16777216.0f);
  }

  inline void stepM1(const TapeMotionCoeffs& c) {
    using namespace detail;
    const float wTorque = randSigned(seedA) * c.gainTorque;
    const float wWander = randSigned(seedB) * c.gainWander;
    const float wDiff = randSigned(seedC) * c.gainDiff;

    torqueLP += c.alphaTorque * (wTorque - torqueLP);
    driftWander += c.alphaWander * (wWander - driftWander);

    const float dispNorm = clampf(motorDisp * c.invMaxDispRef, -1.5f, 1.5f);
    const float drive = torqueLP + c.driftBlend * driftWander;
    motorAcc += c.alphaInertia * (drive - 0.50f * dispNorm - 0.75f * motorAcc);
    motorVel += c.alphaVel * (motorAcc - 0.30f * dispNorm - motorVel);
    motorVel = clampf(motorVel, -2.0f, 2.0f);

    const float eps = c.motorVelScale * motorVel;
    motorDisp = c.dispLeak * motorDisp + eps;

    diffNoise += c.alphaDiff * (wDiff - diffNoise);
    diffDisp = c.diffLeak * diffDisp + 0.14f * c.diffVelScale * diffNoise;
  }

  inline void stepM2(const TapeMotionCoeffs& c) {
    using namespace detail;
    const float wFreq = randSigned(seedA) * c.gainFreqJit;
    const float wNoise = randSigned(seedB);
    const float wWow = randSigned(seedC) * c.m2WowGain;

    freqJitter += c.alphaFreqJit * (wFreq - freqJitter);
    const float clampedJit = clampf(freqJitter, -1.2f, 1.2f);
    capstanPhase += c.capstanInc * (1.0f + c.capstanJitDepth * clampedJit);
    if (capstanPhase >= kTwoPi) capstanPhase -= kTwoPi;
    rollerPhase += c.rollerInc * (1.0f - c.rollerJitDepth * clampedJit);
    if (rollerPhase >= kTwoPi) rollerPhase -= kTwoPi;

    const float capstanVel = 0.65f * fastSin(capstanPhase) + 0.35f * fastSin(rollerPhase + 0.41f);

    bpHpState += c.alphaBpHp * (wNoise - bpHpState);
    const float hp = wNoise - bpHpState;
    bpLpState += c.alphaBpLp * (hp * c.gainBp - bpLpState);

    const float epsCommon = c.flutterVelScale * (c.capstanToneWeight * capstanVel + c.capstanNoiseWeight * bpLpState);
    flutterDisp = c.flutterDispLeak * flutterDisp + epsCommon;

    const float epsDiff = c.flutterVelScale * (0.18f * fastSin(capstanPhase + 1.15f) + 0.14f * bpLpState);
    flutterDispDiff = c.flutterDispLeak * flutterDispDiff + epsDiff;

    m2WowLP += c.m2WowAlpha * (wWow - m2WowLP);
    m2WowDisp = c.m2WowLeak * m2WowDisp + c.m2WowVelScale * m2WowLP;
  }

  inline void stepM3(const TapeMotionCoeffs& c, bool advanceM1M2 = true) {
    using namespace detail;
    if (advanceM1M2) {
      stepM1(c);
      stepM2(c);
    }

    if (eventCooldown > 0) {
      --eventCooldown;
    } else if (rand01(seedD) < c.eventProbPerSample) {
      eventCooldown = c.eventCooldownSamples;
      const float rPol = rand01(seedD);
      const float mag = 0.50f + 0.50f * rand01(seedD);
      const float sign = (rPol < 0.75f) ? 1.0f : -0.75f;
      sagTarget = sign * mag;
      sagHold = static_cast<int32_t>((0.025f + 0.070f * rand01(seedD)) * c.sampleRate);
      tensionVel += sign * mag * c.tensionImpulseGain;
    }

    if (sagHold > 0) {
      --sagHold;
    } else {
      sagTarget = 0.0f;
    }

    const float sagAlpha = (std::fabs(sagTarget) > std::fabs(sagEnv)) ? c.sagAttackAlpha : c.sagReleaseAlpha;
    sagEnv += sagAlpha * (sagTarget - sagEnv);

    const float tensionAcc = -c.tensionK * tensionPos - c.tensionC * tensionVel;
    tensionVel += tensionAcc;
    tensionPos = clampf(tensionPos + tensionVel, -1.5f, 1.5f);

    const float epsEvent = c.eventVelScale * (sagEnv + 0.65f * tensionPos);
    eventDisp = c.eventDispLeak * eventDisp + epsEvent;

    const float epsEventDiff = 0.16f * c.eventVelScale * tensionPos;
    eventDispDiff = c.eventDispLeak * eventDispDiff + epsEventDiff;
  }

  inline void applyScaleAndClamp(float common,
                                 float diff,
                                 float scrapeTermL,
                                 float scrapeTermR,
                                 float maxMod,
                                 const TapeMotionCoeffs& c,
                                 float& outL,
                                 float& outR) const {
    using namespace detail;
    if (c.unitAmountAndStereo) {
      outL = clampf(common + diff + scrapeTermL, -maxMod, maxMod);
      outR = clampf(common - diff + scrapeTermR, -maxMod, maxMod);
    } else {
      const float scrapeCommon = 0.5f * (scrapeTermL + scrapeTermR);
      const float scrapeDiff = 0.5f * (scrapeTermL - scrapeTermR);
      const float scaledDiff = c.stereoNorm * (diff + scrapeDiff);
      outL = clampf(c.amountNorm * (common + scrapeCommon + scaledDiff), -maxMod, maxMod);
      outR = clampf(c.amountNorm * (common + scrapeCommon - scaledDiff), -maxMod, maxMod);
    }
  }

  inline void evalOrganicMode(int mode,
                              const TapeMotionCoeffs& c,
                              float modBaseSamples,
                              float scrapeL,
                              float scrapeR,
                              bool advanceM1,
                              bool advanceM2,
                              bool advanceM3,
                              float& outL,
                              float& outR) {
    const float maxMod = 0.22f * modBaseSamples;
    const float softLim = 0.18f * modBaseSamples;
    const float scrapeTermL = (c.scrapeDepthScale * modBaseSamples) * scrapeL;
    const float scrapeTermR = (c.scrapeDepthScale * modBaseSamples) * scrapeR;

    float common = 0.0f;
    float diff = 0.0f;

    if (mode == 1) {
      if (advanceM1) stepM1(c);
      if (std::fabs(motorDisp) > softLim && softLim > 1.0f) {
        motorDisp *= 0.9995f;
      }
      common = motorDisp;
      diff = diffDisp;
    } else if (mode == 2) {
      if (advanceM2) stepM2(c);
      common = flutterDisp + m2WowDisp;
      diff = flutterDispDiff;
    } else {
      if (advanceM1) stepM1(c);
      if (advanceM2) stepM2(c);
      if (advanceM3) stepM3(c, false);
      if (std::fabs(motorDisp) > softLim && softLim > 1.0f) {
        motorDisp *= 0.9995f;
      }
      common = 0.85f * motorDisp + 0.80f * flutterDisp + eventDisp;
      diff = 0.85f * diffDisp + 0.75f * flutterDispDiff + eventDispDiff;
    }

    applyScaleAndClamp(common, diff, scrapeTermL, scrapeTermR, maxMod, c, outL, outR);
  }

  inline void scaleClassicMode(float classicL,
                               float classicR,
                               float modBaseSamples,
                               const TapeMotionCoeffs& c,
                               float& outL,
                               float& outR) const {
    if (c.unitAmountAndStereo) {
      outL = classicL;
      outR = classicR;
    } else {
      const float maxMod = 0.25f * modBaseSamples;
      const float common = 0.5f * (classicL + classicR);
      const float diff = 0.5f * (classicL - classicR) * c.stereoNorm;
      outL = detail::clampf(c.amountNorm * (common + diff), -maxMod, maxMod);
      outR = detail::clampf(c.amountNorm * (common - diff), -maxMod, maxMod);
    }
  }
};

}  // namespace hydra::dsp
