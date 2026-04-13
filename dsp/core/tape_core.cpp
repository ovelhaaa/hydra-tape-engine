#include "tape_core.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <new>

namespace hydra::dsp {

namespace {
constexpr float PI = 3.14159265359f;
constexpr float TWO_PI = 6.28318530718f;

inline float clampf(float v, float lo, float hi) {
  return std::max(lo, std::min(v, hi));
}

class DCBlocker {
public:
  float process(float input) {
    float output = input - x1 + R * y1;
    x1 = input;
    y1 = output;
    if (std::fabs(y1) < 1e-20f) y1 = 0.0f;
    return output;
  }
  void clear() { x1 = y1 = 0.0f; }

private:
  float x1 = 0.0f, y1 = 0.0f;
  float R = 0.995f;
};

class BiquadFilter {
public:
  void reset() { z1 = z2 = 0.0f; }
  void setLowShelf(float fs, float freq, float Q, float gainDB) {
    float A = std::pow(10.0f, gainDB / 40.0f);
    float w0 = 2.0f * PI * freq / fs;
    float cosw0 = std::cos(w0);
    float sinw0 = std::sin(w0);
    float alpha = sinw0 / (2.0f * Q);
    float sqA = std::sqrt(A);
    float ap1 = A + 1.0f;
    float am1 = A - 1.0f;
    float twosqrtAalpha = 2.0f * sqA * alpha;
    float a0 = ap1 + am1 * cosw0 + twosqrtAalpha;
    b0 = (A * (ap1 - am1 * cosw0 + twosqrtAalpha)) / a0;
    b1 = (2.0f * A * (am1 - ap1 * cosw0)) / a0;
    b2 = (A * (ap1 - am1 * cosw0 - twosqrtAalpha)) / a0;
    a1 = (-2.0f * (am1 + ap1 * cosw0)) / a0;
    a2 = (ap1 + am1 * cosw0 - twosqrtAalpha) / a0;
  }
  void setHighShelf(float fs, float freq, float Q, float gainDB) {
    float A = std::pow(10.0f, gainDB / 40.0f);
    float w0 = 2.0f * PI * freq / fs;
    float cosw0 = std::cos(w0);
    float sinw0 = std::sin(w0);
    float alpha = sinw0 / (2.0f * Q);
    float sqA = std::sqrt(A);
    float ap1 = A + 1.0f;
    float am1 = A - 1.0f;
    float twosqrtAalpha = 2.0f * sqA * alpha;
    float a0 = ap1 - am1 * cosw0 + twosqrtAalpha;
    b0 = (A * (ap1 + am1 * cosw0 + twosqrtAalpha)) / a0;
    b1 = (-2.0f * A * (am1 + ap1 * cosw0)) / a0;
    b2 = (A * (ap1 + am1 * cosw0 - twosqrtAalpha)) / a0;
    a1 = (2.0f * (am1 - ap1 * cosw0)) / a0;
    a2 = (ap1 - am1 * cosw0 - twosqrtAalpha) / a0;
  }
  void setLowpass(float fs, float freq, float Q) {
    float w0 = 2.0f * PI * freq / fs;
    float cosw0 = std::cos(w0);
    float sinw0 = std::sin(w0);
    float alpha = sinw0 / (2.0f * Q);
    float a0 = 1.0f + alpha;
    b0 = ((1.0f - cosw0) / 2.0f) / a0;
    b1 = (1.0f - cosw0) / a0;
    b2 = b0;
    a1 = (-2.0f * cosw0) / a0;
    a2 = (1.0f - alpha) / a0;
  }
  void setHighpass(float fs, float freq, float Q) {
    float w0 = 2.0f * PI * freq / fs;
    float cosw0 = std::cos(w0);
    float sinw0 = std::sin(w0);
    float alpha = sinw0 / (2.0f * Q);
    float a0 = 1.0f + alpha;
    b0 = ((1.0f + cosw0) / 2.0f) / a0;
    b1 = -(1.0f + cosw0) / a0;
    b2 = b0;
    a1 = (-2.0f * cosw0) / a0;
    a2 = (1.0f - alpha) / a0;
  }
  float process(float input) {
    float output = b0 * input + z1;
    z1 = b1 * input - a1 * output + z2;
    z2 = b2 * input - a2 * output;
    if (std::fabs(z1) < 1e-20f) z1 = 0.0f;
    if (std::fabs(z2) < 1e-20f) z2 = 0.0f;
    return output;
  }

private:
  float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
  float z1 = 0.0f, z2 = 0.0f;
};

class AllpassFilter {
public:
  void setCoeff(float coeff) { a1 = clampf(coeff, -0.99f, 0.99f); }
  float process(float input) {
    float output = a1 * input + z1;
    z1 = input - a1 * output;
    return output;
  }

private:
  float a1 = 0.0f, z1 = 0.0f;
};

class DropoutGenerator {
public:
  void setSeverity(float sev) { severity = clampf(sev, 0.0f, 1.0f); }
  float process() {
    if (samplesUntilNext <= 0) {
      if (dropoutDuration <= 0) {
        float chance = severity * 0.0005f;
        if ((fast_rand() & 0xFFFF) < (chance * 65535.0f)) {
          dropoutDuration = 100 + (fast_rand() % 2000);
          targetLevel = 0.1f + ((fast_rand() & 0xFF) / 255.0f) * 0.4f;
          samplesUntilNext = dropoutDuration;
        } else {
          targetLevel = 1.0f;
          samplesUntilNext = 1000 + (fast_rand() % 5000);
        }
      } else {
        dropoutDuration--;
        samplesUntilNext = 1;
      }
    }
    samplesUntilNext--;
    float smoothCoeff = (targetLevel < smoothedLevel) ? 0.0005f : 0.002f;
    smoothedLevel += smoothCoeff * (targetLevel - smoothedLevel);
    return smoothedLevel;
  }

private:
  uint32_t fast_rand() {
    seed = seed * 1664525u + 1013904223u;
    return seed;
  }
  float smoothedLevel = 1.0f, targetLevel = 1.0f;
  int samplesUntilNext = 0, dropoutDuration = 0;
  float severity = 0.5f;
  uint32_t seed = 987654321u;
};

class TapeNoiseGenerator {
public:
  explicit TapeNoiseGenerator(float fs) {
    uint32_t t = static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    seed = 123456789u + t;
    hissShaper.setHighShelf(fs, 3000.0f, 0.7f, 6.0f);
  }
  float next() {
    uint32_t r = fast_rand();
    if (r & 1) state[0] = white();
    else if (r & 2) state[1] = white();
    else state[2] = white();
    float pink = (state[0] + state[1] + state[2]) * 0.33f;
    return hissShaper.process(pink);
  }

private:
  uint32_t fast_rand() { seed = seed * 1664525u + 1013904223u; return seed; }
  float white() { uint32_t r = fast_rand(); return ((float)(r & 0xFFFF) / 32768.0f) - 1.0f; }
  float state[3] = {0.0f, 0.0f, 0.0f};
  uint32_t seed = 1;
  BiquadFilter hissShaper;
};

class DelayAllpass {
public:
  ~DelayAllpass() { delete[] buffer; }
  void init(int len) {
    delete[] buffer;
    size = len;
    buffer = new float[size]{};
    idx = 0;
  }
  void setCoeff(float f) { feedback = f; }
  float process(float input) {
    if (!buffer) return input;
    float bufOut = buffer[idx];
    float node = input + feedback * bufOut;
    if (std::fabs(node) < 1e-15f) node = 0.0f;
    float output = bufOut - feedback * node;
    buffer[idx] = node;
    idx = (idx + 1) % size;
    return output;
  }

private:
  float* buffer = nullptr;
  int size = 0, idx = 0;
  float feedback = 0.5f;
};

} // namespace

struct TapeCore::Impl {
  explicit Impl(float fs, float maxDelayMs)
      : sampleRate(fs), noiseGen(fs) {
    bufferSize = static_cast<int32_t>(fs * (maxDelayMs / 1000.0f));
    delayLine = new (std::nothrow) float[bufferSize]{};
    delayLineR = new (std::nothrow) float[bufferSize]{};
    if (!delayLine || !delayLineR) {
      delete[] delayLine;
      delete[] delayLineR;
      delayLine = delayLineR = nullptr;
      bufferSize = 0;
      return;
    }
    const float springCoeffs[6] = {0.7f, 0.65f, 0.6f, 0.6f, 0.5f, 0.5f};
    const int springTimes[6] = {223, 367, 491, 647, 821, 1039};
    for (int i = 0; i < 6; ++i) {
      springAP_L[i].init(springTimes[i]);
      springAP_R[i].init(springTimes[i] + 23);
      springAP_L[i].setCoeff(springCoeffs[i]);
      springAP_R[i].setCoeff(springCoeffs[i]);
      springLPF_L[i].setLowpass(fs, 2500.0f, 0.5f);
      springLPF_R[i].setLowpass(fs, 2500.0f, 0.5f);
    }
    const float reverseCoeffs[4] = {0.6f, 0.55f, 0.5f, 0.45f};
    const int revTimes[4] = {151, 313, 569, 797};
    for (int i = 0; i < 4; ++i) {
      reverseAP_L[i].init(revTimes[i]);
      reverseAP_R[i].init(revTimes[i] + 17);
      reverseAP_L[i].setCoeff(reverseCoeffs[i]);
      reverseAP_R[i].setCoeff(reverseCoeffs[i]);
    }
    updateFilters();
    flutterLPF.setLowpass(fs, 15.0f, 0.707f);
  }
  ~Impl() { delete[] delayLine; delete[] delayLineR; }

  bool valid() const { return delayLine && delayLineR && bufferSize > 0; }

  float saturator(float x) {
    if (x > 0.5f) x = 0.5f + (x - 0.5f) * 0.8f;
    if (x > 1.5f) return 1.0f;
    if (x < -1.5f) return -1.0f;
    return x - (0.1f * x * x * x);
  }
  float feedbackCompressor(float x) {
    const float thresh = 0.6f, ratio = 1.5f;
    float a = std::fabs(x);
    if (a <= thresh) return x;
    float excess = a - thresh;
    float compressed = thresh + excess / ratio;
    return std::copysign(compressed, x);
  }
  float outputLimiter(float x) {
    if (x > 0.9f) { float excess = x - 0.9f; x = 0.9f + excess * 0.1f; }
    else if (x < -0.9f) { float excess = x + 0.9f; x = -0.9f + excess * 0.1f; }
    return clampf(x, -0.99f, 0.99f);
  }

  float readTapeAt(float delaySamples, float* buffer) {
    if (!buffer || bufferSize == 0) return 0.0f;
    delaySamples = clampf(delaySamples, 2.0f, (float)bufferSize - 4.0f);
    float readPos = (float)writeHead - delaySamples;
    if (readPos < 0.0f) readPos += bufferSize;
    int32_t r = (int32_t)readPos;
    float f = readPos - r;
    int32_t i1 = r;
    int32_t i2 = (r > 0) ? r - 1 : bufferSize - 1;
    int32_t i0 = (r < bufferSize - 1) ? r + 1 : 0;
    int32_t i3 = (i0 < bufferSize - 1) ? i0 + 1 : 0;
    float d1 = buffer[i1], d0 = buffer[i0], d2 = buffer[i2], d3 = buffer[i3];
    float c0 = d1;
    float c1 = 0.5f * (d0 - d2);
    float c2 = d2 - 2.5f * d1 + 2.0f * d0 - 0.5f * d3;
    float c3 = 0.5f * (d3 - d1) + 1.5f * (d1 - d0);
    return ((c3 * f + c2) * f + c1) * f + c0;
  }

  float readTapeReverse(float delaySamples, float* buffer) {
    if (!buffer || bufferSize <= 0) return 0.0f;
    delaySamples = clampf(delaySamples, 2.0f, (float)bufferSize - 4.0f);
    int32_t delayInt = (int32_t)delaySamples;
    if (std::abs(delayInt - reverseWindowSize) > 1000) {
      reverseCounter = 0;
      reverseWindowSize = delayInt;
    }
    reverseCounter = (reverseCounter + 1) % std::max(1, delayInt);
    float readPos = (float)writeHead - delaySamples + (float)reverseCounter;
    int32_t r = ((int32_t)readPos % bufferSize + bufferSize) % bufferSize;
    float f = readPos - std::floor(readPos);
    int32_t i1 = r;
    int32_t i0 = (r > 0) ? r - 1 : bufferSize - 1;
    int32_t i2 = (r < bufferSize - 1) ? r + 1 : 0;
    int32_t i3 = (i2 < bufferSize - 1) ? i2 + 1 : 0;
    float d1 = buffer[i1], d0 = buffer[i0], d2 = buffer[i2], d3 = buffer[i3];
    float c0 = d1;
    float c1 = 0.5f * (d0 - d2);
    float c2 = d2 - 2.5f * d1 + 2.0f * d0 - 0.5f * d3;
    float c3 = 0.5f * (d3 - d1) + 1.5f * (d1 - d0);
    return ((c3 * f + c2) * f + c1) * f + c0;
  }

  void updateFilters();

  float sampleRate;
  TapeParams currentParams{};
  float flutterPhase = 0.0f, wowPhase = 0.0f, azimuthPhase = 0.0f;
  BiquadFilter flutterLPF;
  DropoutGenerator dropout;
  TapeNoiseGenerator noiseGen;
  BiquadFilter headBump, tapeRolloff, outputLPF;
  AllpassFilter azimuthFilter;
  DCBlocker dcBlocker;
  BiquadFilter inputHPF, inputLPF;
  BiquadFilter headBumpR, tapeRolloffR, outputLPFR;
  AllpassFilter azimuthFilterR;
  DCBlocker dcBlockerR;
  BiquadFilter inputHPFR, inputLPFR;
  BiquadFilter feedbackLPF, feedbackLPFR, feedbackHPF, feedbackHPFR;
  AllpassFilter feedbackAllpass, feedbackAllpassR;
  DelayAllpass springAP_L[6], springAP_R[6], reverseAP_L[4], reverseAP_R[4];
  BiquadFilter springLPF_L[6], springLPF_R[6];
  float freezeFade = 0.0f;
  float delayEnableRamp = 0.0f, smoothedDelaySamples = 0.0f;
  float* delayLine = nullptr;
  float* delayLineR = nullptr;
  int32_t bufferSize = 0, writeHead = 0;
  int32_t reverseCounter = 0, reverseWindowSize = 0;
};

void TapeCore::Impl::updateFilters() {
  float speedMod = currentParams.tapeSpeed * 0.01f;
  float ageMod = currentParams.tapeAge * 0.01f;
  float toneMod = currentParams.tone * 0.01f;
  if (currentParams.guitarFocus) {
    inputHPF.setLowShelf(sampleRate, 150.0f, 0.7f, -30.0f);
    inputHPFR.setLowShelf(sampleRate, 150.0f, 0.7f, -30.0f);
    inputLPF.setLowpass(sampleRate, 5000.0f, 0.707f);
    inputLPFR.setLowpass(sampleRate, 5000.0f, 0.707f);
  } else {
    inputHPF.setLowShelf(sampleRate, 20.0f, 0.7f, 0.0f);
    inputHPFR.setLowShelf(sampleRate, 20.0f, 0.7f, 0.0f);
    inputLPF.setLowpass(sampleRate, 20000.0f, 0.707f);
    inputLPFR.setLowpass(sampleRate, 20000.0f, 0.707f);
  }
  float bumpGain = currentParams.headBumpAmount * 0.05f;
  headBump.setLowShelf(sampleRate, 100.0f, 0.7f, bumpGain);
  headBumpR.setLowShelf(sampleRate, 100.0f, 0.7f, bumpGain);
  float baseFreq = 6000.0f + (speedMod * 10000.0f);
  float ageFactor = 1.0f - (ageMod * 0.90f);
  float toneFactor = (toneMod - 0.5f) * 2.0f;
  if (toneFactor > 0.0f) ageFactor = std::min(1.0f, ageFactor + toneFactor * 0.5f);
  else ageFactor *= (1.0f + toneFactor * 0.5f);
  float finalCutoff = std::max(400.0f, baseFreq * ageFactor);
  tapeRolloff.setHighShelf(sampleRate, finalCutoff, 0.5f, -50.0f);
  tapeRolloffR.setHighShelf(sampleRate, finalCutoff, 0.5f, -50.0f);
  outputLPF.setLowpass(sampleRate, finalCutoff, 0.707f);
  outputLPFR.setLowpass(sampleRate, finalCutoff, 0.707f);
  float fbCutoff = 1500.0f + (speedMod * 10500.0f);
  feedbackLPF.setLowpass(sampleRate, fbCutoff, 0.5f);
  feedbackLPFR.setLowpass(sampleRate, fbCutoff, 0.5f);
  feedbackHPF.setHighpass(sampleRate, 300.0f, 0.5f);
  feedbackHPFR.setHighpass(sampleRate, 300.0f, 0.5f);
  float allpassCoeff = 0.3f + ageMod * 0.4f;
  feedbackAllpass.setCoeff(allpassCoeff);
  feedbackAllpassR.setCoeff(allpassCoeff);
}

TapeCore::TapeCore(float sampleRate, float maxDelayMs) : impl_(new Impl(sampleRate, maxDelayMs)) {}
TapeCore::~TapeCore() { delete impl_; }
bool TapeCore::isValid() const { return impl_ && impl_->valid(); }
const TapeParams& TapeCore::params() const { return impl_->currentParams; }
void TapeCore::reset() {
  if (!isValid()) return;
  std::memset(impl_->delayLine, 0, sizeof(float) * impl_->bufferSize);
  std::memset(impl_->delayLineR, 0, sizeof(float) * impl_->bufferSize);
  impl_->writeHead = 0;
  impl_->flutterPhase = impl_->wowPhase = impl_->azimuthPhase = 0.0f;
}
void TapeCore::updateParams(const TapeParams& newParams) {
  if (!isValid()) return;
  impl_->currentParams = newParams;
  impl_->dropout.setSeverity(newParams.dropoutSeverity);
  impl_->updateFilters();
}

float TapeCore::process(float input) {
  float oL, oR;
  processStereo(input, input, &oL, &oR);
  return oL;
}

void TapeCore::processStereo(float inL, float inR, float* outL, float* outR) {
  if (!isValid()) { *outL = inL; *outR = inR; return; }
  TapeParams* p = &impl_->currentParams;
  float flutterInc = TWO_PI * p->flutterRate / impl_->sampleRate;
  impl_->flutterPhase += flutterInc;
  if (impl_->flutterPhase > TWO_PI) impl_->flutterPhase -= TWO_PI;
  float wowInc = TWO_PI * p->wowRate / impl_->sampleRate;
  impl_->wowPhase += wowInc;
  if (impl_->wowPhase > TWO_PI) impl_->wowPhase -= TWO_PI;
  float rawMod = (std::sin(impl_->flutterPhase) * (p->flutterDepth * 0.01f)) +
                 (std::sin(impl_->wowPhase) * (p->wowDepth * 0.01f));
  float mod = impl_->flutterLPF.process(rawMod);
  float dropoutGain = impl_->dropout.process();
  float hiss = (p->noise > 0.001f) ? impl_->noiseGen.next() * (p->noise * 0.001f) * (1.0f + (2.0f * (1.0f - dropoutGain))) : 0.0f;

  if (p->delayActive) impl_->delayEnableRamp = std::min(1.0f, impl_->delayEnableRamp + 0.001f);
  else impl_->delayEnableRamp = 0.0f;
  float targetDelay = p->delayTimeMs * impl_->sampleRate * 0.001f;
  impl_->smoothedDelaySamples += 0.0001f * (targetDelay - impl_->smoothedDelaySamples);

  auto ch = [&](float input, float* buffer, BiquadFilter& hb, BiquadFilter& tr,
                BiquadFilter& outLPF, DCBlocker& dc, BiquadFilter& iHP,
                BiquadFilter& iLP, BiquadFilter& fbLPF, BiquadFilter& fbHPF,
                AllpassFilter& fbAllpass) {
    float condInput = iLP.process(iHP.process(input));
    float tapeSig = 0.0f;
    float baseDelay = impl_->smoothedDelaySamples;
    float d1 = baseDelay * 0.33f + mod * 80.0f;
    float d2 = baseDelay * 0.66f + mod * 120.0f;
    float d3 = baseDelay + mod * 160.0f;
    if (!p->delayActive) tapeSig = impl_->readTapeAt(200.0f + mod * 80.0f, buffer);
    else if (p->reverse) tapeSig = impl_->readTapeReverse(d3, buffer);
    else tapeSig = impl_->readTapeAt(d3, buffer);
    tapeSig *= dropoutGain;
    tapeSig = outLPF.process(tr.process(hb.process(tapeSig)));
    float feedSig = 0.0f;
    if (p->delayActive) {
      feedSig = std::tanh(fbAllpass.process(fbHPF.process(fbLPF.process(tapeSig))) * 1.3f) / 1.3f;
      feedSig *= std::min(0.88f, p->feedback * 0.01f);
      feedSig = impl_->feedbackCompressor(feedSig) * impl_->delayEnableRamp;
    }
    float recSig = dc.process(condInput * (p->drive * 0.05f) + feedSig);
    recSig = clampf(recSig, -4.0f, 4.0f);
    if (!p->freeze) buffer[impl_->writeHead] = impl_->saturator(recSig);
    float mix = p->dryWet * 0.01f;
    return impl_->outputLimiter((input * (1.0f - mix)) + (tapeSig * mix));
  };

  *outL = ch(inL, impl_->delayLine, impl_->headBump, impl_->tapeRolloff, impl_->outputLPF,
             impl_->dcBlocker, impl_->inputHPF, impl_->inputLPF, impl_->feedbackLPF,
             impl_->feedbackHPF, impl_->feedbackAllpass);
  *outR = ch(inR, impl_->delayLineR, impl_->headBumpR, impl_->tapeRolloffR, impl_->outputLPFR,
             impl_->dcBlockerR, impl_->inputHPFR, impl_->inputLPFR, impl_->feedbackLPFR,
             impl_->feedbackHPFR, impl_->feedbackAllpassR);
  *outL += hiss * 0.5f;
  *outR += hiss * 0.5f;
  impl_->writeHead = (impl_->writeHead + 1) % impl_->bufferSize;
}

} // namespace hydra::dsp
