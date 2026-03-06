// ============================================================================
// BUBBLES ENGINE IMPLEMENTATION
// Reverse Delay with "Colorful Artifacts" - Chase Bliss style
// ============================================================================

#include "TapeDelay.h"

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================
BubblesEngine::BubblesEngine(float fs) : sampleRate(fs) {
  // Calculate buffer size for ~1.5 seconds max delay (at 60 BPM = 1 beat)
  // At 60 BPM: 1s delay, at 40 BPM: 1.5s delay
  int32_t maxDelaySamples = (int32_t)(fs * 1.5f);
  bufferSize = maxDelaySamples + 1000; // Headroom
  
  writeHead = 0;
  delaySamples = (int32_t)(fs * 0.75f); // Default ~750ms (80 BPM)
  
  // Initialize allpass coefficients (the magic numbers from .bak)
  allpassCoeffs[0] = 0.6f;
  allpassCoeffs[1] = 0.55f;
  allpassCoeffs[2] = 0.5f;
  allpassCoeffs[3] = 0.45f;
  
  // Clear allpass state
  for (int i = 0; i < 4; i++) {
    allpassZ_L[i] = 0.0f;
    allpassZ_R[i] = 0.0f;
  }
  
  // Allocate PSRAM buffers
  size_t bytesNeeded = bufferSize * sizeof(float);
  Serial.printf("BubblesEngine: Allocating %.1f MB per channel\n", bytesNeeded / 1048576.0f);
  
  delayBufferL = (float *)heap_caps_malloc(bytesNeeded, MALLOC_CAP_SPIRAM);
  delayBufferR = (float *)heap_caps_malloc(bytesNeeded, MALLOC_CAP_SPIRAM);
  
  if (delayBufferL && delayBufferR) {
    memset(delayBufferL, 0, bytesNeeded);
    memset(delayBufferR, 0, bytesNeeded);
    Serial.println("BubblesEngine: PSRAM allocation SUCCESS");
  } else {
    Serial.println("BubblesEngine: PSRAM allocation FAILED!");
    if (delayBufferL) heap_caps_free(delayBufferL);
    if (delayBufferR) heap_caps_free(delayBufferR);
    delayBufferL = nullptr;
    delayBufferR = nullptr;
    bufferSize = 0;
  }
  
  // Initialize filters - the feedback darkening
  feedbackLPF_L.setLowpass(fs, params.feedbackLPF, 0.5f);
  feedbackLPF_R.setLowpass(fs, params.feedbackLPF, 0.5f);
}

BubblesEngine::~BubblesEngine() {
  if (delayBufferL) heap_caps_free(delayBufferL);
  if (delayBufferR) heap_caps_free(delayBufferR);
}

// ============================================================================
// PARAMETER UPDATE
// ============================================================================
void BubblesEngine::updateParams(const BubblesParams &newParams) {
  if (!delayBufferL || !delayBufferR || bufferSize == 0) return;
  
  params = newParams;
  
  // Calculate delay samples from BPM (1 beat delay)
  float msPerBeat = 60000.0f / params.bpm;
  delaySamples = (int32_t)((msPerBeat / 1000.0f) * sampleRate);
  
  // Clamp to buffer size
  if (delaySamples >= bufferSize - 100) {
    delaySamples = bufferSize - 100;
  }
  
  // Update feedback filter
  feedbackLPF_L.setLowpass(sampleRate, params.feedbackLPF, 0.5f);
  feedbackLPF_R.setLowpass(sampleRate, params.feedbackLPF, 0.5f);
}

// ============================================================================
// THE "BUGGY" REVERSE READ - Creates the artifacts!
// ============================================================================
AUDIO_INLINE float BubblesEngine::readReverse(float *buffer, int32_t size, int32_t wHead, int32_t delaySamps) {
  // Safety check
  if (size <= 0 || buffer == nullptr) return 0.0f;
  
  // ANOMALY #1: Calculate read position by ADDING instead of subtracting
  int32_t readPos = wHead + delaySamps;
  
  // Wrap around using modulo (safer than while loop)
  readPos = readPos % size;
  if (readPos < 0) readPos += size;
  
  // ANOMALY #2: Double inversion - subtract from size
  int32_t finalPos = size - 1 - readPos;  // -1 to stay in bounds
  
  // Clamp to valid range (extra safety)
  if (finalPos < 0) finalPos = 0;
  if (finalPos >= size) finalPos = size - 1;
  
  // Read without interpolation (more artifacts!)
  return buffer[finalPos];
}

// ============================================================================
// MAIN STEREO PROCESS
// ============================================================================
void BubblesEngine::processStereo(float inL, float inR, float *outL, float *outR) {
  // Safety: check all buffer state
  if (!delayBufferL || !delayBufferR || bufferSize <= 0) {
    *outL = inL;
    *outR = inR;
    return;
  }
  
  // Convert params
  float feedback = params.feedback * 0.01f;  // 0-100 -> 0-1
  float wetMix = params.mix * 0.01f;         // 0-100 -> 0-1
  
  // === READ FROM BUFFER (The "reverse" read with artifacts) ===
  float wetL = readReverse(delayBufferL, bufferSize, writeHead, delaySamples);
  float wetR = readReverse(delayBufferR, bufferSize, writeHead, delaySamples);
  
  // === ALLPASS SMEARING (if enabled) ===
  if (params.allpassEnabled) {
    for (int s = 0; s < 4; s++) {
      wetL = processAllpass(wetL, allpassZ_L[s], allpassCoeffs[s]);
      wetR = processAllpass(wetR, allpassZ_R[s], allpassCoeffs[s]);
    }
  }
  
  // === FEEDBACK PATH ===
  float fbL = feedbackLPF_L.process(wetL) * feedback;
  float fbR = feedbackLPF_R.process(wetR) * feedback;
  
  // === WRITE TO BUFFER ===
  // ANOMALY #4: Feedback goes IN while reading reversed = time loops!
  delayBufferL[writeHead] = dcL.process(inL + fbL);
  delayBufferR[writeHead] = dcR.process(inR + fbR);
  
  // Advance write head (linear)
  writeHead++;
  if (writeHead >= bufferSize) writeHead = 0;
  
  // === MIX OUTPUT ===
  *outL = (inL * (1.0f - wetMix) + wetL * wetMix);
  *outR = (inR * (1.0f - wetMix) + wetR * wetMix);
  
  // Soft limit
  if (*outL > 0.95f) *outL = 0.95f;
  if (*outL < -0.95f) *outL = -0.95f;
  if (*outR > 0.95f) *outR = 0.95f;
  if (*outR < -0.95f) *outR = -0.95f;
}
