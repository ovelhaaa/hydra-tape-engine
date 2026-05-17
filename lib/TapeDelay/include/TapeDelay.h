#ifndef TAPE_DELAY_IMPROVED_H
#define TAPE_DELAY_IMPROVED_H

#include "esp_heap_caps.h"
#include "hydra_mech_noise.hpp"
#include <Arduino.h>
#include <math.h>

// ============================================================================
// DSP CONSTANTS
// ============================================================================
#ifndef TWO_PI
#define TWO_PI               6.28318530718f
#endif
#define BIQUAD_Q_BUTTERWORTH 0.707f
#define DENORMAL_THRESHOLD   1e-20f
#define HERMITE_MIN_DELAY    2.0f
#define HERMITE_MARGIN       4.0f
#define TAPE_BUFFER_GUARD    4
#define FEEDBACK_MAX_SAFE    1.05f  // Allow self-oscillation (> 1.0)
#define FEEDBACK_CLAMP       1.2f

// Macro to force inline on critical audio functions
#define AUDIO_INLINE inline __attribute__((always_inline))

// ============================================================================
// DC BLOCKER - Essential for delay loops with saturation
// ============================================================================
class DCBlocker {
private:
  float x1, y1;
  float R = 0.995f;

public:
  DCBlocker() : x1(0), y1(0) {}

  AUDIO_INLINE float process(float input) {
    float output = input - x1 + R * y1;
    x1 = input;
    y1 = output;
    if (fabsf(y1) < 1e-20f) y1 = 0.0f;
    return output;
  }

  void clear() {
    x1 = 0;
    y1 = 0;
  }
};

// ============================================================================
// BIQUAD FILTER
// ============================================================================
class BiquadFilter {
private:
  float b0, b1, b2, a1, a2;
  float z1, z2;

public:
  BiquadFilter() : b0(1), b1(0), b2(0), a1(0), a2(0), z1(0), z2(0) {}

  void reset() { z1 = z2 = 0; }

  // Low shelf - simula head bump
  void setLowShelf(float fs, float freq, float Q, float gainDB) {
    float A = powf(10.0f, gainDB / 40.0f);
    float w0 = 2.0f * PI * freq / fs;
    float cosw0 = cosf(w0);
    float sinw0 = sinf(w0);
    float alpha = sinw0 / (2.0f * Q);
    float sqA = sqrtf(A);

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

  // Peaking EQ - head bump dedicado sem elevar todo o grave
  void setPeak(float fs, float freq, float Q, float gainDB) {
    freq = fminf(freq, 0.45f * fs);
    Q = fmaxf(Q, 0.001f);
    float A = powf(10.0f, gainDB * 0.025f);
    float w0 = TWO_PI * freq / fs;
    float s = sinf(w0);
    float c = cosf(w0);
    float alpha = s / (2.0f * Q);

    float a0 = 1.0f + alpha / A;
    b0 = (1.0f + alpha * A) / a0;
    b1 = (-2.0f * c) / a0;
    b2 = (1.0f - alpha * A) / a0;
    a1 = (-2.0f * c) / a0;
    a2 = (1.0f - alpha / A) / a0;
  }

  // High shelf - simula perda de agudos
  void setHighShelf(float fs, float freq, float Q, float gainDB) {
    float A = powf(10.0f, gainDB / 40.0f);
    float w0 = 2.0f * PI * freq / fs;
    float cosw0 = cosf(w0);
    float sinw0 = sinf(w0);
    float alpha = sinw0 / (2.0f * Q);
    float sqA = sqrtf(A);

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

  // Lowpass 2 polos
  void setLowpass(float fs, float freq, float Q) {
    float w0 = 2.0f * PI * freq / fs;
    float cosw0 = cosf(w0);
    float sinw0 = sinf(w0);
    float alpha = sinw0 / (2.0f * Q);

    float a0 = 1.0f + alpha;
    b0 = ((1.0f - cosw0) / 2.0f) / a0;
    b1 = (1.0f - cosw0) / a0;
    b2 = b0;
    a1 = (-2.0f * cosw0) / a0;
    a2 = (1.0f - alpha) / a0;
  }

  // Highpass 2 polos - remove low frequencies
  void setHighpass(float fs, float freq, float Q) {
    float w0 = 2.0f * PI * freq / fs;
    float cosw0 = cosf(w0);
    float sinw0 = sinf(w0);
    float alpha = sinw0 / (2.0f * Q);

    float a0 = 1.0f + alpha;
    b0 = ((1.0f + cosw0) / 2.0f) / a0;
    b1 = -(1.0f + cosw0) / a0;
    b2 = b0;
    a1 = (-2.0f * cosw0) / a0;
    a2 = (1.0f - alpha) / a0;
  }

  // Transposed Direct Form II (TDF2) - Numerically stable
  AUDIO_INLINE float process(float input) {
    float output = b0 * input + z1;
    z1 = b1 * input - a1 * output + z2;
    z2 = b2 * input - a2 * output;
    // Denormal protection
    if (fabsf(z1) < 1e-20f) z1 = 0.0f;
    if (fabsf(z2) < 1e-20f) z2 = 0.0f;
    return output;
  }
};

// ============================================================================
// ONE-POLE LOWPASS - cheap dynamic HF loss for dropout/contact events
// ============================================================================
class OnePoleLP {
private:
  float a;
  float z;

public:
  OnePoleLP() : a(0.1f), z(0.0f) {}
  void reset() { z = 0.0f; }

  void setCutoff(float fs, float fc) {
    fc = constrain(fc, 20.0f, 0.45f * fs);
    float x = expf(-TWO_PI * fc / fs);
    a = 1.0f - x;
  }

  void setCutoffFast(float fs, float fc) {
    fc = constrain(fc, 20.0f, 0.45f * fs);
    float x = TWO_PI * fc / fs;
    a = constrain(x / (1.0f + x), 0.0f, 1.0f);
  }

  AUDIO_INLINE float process(float input) {
    z += a * (input - z);
    if (fabsf(z) < DENORMAL_THRESHOLD) z = 0.0f;
    return z;
  }
};

// ============================================================================
// PINK NOISE com perfil espectral de tape hiss
// ============================================================================
class TapeNoiseGenerator {
private:
  float state[3];
  uint32_t seed;
  BiquadFilter hissShaper;

  AUDIO_INLINE uint32_t fast_rand() {
    seed = seed * 1664525u + 1013904223u;
    return seed;
  }

  AUDIO_INLINE float white() {
    uint32_t r = fast_rand();
    return ((float)(r & 0xFFFF) / 32768.0f) - 1.0f;
  }

public:
  TapeNoiseGenerator(float fs, uint32_t seedOffset = 0u) {
    state[0] = state[1] = state[2] = 0;
    seed = 123456789 + micros() + seedOffset;
    hissShaper.setHighShelf(fs, 3000.0f, 0.7f, 6.0f);
  }

  AUDIO_INLINE float next() {
    uint32_t r = fast_rand(); // CORREÇÃO: Erro de sintaxe corrigido aqui
    if (r & 1)
      state[0] = white();
    else if (r & 2)
      state[1] = white();
    else
      state[2] = white();

    float pink = (state[0] + state[1] + state[2]) * 0.33f;
    return hissShaper.process(pink);
  }
};

// ============================================================================
// MULTI-SCALE DROPOUT GENERATOR
// ============================================================================
struct DropoutFrame {
  float ampGain = 1.0f;
  float hfLoss = 1.0f;
};

class DropoutGenerator {
private:
  struct State {
    float amp = 1.0f;
    float ampTarget = 1.0f;
    float hf = 1.0f;
    float hfTarget = 1.0f;
    float severity = 0.5f;
    int remain = 0;
    uint32_t s = 987654321u;

    void reset(uint32_t seed) {
      amp = ampTarget = 1.0f;
      hf = hfTarget = 1.0f;
      remain = 0;
      s = seed;
    }

    AUDIO_INLINE uint32_t randu() {
      s = s * 1664525u + 1013904223u;
      return s;
    }

    AUDIO_INLINE float rand01() {
      return float(randu() >> 8) * (1.0f / 16777216.0f);
    }

    AUDIO_INLINE void maybeTrigger(float fs) {
      float safeFs = fmaxf(1.0f, fs);
      float p = (severity * 0.00012f) * (48000.0f / safeFs);
      if (rand01() < p) {
        float r = rand01();

        if (r < 0.72f) {
          remain = int((0.001f + 0.019f * rand01()) * fs);  // dust
          ampTarget = 0.65f + 0.30f * rand01();
          hfTarget = 0.35f + 0.45f * rand01();
        } else if (r < 0.96f) {
          remain = int((0.020f + 0.130f * rand01()) * fs);  // oxide
          ampTarget = 0.18f + 0.55f * rand01();
          hfTarget = 0.12f + 0.35f * rand01();
        } else {
          remain = int((0.150f + 0.600f * rand01()) * fs);  // spacing/contact
          ampTarget = 0.05f + 0.35f * rand01();
          hfTarget = 0.05f + 0.20f * rand01();
        }
      }
    }

    AUDIO_INLINE void process(float fs) {
      if (remain <= 0) {
        ampTarget = 1.0f;
        hfTarget = 1.0f;
        maybeTrigger(fs);
      } else {
        --remain;
      }

      float scale = 48000.0f / fmaxf(1.0f, fs);
      float down = (0.0012f + 0.006f * severity) * scale;
      float up = 0.00025f * scale;
      float ca = (ampTarget < amp) ? down : up;
      float ch = (hfTarget < hf) ? down * 1.8f : up * 0.7f;

      amp += ca * (ampTarget - amp);
      hf += ch * (hfTarget - hf);
    }
  };

  State common;
  State local[2];
  DropoutFrame frame;
  float severity = 0.5f;

public:
  DropoutGenerator() { reset(); }

  void reset() {
    common.reset(987654321u);
    local[0].reset(2246822519u);
    local[1].reset(3266489917u);
    severity = 0.5f;
    setSeverity(severity);
    frame = {1.0f, 1.0f};
  }

  void setSeverity(float sev) {
    severity = constrain(sev, 0.0f, 1.0f);
    common.severity = severity;
    local[0].severity = severity * 0.45f;
    local[1].severity = severity * 0.45f;
  }

  AUDIO_INLINE void beginFrame(float fs) {
    common.process(fs);
    frame.ampGain = common.amp;
    frame.hfLoss = common.hf;
  }

  AUDIO_INLINE DropoutFrame value(int channel, float fs) {
    int ch = (channel != 0) ? 1 : 0;
    local[ch].process(fs);

    DropoutFrame out;
    out.ampGain = constrain(frame.ampGain * (0.75f + 0.25f * local[ch].amp),
                            0.02f, 1.0f);
    out.hfLoss = constrain(frame.hfLoss * (0.70f + 0.30f * local[ch].hf),
                           0.02f, 1.0f);
    return out;
  }
};

// ============================================================================
// ALLPASS FILTER
// ============================================================================
class AllpassFilter {
private:
  float a1, z1;

public:
  AllpassFilter() : a1(0), z1(0) {}

  void setCoeff(float coeff) { a1 = constrain(coeff, -0.99f, 0.99f); }

  void reset() { z1 = 0; }

  AUDIO_INLINE float process(float input) {
    float output = a1 * input + z1;
    z1 = input - a1 * output;
    return output;
  }
};

// Schroeder Allpass (Delay-based) for Reverb
class DelayAllpass {
private:
  float *buffer;
  int size;
  int idx;
  float feedback;

public:
  DelayAllpass() : buffer(nullptr), size(0), idx(0), feedback(0.5f) {}
  ~DelayAllpass() { if (buffer) delete[] buffer; }

  void init(int len) {
    if (buffer) delete[] buffer;
    size = len;
    // Allocate in internal RAM for speed, usually small enough
    buffer = new float[size];
    memset(buffer, 0, size * sizeof(float));
    idx = 0;
  }

  void setCoeff(float f) { feedback = f; }

  // Schroeder Allpass Process
  // y[n] = -g*x[n] + x[n-D] + g*y[n-D]
  AUDIO_INLINE float process(float input) {
    if (!buffer) return input;
    
    float bufOut = buffer[idx];
    float node = input + feedback * bufOut;
    // Anti-denormal flush
    if (fabsf(node) < 1e-15f) node = 0.0f;
    float output = bufOut - feedback * node;
    
    buffer[idx] = node;
    
    idx++;
    if (idx >= size) idx = 0;
    
    return output;
  }
};

// ============================================================================
// PARÂMETROS
// ============================================================================
struct TapeParams {
  float flutterDepth;
  float wowDepth;
  float dropoutSeverity;
  float drive;
  float noise;
  float tapeSpeed;
  float tapeAge;
  float headBumpAmount;
  float azimuthError;
  float flutterRate;
  float wowRate;
  bool delayActive;
  float delayTimeMs;
  float feedback;
  float dryWet;
  int activeHeads; // Bitmask: 1=Head1, 2=Head2, 4=Head3
  float bpm;       // tempo em BPM, usado quando heads em modo musical
  bool
      headsMusical; // se true, usa mapeamento musical para posições das cabeças
  bool guitarFocus; // Input bandpass for guitar
  float tone;       // Tone control (0.0 dark - 1.0 bright)
  
  // === NEW EFFECT MODES ===
  bool pingPong;        // L/R alternating feedback (modifier for delay)
  bool freeze;          // Infinite loop mode (standalone)
  bool reverse;         // Read buffer backwards
  bool reverseSmear;    // Add allpass diffusion = Reverse Reverb
  bool spring;          // Spring reverb post-delay
  float springDecay;    // 0.0-1.0 (maps to 1-5 seconds)
  float springDamping;  // 0.0-1.0 (tape-like high cut)
  float springMix;      // 0-100% dry/wet mix for spring reverb
};


struct FastTanhLUT {
  static constexpr int N = 1024;
  float table[N + 1];

  void init() {
    for (int i = 0; i <= N; ++i) {
      float x = -4.0f + 8.0f * (float(i) / float(N));
      table[i] = tanhf(x);
    }
  }

  AUDIO_INLINE float process(float x) const {
    x = fminf(4.0f, fmaxf(-4.0f, x));
    float u = (x + 4.0f) * (float(N) * 0.125f);
    int i = int(u);
    if (i >= N) return table[N];
    float f = u - float(i);
    return table[i] + f * (table[i + 1] - table[i]);
  }
};

struct TapeMagnetics {
  const FastTanhLUT* lut = nullptr;
  float fs = 48000.0f;
  float m = 0.0f;
  float biasPhase = 0.0f;

  float drive = 1.0f;
  float biasAmount = 0.08f;
  float biasHz = 18000.0f;
  float coercivity = 0.12f;
  float remanence = 0.985f;
  float satNorm = 1.0f;

  void updateParams(float sampleRate, const TapeParams& params) {
    fs = fmaxf(1.0f, sampleRate);
    float driveNorm = constrain(params.drive * 0.01f, 0.0f, 1.0f);
    float ageNorm = constrain(params.tapeAge * 0.01f, 0.0f, 1.0f);
    float speedNorm = constrain(params.tapeSpeed * 0.01f, 0.0f, 1.0f);

    drive = 0.9f + driveNorm * 0.35f;
    biasAmount = 0.035f + (1.0f - speedNorm) * 0.035f + driveNorm * 0.035f;
    biasHz = fminf(18000.0f, fs * 0.45f);
    coercivity = 0.07f + ageNorm * 0.10f;
    remanence = 0.970f + ageNorm * 0.024f;
    satNorm = 1.0f / fmaxf(0.55f, 0.82f + 0.18f * remanence);
  }

  AUDIO_INLINE float process(float x) {
    if (!lut) return x;
    biasPhase += biasHz / fs;
    biasPhase -= floorf(biasPhase);
    float tri = 4.0f * fabsf(biasPhase - 0.5f) - 1.0f;

    // Gate the inaudible HF bias out of truly silent record paths so an
    // empty buffer does not accumulate a bias tone. Existing remanent
    // magnetization still decays through the hysteresis term below.
    float biasGate = (fabsf(x) > 1e-7f) ? 1.0f : 0.0f;
    float xb = x * drive + (biasAmount * biasGate * tri);
    float h = xb + coercivity * m;
    float y = lut->process(h);

    m = remanence * m + (1.0f - remanence) * y;
    if (fabsf(m) < DENORMAL_THRESHOLD) m = 0.0f;

    return satNorm * (0.82f * y + 0.18f * m);
  }

  AUDIO_INLINE void reset() {
    m = 0.0f;
    biasPhase = 0.0f;
  }
};


// ============================================================================
// TAPE MODEL
// ============================================================================
class TapeModel {
private:
  float sampleRate;
  TapeParams currentParams;

  FastTanhLUT tanhLUT;
  TapeMagnetics magneticsL;
  TapeMagnetics magneticsR;

  float flutterPhase, wowPhase;
  float azimuthPhase;
  float flutterInc, wowInc, azimuthInc;
  float delaySmoothAlpha, delayRampInc;
  float mechNoiseSlowAlpha, mechNoiseFastAlpha;
  BiquadFilter flutterLPF;
  BiquadFilter flutterLPFR;
  hydra::dsp::MechNoise mechNoiseL;
  hydra::dsp::MechNoise mechNoiseR;

  DropoutGenerator dropout;
  TapeNoiseGenerator noiseGen;
  TapeNoiseGenerator noiseGenR;
  BiquadFilter headBump;
  BiquadFilter tapeRolloff;
  BiquadFilter outputLPF;
  OnePoleLP dropoutLPF;
  AllpassFilter azimuthFilter;
  DCBlocker dcBlocker;

  // Guitar Focus Filters (Input)
  BiquadFilter inputHPF;
  BiquadFilter inputLPF;

  // Right Channel Filters
  BiquadFilter headBumpR;
  BiquadFilter tapeRolloffR;
  BiquadFilter outputLPFR;
  OnePoleLP dropoutLPFR;
  AllpassFilter azimuthFilterR;
  DCBlocker dcBlockerR;
  BiquadFilter inputHPFR;
  BiquadFilter inputLPFR;

  // Feedback-specific light physical tape chain
  BiquadFilter feedbackHeadBump;
  BiquadFilter feedbackHeadBumpR;
  BiquadFilter feedbackGapLoss;
  BiquadFilter feedbackGapLossR;
  BiquadFilter feedbackHPF;       // Remove mud accumulation
  BiquadFilter feedbackHPFR;
  AllpassFilter feedbackPhase;    // Phase smearing for vintage character
  AllpassFilter feedbackPhaseR;
  TapeMagnetics feedbackTapeSaturation;
  TapeMagnetics feedbackTapeSaturationR;
  
  // === NEW EFFECT MODE FILTERS ===
  // Spring Reverb (6-stage allpass cascade with damping)
  DelayAllpass springAP_L[6];
  DelayAllpass springAP_R[6];
  BiquadFilter springLPF_L[6];   // Tape-like damping per stage
  BiquadFilter springLPF_R[6];
  
  // Reverse Reverb smearing (4-stage)
  DelayAllpass reverseAP_L[4];
  DelayAllpass reverseAP_R[4];
  
  // Freeze state
  float freezeFade;    // Crossfade: 0=normal, 1=frozen
  int32_t freezeHead;  // Frozen read position

  // Runaway Protection
  float delayEnableRamp;
  float smoothedDelaySamples;
  float smoothedAzCoeff;
  float springFB_L;
  float springFB_R;

  float *delayLine;
  float *delayLineR;
  int32_t bufferSize;
  int32_t delayBufferCapacity;
  int32_t writeHead;
  bool usesSPIRAM;

  // Soft Knee Compressor (1.5:1 ratio) - The "Glue"
  AUDIO_INLINE float feedbackCompressor(float x) {
    const float t = 0.6f, r = 1.5f, knee = 0.2f;
    float a = fabsf(x);
    if (a <= t - knee * 0.5f) return x;
    if (a >= t + knee * 0.5f) return copysignf(t + (a - t) / r, x);
    float kx = (a - (t - knee * 0.5f)) / knee;
    float ratio = 1.0f + (1.0f / r - 1.0f) * kx * kx;
    return copysignf(a * ratio, x);
  }

  // Soft Knee Output Limiter - Prevents digital clipping
  AUDIO_INLINE float outputLimiter(float x) {
    // Soft knee at ±0.9f, hard limit at ±1.0f
    if (x > 0.9f) {
      float excess = x - 0.9f;
      x = 0.9f + excess * 0.1f;  // 10:1 compression above 0.9
    } else if (x < -0.9f) {
      float excess = x + 0.9f;
      x = -0.9f + excess * 0.1f;
    }
    // Final brickwall
    if (x > 0.99f) return 0.99f;
    if (x < -0.99f) return -0.99f;
    return x;
  }

  AUDIO_INLINE float fastSin(float x) { return sinf(x); }
  AUDIO_INLINE float hermite4(float ym1, float y0, float y1, float y2, float f);
  AUDIO_INLINE void mirrorDelayGuard(float *buffer);
  AUDIO_INLINE float mechanicalMod(float scrape, BiquadFilter &flutterFilter);
  AUDIO_INLINE float readTapeAt(float delaySamples, float *buffer);
  AUDIO_INLINE float readTapeReverse(float delaySamples, float *buffer);

public:
  TapeModel(float fs, float maxDelayTimeMs = 2000.0f);
  ~TapeModel();

  void updateFilters();
  void updateParams(const TapeParams &newParams);

  // Mono process (legacy support, uses Left channel)
  float process(float input);

  // Stereo process
  void processStereo(float inL, float inR, float *outL, float *outR);
};

// ============================================================================
// FRIPPERTRONICS / ENO PARAMETERS
// ============================================================================
struct FrippParams {
  float delayTimeA;     // 1000-7000 ms (Delay A)
  float delayTimeB;     // 1000-11000 ms (Delay B, longer for polyrhythm)
  float feedbackA;      // 0-100 (self-feedback of Delay A)
  float feedbackB;      // 0-100 (self-feedback of Delay B)
  float crossFeedback;  // 0-100 (A→B and B→A cross-feedback)
  float inputLevel;     // 0-100 (how much input is recorded)
  float outputMix;      // 0-100 (output level vs input)
  float driftAmount;    // 0-100 (pitch modulation per loop - "shimmer")
  float decayRate;      // 0-100 (how fast old layers fade - 100=infinite)
  bool enoMode;         // false=Fripp (manual), true=Eno (generative)
  bool recording;       // Fripp mode: manual record toggle
  bool clearRequested;  // Request to fade out and clear buffers
};

// ============================================================================
// FRIPPERTRONICS ENGINE (Dual Long Delay with Cross-Feedback)
// ============================================================================
class FrippEngine {
private:
  float sampleRate;
  FrippParams params;
  
  // Dual delay buffers (PSRAM)
  float *delayBufferA;
  float *delayBufferB;
  int32_t bufferSizeA;     // 7 seconds @ 44.1kHz = 308,700 samples
  int32_t bufferSizeB;     // 11 seconds @ 44.1kHz = 485,100 samples
  int32_t writeHeadA;
  int32_t writeHeadB;
  
  // Filters
  BiquadFilter inputLPF;     // Gentle input filtering
  BiquadFilter inputLPF_R;
  BiquadFilter feedbackLPF_A;   // Tape-like darkening per loop
  BiquadFilter feedbackLPF_B;
  DCBlocker dcA, dcB;
  
  // Modulation for "shimmer" effect
  float driftPhaseA, driftPhaseB;
  
  // Envelope follower for Eno mode
  float envelope;
  
  // Crossfade for clear operation
  float clearFade;
  
  // Hermite interpolation for smooth reading
  AUDIO_INLINE float readHermite(float *buffer, int32_t size, int32_t writeHead, float delaySamples);
  
  // Soft saturation (tape-like per loop)
  AUDIO_INLINE float saturate(float x) {
    return x - (0.15f * x * x * x);
  }

public:
  FrippEngine(float fs);
  ~FrippEngine();
  
  void updateParams(const FrippParams &newParams);
  
  // Main stereo process
  void processStereo(float inL, float inR, float *outL, float *outR);
  
  // Control
  void setRecording(bool rec) { params.recording = rec; }
  bool isRecording() const { return params.recording; }
  void requestClear() { params.clearRequested = true; }
  
  // Status
  bool isAllocated() const { return delayBufferA != nullptr && delayBufferB != nullptr; }
};

// ============================================================================
// BUBBLES PARAMS - Reverse Delay with "Colorful Artifacts"
// ============================================================================
struct BubblesParams {
  float bpm = 80.0f;          // BPM for delay time (slow = more artifacts)
  float feedback = 50.0f;     // 0-100 feedback (creates loops)
  float mix = 60.0f;          // 0-100 wet mix
  float feedbackLPF = 2000.0f; // Feedback darkening frequency (Hz)
  bool allpassEnabled = true;  // Toggle 4-stage allpass smearing
};

// ============================================================================
// BUBBLES ENGINE - Reverse Delay with Artifacts
// Replicates the "buggy" behavior that creates colorful bubble sounds
// ============================================================================
class BubblesEngine {
private:
  float sampleRate;
  BubblesParams params;
  
  // Delay buffers (allocated in PSRAM)
  float *delayBufferL;
  float *delayBufferR;
  int32_t bufferSize;
  int32_t writeHead;
  int32_t delaySamples;
  
  // Allpass filters for smearing (4 stages)
  float allpassZ_L[4];
  float allpassZ_R[4];
  float allpassCoeffs[4];
  
  // Feedback filters
  BiquadFilter feedbackLPF_L;
  BiquadFilter feedbackLPF_R;
  DCBlocker dcL, dcR;
  
  // The "buggy" read that creates artifacts
  AUDIO_INLINE float readReverse(float *buffer, int32_t size, int32_t wHead, int32_t delaySamps);
  
  // Allpass process (single stage)
  AUDIO_INLINE float processAllpass(float input, float &z, float coeff) {
    float output = coeff * input + z;
    z = input - coeff * output;
    return output;
  }

public:
  BubblesEngine(float fs);
  ~BubblesEngine();
  
  void updateParams(const BubblesParams &newParams);
  void processStereo(float inL, float inR, float *outL, float *outR);
  
  bool isAllocated() const { return delayBufferL != nullptr && delayBufferR != nullptr; }
};

// ============================================================================
// FREEVERB ENGINE - Schroeder-Moorer Reverb
// 8 parallel lowpass comb filters + 4 series allpass filters
// Based on public domain Freeverb by Jezar at Dreampoint
// ============================================================================

// ============================================================================
// FREEVERB ENGINE - STANDARD IMPLEMENTATION
// Based on public domain Freeverb by Jezar at Dreampoint
// Reimplemented for ESP32S3 SPIRAM
// ============================================================================

class Comb {
private:
  float* buffer;
  int bufsize;
  int bufidx;
  float feedback;
  float filterstore;
  float damp1;
  float damp2;

public:
  Comb() : buffer(nullptr), bufsize(0), bufidx(0), feedback(0), filterstore(0), damp1(0), damp2(0) {}
  ~Comb() { if(buffer) heap_caps_free(buffer); }

  void setbuffer(float* buf, int size) {
    buffer = buf;
    bufsize = size; // Original uses size
    bufidx = 0;
  }

  void mute() {
    filterstore = 0;
    if (buffer) memset(buffer, 0, bufsize * sizeof(float));
  }

  void setdamp(float val) {
    damp1 = val;
    damp2 = 1.0f - val;
  }

  void setfeedback(float val) {
    feedback = val;
  }

  AUDIO_INLINE float process(float input) {
    if (!buffer) return 0.0f;
    
    float output = buffer[bufidx];
    
    filterstore = (output * damp2) + (filterstore * damp1);
    
    // Anti-denormal
    if(fabsf(filterstore) < 1e-20f) filterstore = 0.0f;
    
    buffer[bufidx] = input + (filterstore * feedback);
    
    if(++bufidx >= bufsize) bufidx = 0;
    
    return output;
  }
};

class Allpass {
private:
  float* buffer;
  int bufsize;
  int bufidx;
  float feedback;

public:
  Allpass() : buffer(nullptr), bufsize(0), bufidx(0), feedback(0) {}
  ~Allpass() { if(buffer) heap_caps_free(buffer); }

  void setbuffer(float* buf, int size) {
    buffer = buf;
    bufsize = size;
    bufidx = 0;
  }

  void mute() {
    if (buffer) memset(buffer, 0, bufsize * sizeof(float));
  }

  void setfeedback(float val) {
    feedback = val;
  }

  AUDIO_INLINE float process(float input) {
    if (!buffer) return input;
    
    float bufout = buffer[bufidx];
    float output = -input + bufout;
    
    // buffer = input + (bufout * feedback)
    float newBuf = input + (bufout * feedback);
    
    // Anti-denormal
    if(fabsf(newBuf) < 1e-20f) newBuf = 0.0f;
    
    buffer[bufidx] = newBuf;
    
    if(++bufidx >= bufsize) bufidx = 0;
    
    return output;
  }
};

struct FreeverbParams {
    float roomSize;
    float damping;
    float wet;
    float dry;
    float width;
    bool enabled;
};

class FreeverbEngine {
private:
    static const int numcombs = 8;
    static const int numallpasses = 4;
    static constexpr int STEREO_SPREAD = 23; // Original Freeverb spread
    
    // Comb filter delay times (samples at 44.1kHz)
    static constexpr int combTuningsL[8] = {1557, 1617, 1491, 1422, 1277, 1356, 1188, 1116};
    // Allpass filter delay times
    static constexpr int allpassTuningsL[4] = {556, 441, 341, 225};

    // Fixed gain for mixing comb outputs (standard Freeverb is 0.015)
    static constexpr float fixedGain = 0.015f;
    
    // Scalar constants for parameter mapping (Standard Freeverb)
    static constexpr float scaleRoom = 0.28f;
    static constexpr float offsetRoom = 0.7f;
    static constexpr float scaleDamp = 0.4f;
    static constexpr float scaleWet = 1.0f;
    static constexpr float scaleDry = 1.0f;

    float sampleRate;
    float sampleRateRatio; // For scaling delay times
    
    Comb combL[numcombs];
    Comb combR[numcombs];
    Allpass allpassL[numallpasses];
    Allpass allpassR[numallpasses];
    
    // Pointers to buffers for manual allocation logic
    float* combLBuf[numcombs];
    float* combRBuf[numcombs];
    float* allpassLBuf[numallpasses];
    float* allpassRBuf[numallpasses];
    
    float gain;
    float roomsize, roomsize1;
    float damp, damp1;
    float wet, wet1, wet2;
    float dry;
    float width;
    
    bool allocated;

    void update();

public:
    FreeverbEngine(float fs);
    ~FreeverbEngine();
    
    void mute();
    void processStereo(float inL, float inR, float* outL, float* outR);
    
    void setRoomSize(float value);
    void setDamping(float value);
    void setWet(float value);
    void setDry(float value);
    void setWidth(float value);
    
    void updateParams(const FreeverbParams& params);
    bool isAllocated() const { return allocated; }
};



#endif // TAPE_DELAY_IMPROVED_H
