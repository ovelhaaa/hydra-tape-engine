// ============================================================================
// FRIPPERTRONICS ENGINE IMPLEMENTATION
// Dual Long Delay with Cross-Feedback for Fripp/Eno Looping
// ============================================================================

#include "TapeDelay.h"

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================
FrippEngine::FrippEngine(float fs) : sampleRate(fs) {
  // Calculate buffer sizes for 7s and 11s max delays
  bufferSizeA = (int32_t)(fs * 7.0f);   // 7 seconds
  bufferSizeB = (int32_t)(fs * 11.0f);  // 11 seconds
  
  writeHeadA = 0;
  writeHeadB = 0;
  driftPhaseA = 0.0f;
  driftPhaseB = 0.0f;
  envelope = 0.0f;
  clearFade = 1.0f;
  
  // Attempt PSRAM allocation
  size_t bytesA = bufferSizeA * sizeof(float);
  size_t bytesB = bufferSizeB * sizeof(float);
  
  Serial.printf("FrippEngine: Allocating %.1f MB for Delay A (7s)\n", bytesA / 1048576.0f);
  Serial.printf("FrippEngine: Allocating %.1f MB for Delay B (11s)\n", bytesB / 1048576.0f);
  
  delayBufferA = (float *)heap_caps_malloc(bytesA, MALLOC_CAP_SPIRAM);
  delayBufferB = (float *)heap_caps_malloc(bytesB, MALLOC_CAP_SPIRAM);
  
  if (delayBufferA && delayBufferB) {
    memset(delayBufferA, 0, bytesA);
    memset(delayBufferB, 0, bytesB);
    Serial.println("FrippEngine: PSRAM allocation SUCCESS");
  } else {
    Serial.println("FrippEngine: PSRAM allocation FAILED!");
    if (delayBufferA) heap_caps_free(delayBufferA);
    if (delayBufferB) heap_caps_free(delayBufferB);
    delayBufferA = nullptr;
    delayBufferB = nullptr;
    bufferSizeA = 0;
    bufferSizeB = 0;
  }
  
  // Initialize filters
  // Gentle input lowpass (remove harsh highs)
  inputLPF.setLowpass(fs, 8000.0f, 0.707f);
  inputLPF_R.setLowpass(fs, 8000.0f, 0.707f);
  
  // Feedback darkening (tape-like high loss per loop)
  feedbackLPF_A.setLowpass(fs, 6000.0f, 0.5f);
  feedbackLPF_B.setLowpass(fs, 5500.0f, 0.5f);  // Slightly different for character
  
  // Default params
  params.delayTimeA = 5000.0f;   // 5 seconds default
  params.delayTimeB = 7500.0f;   // 7.5 seconds default (different for polyrhythm)
  params.feedbackA = 85.0f;
  params.feedbackB = 85.0f;
  params.crossFeedback = 30.0f;  // Moderate cross-feedback
  params.inputLevel = 80.0f;
  params.outputMix = 70.0f;
  params.driftAmount = 10.0f;    // Subtle pitch drift
  params.decayRate = 95.0f;      // Long decay
  params.enoMode = false;
  params.recording = false;
  params.clearRequested = false;
}

FrippEngine::~FrippEngine() {
  if (delayBufferA) heap_caps_free(delayBufferA);
  if (delayBufferB) heap_caps_free(delayBufferB);
}

// ============================================================================
// PARAMETER UPDATE
// ============================================================================
void FrippEngine::updateParams(const FrippParams &newParams) {
  // Safety check - don't update if not properly allocated
  if (!delayBufferA || !delayBufferB || bufferSizeA <= 0 || bufferSizeB <= 0) {
    return;
  }
  
  params = newParams;
  
  // Clamp values to safe ranges to prevent NaN/Inf
  if (params.decayRate < 0.0f) params.decayRate = 0.0f;
  if (params.decayRate > 100.0f) params.decayRate = 100.0f;
  
  // Update feedback filters based on decay rate
  // Higher decay = less darkening per loop
  float darkening = 4000.0f + (params.decayRate * 0.01f) * 6000.0f;  // 4kHz - 10kHz
  feedbackLPF_A.setLowpass(sampleRate, darkening, 0.5f);
  feedbackLPF_B.setLowpass(sampleRate, darkening * 0.9f, 0.5f);
}

// ============================================================================
// HERMITE INTERPOLATION (for smooth pitch modulation)
// ============================================================================
AUDIO_INLINE float FrippEngine::readHermite(float *buffer, int32_t size, int32_t wHead, float delaySamples) {
  // Safety checks
  if (!buffer || size <= 0) return 0.0f;
  
  if (delaySamples < 2.0f) delaySamples = 2.0f;
  if (delaySamples > size - 4.0f) delaySamples = (float)size - 4.0f;
  
  float readPos = (float)wHead - delaySamples;
  
  // Wrap using modulo (safer than while loop)
  int32_t readPosInt = (int32_t)readPos;
  readPosInt = readPosInt % size;
  if (readPosInt < 0) readPosInt += size;
  readPos = (float)readPosInt + (readPos - floorf(readPos));
  
  int32_t r = (int32_t)readPos;
  if (r < 0) r = 0;
  if (r >= size) r = size - 1;
  float f = readPos - (float)r;
  
  int32_t i1 = r;
  int32_t i2 = (r > 0) ? r - 1 : size - 1;
  int32_t i0 = (r < size - 1) ? r + 1 : 0;
  int32_t i3 = (i0 < size - 1) ? i0 + 1 : 0;
  
  float d1 = buffer[i1];
  float d0 = buffer[i0];
  float d2 = buffer[i2];
  float d3 = buffer[i3];
  
  // Hermite coefficients
  float c0 = d1;
  float c1 = 0.5f * (d0 - d2);
  float c2 = d2 - 2.5f * d1 + 2.0f * d0 - 0.5f * d3;
  float c3 = 0.5f * (d3 - d1) + 1.5f * (d1 - d0);
  
  return ((c3 * f + c2) * f + c1) * f + c0;
}

// ============================================================================
// MAIN STEREO PROCESS
// ============================================================================
void FrippEngine::processStereo(float inL, float inR, float *outL, float *outR) {
  // Safety: check all buffer state
  if (!delayBufferA || !delayBufferB || bufferSizeA <= 0 || bufferSizeB <= 0) {
    *outL = inL;
    *outR = inR;
    return;
  }
  
  // Convert params to internal scale
  float fbA = params.feedbackA * 0.01f;
  float fbB = params.feedbackB * 0.01f;
  float crossFB = params.crossFeedback * 0.01f;
  float inputLvl = params.inputLevel * 0.01f;
  float outputMix = params.outputMix * 0.01f;
  float drift = params.driftAmount * 0.01f;
  
  // Calculate delay times in samples
  float delaySamplesA = (params.delayTimeA / 1000.0f) * sampleRate;
  float delaySamplesB = (params.delayTimeB / 1000.0f) * sampleRate;
  
  // Clamp to buffer sizes
  if (delaySamplesA > bufferSizeA - 10) delaySamplesA = bufferSizeA - 10;
  if (delaySamplesB > bufferSizeB - 10) delaySamplesB = bufferSizeB - 10;
  
  // === PITCH DRIFT MODULATION (Shimmer) ===
  driftPhaseA += 0.07f / sampleRate;  // ~0.07 Hz slow drift
  driftPhaseB += 0.11f / sampleRate;  // ~0.11 Hz (different for organic feel)
  if (driftPhaseA > 1.0f) driftPhaseA -= 1.0f;
  if (driftPhaseB > 1.0f) driftPhaseB -= 1.0f;
  
  float driftModA = sinf(driftPhaseA * 6.28318f) * drift * 50.0f;  // ±50 samples max
  float driftModB = sinf(driftPhaseB * 6.28318f) * drift * 50.0f;
  
  delaySamplesA += driftModA;
  delaySamplesB += driftModB;
  
  // === READ FROM DELAY BUFFERS ===
  float readA = readHermite(delayBufferA, bufferSizeA, writeHeadA, delaySamplesA);
  float readB = readHermite(delayBufferB, bufferSizeB, writeHeadB, delaySamplesB);
  
  // Apply feedback filters (tape darkening per loop)
  readA = feedbackLPF_A.process(readA);
  readB = feedbackLPF_B.process(readB);
  
  // Soft saturation per loop (adds warmth, prevents runaway)
  readA = saturate(readA);
  readB = saturate(readB);
  
  // === ENVELOPE FOLLOWER FOR ENO MODE ===
  float monoIn = (inL + inR) * 0.5f;
  float absIn = fabsf(monoIn);
  if (absIn > envelope) {
    envelope += 0.001f * (absIn - envelope);  // Fast attack
  } else {
    envelope += 0.00005f * (absIn - envelope);  // Slow release
  }
  
  // === DETERMINE INPUT LEVEL ===
  float effectiveInput = 0.0f;
  
  if (params.enoMode) {
    // ENO MODE: Continuous subtle recording based on input level
    // Input is blended based on envelope - more input = more impact
    float enoBlend = 0.1f + envelope * 0.5f;  // 10% base + 50% dynamic
    enoBlend *= inputLvl;
    effectiveInput = monoIn * enoBlend;
  } else {
    // FRIPP MODE: Manual recording toggle
    if (params.recording) {
      effectiveInput = monoIn * inputLvl;
    }
  }
  
  // Gentle input filtering
  float filteredInput = inputLPF.process(effectiveInput);
  
  // === CROSS-FEEDBACK MIXING ===
  // A receives: input + self-feedback + cross from B
  // B receives: input + self-feedback + cross from A
  float writeA = filteredInput + (readA * fbA) + (readB * crossFB);
  float writeB = filteredInput + (readB * fbB) + (readA * crossFB);
  
  // DC blocking
  writeA = dcA.process(writeA);
  writeB = dcB.process(writeB);
  
  // === CLEAR HANDLING ===
  if (params.clearRequested) {
    clearFade -= 0.0001f;  // ~2 second fade
    if (clearFade <= 0.0f) {
      clearFade = 0.0f;
      // Clear buffers
      memset(delayBufferA, 0, bufferSizeA * sizeof(float));
      memset(delayBufferB, 0, bufferSizeB * sizeof(float));
      params.clearRequested = false;
      clearFade = 1.0f;
    }
    writeA *= clearFade;
    writeB *= clearFade;
    readA *= clearFade;
    readB *= clearFade;
  }
  
  // === WRITE TO BUFFERS ===
  // Limit to prevent buffer explosion
  if (writeA > 2.0f) writeA = 2.0f;
  if (writeA < -2.0f) writeA = -2.0f;
  if (writeB > 2.0f) writeB = 2.0f;
  if (writeB < -2.0f) writeB = -2.0f;
  
  delayBufferA[writeHeadA] = writeA;
  delayBufferB[writeHeadB] = writeB;
  
  // Advance write heads
  writeHeadA++;
  writeHeadB++;
  if (writeHeadA >= bufferSizeA) writeHeadA = 0;
  if (writeHeadB >= bufferSizeB) writeHeadB = 0;
  
  // === STEREO OUTPUT WITH INTERLEAVING ===
  // Delay A panned slightly left, Delay B panned slightly right
  // This creates a wide, enveloping stereo image
  float panA_L = 0.8f;   // A: 80% left, 30% right
  float panA_R = 0.3f;
  float panB_L = 0.3f;   // B: 30% left, 80% right
  float panB_R = 0.8f;
  
  float wetL = (readA * panA_L) + (readB * panB_L);
  float wetR = (readA * panA_R) + (readB * panB_R);
  
  // Normalize (sum of pans = 1.1, so divide to prevent boost)
  wetL *= 0.9f;
  wetR *= 0.9f;
  
  // Mix with dry input
  *outL = (inL * (1.0f - outputMix)) + (wetL * outputMix);
  *outR = (inR * (1.0f - outputMix)) + (wetR * outputMix);
  
  // Soft limit output
  if (*outL > 0.95f) *outL = 0.95f;
  if (*outL < -0.95f) *outL = -0.95f;
  if (*outR > 0.95f) *outR = 0.95f;
  if (*outR < -0.95f) *outR = -0.95f;
}
