#ifndef TAPE_DELAY_IMPROVED_H
#define TAPE_DELAY_IMPROVED_H

#include "esp_heap_caps.h"
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
  TapeNoiseGenerator(float fs) {
    state[0] = state[1] = state[2] = 0;
    seed = 123456789 + micros();
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
// DROPOUT GENERATOR
// ============================================================================
class DropoutGenerator {
private:
  float smoothedLevel;
  float targetLevel;
  int samplesUntilNext;
  int dropoutDuration;
  float severity;
  uint32_t seed;

  AUDIO_INLINE uint32_t fast_rand() {
    seed = seed * 1664525u + 1013904223u;
    return seed;
  }

public:
  DropoutGenerator()
      : smoothedLevel(1.0f), targetLevel(1.0f), samplesUntilNext(0),
        dropoutDuration(0), severity(0.5f), seed(987654321) {}

  void setSeverity(float sev) { severity = constrain(sev, 0.0f, 1.0f); }

  AUDIO_INLINE float process() {
    if (samplesUntilNext <= 0) {
      if (dropoutDuration <= 0) {
        float chance =
            severity * 0.0005f; // Ajustado para ser probabilidade por sample
        // Usando lógica inteira rápida para chance
        if ((fast_rand() & 0xFFFF) < (chance * 65535.0f)) {
          dropoutDuration = 100 + (fast_rand() % 2000);
          targetLevel = 0.1f + ((fast_rand() & 0xFF) / 255.0f) * 0.4f;
          samplesUntilNext = dropoutDuration;
        } else {
          targetLevel = 1.0f;
          samplesUntilNext = 1000 + (fast_rand() % 5000); // Check again soon
        }
      } else {
        dropoutDuration--;
        samplesUntilNext = 1;
      }
    }
    samplesUntilNext--;

    float smoothCoeff = (targetLevel < smoothedLevel)
                            ? 0.0005f
                            : 0.002f; // Ataque rápido, release lento
    smoothedLevel += smoothCoeff * (targetLevel - smoothedLevel);

    return smoothedLevel;
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

// ============================================================================
// TAPE MODEL
// ============================================================================
class TapeModel {
private:
  float sampleRate;
  TapeParams currentParams;

  float flutterPhase, wowPhase;
  float azimuthPhase;
  BiquadFilter flutterLPF;

  DropoutGenerator dropout;
  TapeNoiseGenerator noiseGen;
  BiquadFilter headBump;
  BiquadFilter tapeRolloff;
  BiquadFilter outputLPF;
  AllpassFilter azimuthFilter;
  DCBlocker dcBlocker;

  // Guitar Focus Filters (Input)
  BiquadFilter inputHPF;
  BiquadFilter inputLPF;

  // Right Channel Filters
  BiquadFilter headBumpR;
  BiquadFilter tapeRolloffR;
  BiquadFilter outputLPFR;
  AllpassFilter azimuthFilterR;
  DCBlocker dcBlockerR;
  BiquadFilter inputHPFR;
  BiquadFilter inputLPFR;

  // Feedback specific filters
  BiquadFilter feedbackLPF;
  BiquadFilter feedbackLPFR;
  BiquadFilter feedbackHPF;     // Remove mud accumulation
  BiquadFilter feedbackHPFR;
  AllpassFilter feedbackAllpass;  // Phase smearing for vintage character
  AllpassFilter feedbackAllpassR;
  
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

  float *delayLine;
  float *delayLineR;
  int32_t bufferSize;
  int32_t writeHead;
  bool usesSPIRAM;

  // Tube-like Asymmetric Saturation
  AUDIO_INLINE float saturator(float x) {
    // 1. Asymmetry (Tube Bias) - Adds even harmonics
    // Positive swings get slightly compressed earlier than negative
    if (x > 0.5f)
      x = 0.5f + (x - 0.5f) * 0.8f;

    // 2. Soft Clipping (Tapered)
    if (x > 1.5f)
      return 1.0f;
    if (x < -1.5f)
      return -1.0f;

    // Smooth cubic clipper
    return x - (0.1f * x * x * x);
  }

  // Soft Knee Compressor (1.5:1 ratio) - The "Glue"
  AUDIO_INLINE float feedbackCompressor(float x) {
    const float thresh = 0.6f;
    const float ratio = 1.5f;

    float a = fabsf(x);
    if (a <= thresh)
      return x;

    float excess = a - thresh;
    float compressed = thresh + excess / ratio;

    return copysignf(compressed, x);
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
