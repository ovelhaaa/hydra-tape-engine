#include "MelodyGen.h"

// Removed LUT static ref

MelodyGen::MelodyGen(float fs) : sampleRate(fs) {
  // initLUT(); // Removed

  // Init Recursive Oscillator
  oscSin = 0.0f;
  oscCos = 1.0f;
  sinInc = 0.0f;
  cosInc = 1.0f;

  uniSin = 0.0f;
  uniCos = 1.0f;
  uniSinInc = 0.0f;
  uniCosInc = 1.0f;

  tri = 0.0f;
  triInc = 0.0f;
  saw = 0.0f;
  normCounter = 0;

  frequency = 440.0;
  targetFrequency = 440.0;
  timer = 0;
  currentWaveform = SINE;
  mode = MODE_NORMAL;

  // Padrões
  currentScale = MINOR;
  rootKey = 4; // E
  bpm = 80.0;
  mood = 0.5;
  rhythmDensity = 0.65;

  currentDurationSamples = 12000;
  currentScaleDegree = 0;
  currentOctave = 4;

  frequencySmoothing = 0.998f;

  // --- UPGRADE V1-V3 INIT ---
  motifIndex = 0;
  rhythmIndex = 0;
  phraseCounter = 0;
  currentAccent = 1.0f;
  motif[0] = 0;
  motif[1] = 2;
  motif[2] = 0;
  motif[3] = -1;
  rhythmPattern[0] = 1.0f;
  rhythmPattern[1] = 0.5f;
  rhythmPattern[2] = 0.5f;
  rhythmPattern[3] = 1.0f;

  // --- UPGRADE V4: ORGANIC INIT ---
  detune = 0.0f;
  timbre = 0.0f;
  timbreTarget = 0.0f;
  tone = 0.4f;
  toneState = 0.0f;
  drift = 1.0f;

  // --- UPGRADE V5: STEREO & ENVELOPE ---
  env = 0.0f;
  envTarget = 0.0f;
  envAttack = 0.005f;
  envRelease = 0.001f;
  stereoDrift = 0.0f;
  noteOn = true;
  isResting = false;

  // Force first note to be valid
  pickNextNote();
  isResting = false; // Override random rest for startup
  envTarget = 1.0f;
}

void MelodyGen::setWaveform(Waveform wave) { currentWaveform = wave; }

void MelodyGen::setScale(ScaleType scale) { currentScale = scale; }
void MelodyGen::setKey(int midiRoot) { rootKey = midiRoot % 12; }
void MelodyGen::setBPM(float newBpm) { bpm = (newBpm < 30) ? 30 : newBpm; }
void MelodyGen::setMood(float m) { mood = constrain(m, 0.0f, 1.0f); }
void MelodyGen::setRhythm(float r) { rhythmDensity = constrain(r, 0.0f, 1.0f); }

float MelodyGen::mtof(int midiNote) {
  return 440.0f * powf(2.0f, (midiNote - 69) / 12.0f);
}

int MelodyGen::getInterval(ScaleType scale, int degree) {
  // Static lookup tables - no switch, no memcpy
  static const int8_t MAJOR_SCALE[]    = {0, 2, 4, 5, 7, 9, 11};
  static const int8_t MINOR_SCALE[]    = {0, 2, 3, 5, 7, 8, 10};
  static const int8_t PENTA_SCALE[]    = {0, 3, 5, 7, 10, 0, 0};  // 5 notes, padded
  static const int8_t BLUES_SCALE[]    = {0, 3, 5, 6, 7, 10, 0};  // 6 notes, padded
  static const int8_t CHROMATIC_SCALE[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
  
  static const int8_t* SCALE_TABLES[] = {
    CHROMATIC_SCALE, MAJOR_SCALE, MINOR_SCALE, PENTA_SCALE, BLUES_SCALE
  };
  static const int SCALE_SIZES[] = {12, 7, 7, 5, 6};
  
  int scaleIdx = (int)scale;
  if (scaleIdx < 0 || scaleIdx > 4) scaleIdx = 1;  // Default to major
  
  const int8_t* intervals = SCALE_TABLES[scaleIdx];
  int count = SCALE_SIZES[scaleIdx];

  int octaveOffset = 0;
  int safeDegree = degree;

  if (degree >= 0) {
    octaveOffset = (degree / count) * 12;
    safeDegree = degree % count;
  } else {
    octaveOffset = ((degree - count + 1) / count) * 12;
    safeDegree = (degree % count);
    if (safeDegree < 0)
      safeDegree += count;
  }

  return intervals[safeDegree] + octaveOffset;
}

void MelodyGen::pickNextNote() {
  noteOn = true;
  envTarget = 1.0f;
  isResting = false; // BUGFIX: Reset rest state!

  detune = ((esp_random() % 1000) / 1000.0f - 0.5f) * 0.003f;
  
  // Removed random timbre randomization to respect UI Waveform selection
  if (currentWaveform == TRIANGLE) timbreTarget = 1.0f;
  else timbreTarget = 0.0f;

  tone = 0.2f + (mood * 0.6f);
  stereoDrift = ((esp_random() % 1000) / 1000.0f - 0.5f) * 0.3f;

  // FIXED: Deterministic Drift (Simplified for speed)
  drift = 1.0f + ((esp_random() & 0xFF) / 255.0f - 0.5f) * 0.004f;

  if (mode == MODE_ENO) {
    float samplesPerBeat = (sampleRate * 60.0f) / bpm;
    // Safety check for samplesPerBeat
    if (samplesPerBeat < 1000.0f) samplesPerBeat = 1000.0f;

    if (esp_random() % 100 > 10) {
      isResting = true;
      envTarget = 0.0f;
      currentDurationSamples = samplesPerBeat * (2 + (esp_random() % 4));
      // Enforce minimum duration
      if (currentDurationSamples < 4410) currentDurationSamples = 4410; 
      return;
    }
    isResting = false;
    currentAccent = 0.6f;
    currentDurationSamples = samplesPerBeat * (4 + (esp_random() % 4));
    if (currentDurationSamples < 8820) currentDurationSamples = 8820;

      envAttack = 0.00005f;   // Very slow attack (ambient swell)
      envRelease = 0.00002f;  // Very slow release (~2s fade for ambient tails)

    if (esp_random() % 100 < 15) {
      currentScaleDegree += (esp_random() % 2) ? 1 : -1;
    }
    if (currentScaleDegree > 3)
      currentScaleDegree = 3;
    if (currentScaleDegree < -3)
      currentScaleDegree = -3;

    currentOctave = 3 + (esp_random() % 2);

    int noteInScale = getInterval(currentScale, currentScaleDegree);
    int midiNote = rootKey + (currentOctave * 12) + noteInScale;

    targetFrequency = mtof(midiNote);
    tone *= 0.7f;

    // --- OSCILLATOR INIT (CORRECT) ---
    float pInc = 2.0f * M_PI * targetFrequency / sampleRate;
    sinInc = sinf(pInc);
    cosInc = cosf(pInc);
    oscSin = 0.0f;
    oscCos = 1.0f; // Reset phase

    float uInc = pInc * (1.0f + detune);
    uniSinInc = sinf(uInc);
    uniCosInc = cosf(uInc);
    uniSin = 0.0f;
    uniCos = 1.0f;
    return;
  }

  // === MODE NORMAL ===
  if (rhythmIndex == 0 && (phraseCounter++ % 2 == 0)) {
    currentOctave = 3;
  } else if (rhythmIndex == 0) {
    currentOctave = 4;
  }

  envAttack = (currentOctave <= 3) ? 0.002f : 0.01f;
  envRelease = 0.002f;

  float samplesPerBeat = (sampleRate * 60.0f) / bpm;

  if (rhythmIndex >= 4) {
    rhythmIndex = 0;
    if (esp_random() % 100 < 30) {
      isResting = true;
      envTarget = 0.0f;
      currentDurationSamples = (long)(samplesPerBeat * 2.0f);
      return;
    }
    int p = esp_random() % 3;
    if (p == 0) {
      rhythmPattern[0] = 1.0f;
      rhythmPattern[1] = 1.0f;
      rhythmPattern[2] = 1.0f;
      rhythmPattern[3] = 1.0f;
    } else if (p == 1) {
      rhythmPattern[0] = 1.5f;
      rhythmPattern[1] = 0.5f;
      rhythmPattern[2] = 1.0f;
      rhythmPattern[3] = 1.0f;
    } else {
      rhythmPattern[0] = 0.5f;
      rhythmPattern[1] = 0.5f;
      rhythmPattern[2] = 0.5f;
      rhythmPattern[3] = 0.5f;
    }
  }

  float beatMult = rhythmPattern[rhythmIndex++] * rhythmDensity;
  if (beatMult < 0.25f)
    beatMult = 0.25f;
  currentDurationSamples = (long)(samplesPerBeat * beatMult);

  bool isStrongBeat = (rhythmIndex == 0 || rhythmIndex == 2);
  if (!isStrongBeat && (esp_random() % 100 < 60)) {
    isResting = true;
    envTarget = 0.0f;
    return;
  }
  if ((esp_random() % 100) < 5) {
    isResting = true;
    envTarget = 0.0f;
    return;
  }

  if (motifIndex >= 4)
    motifIndex = 0;
  int step = motif[motifIndex++];

  int var = esp_random() % 100;
  if (var < 20)
    step = -step;
  else if (var > 80)
    motif[motifIndex % 4] = (esp_random() % 5) - 2;

  if (esp_random() % 100 < 30) {
    int weights[] = {40, 15, 25, 10, 25, 15, 5};
    int r = esp_random() % 135;
    int sum = 0;
    int chosenLvl = 0;
    for (int i = 0; i < 7; i++) {
      sum += weights[i];
      if (r < sum) {
        chosenLvl = i;
        break;
      }
    }
    int baseOctave = currentScaleDegree / 7;
    currentScaleDegree = (baseOctave * 7) + chosenLvl;
    step = 0;
  }

  currentScaleDegree += step;
  if (currentScaleDegree > 7)
    currentScaleDegree -= 7;
  if (currentScaleDegree < -7)
    currentScaleDegree += 7;

  currentAccent = (isStrongBeat) ? 1.0f : 0.7f;
  if (step != 0)
    currentAccent += 0.1f;
  if (currentAccent > 1.0f)
    currentAccent = 1.0f;
  if (currentAccent < 0.5f)
    currentAccent = 0.5f;

  int noteInScale = getInterval(currentScale, currentScaleDegree);
  int midiNote = rootKey + (currentOctave * 12) + noteInScale;

  if (midiNote < 36)
    midiNote = 36;
  if (midiNote > 84)
    midiNote = 84;

  targetFrequency = mtof(midiNote);

  // --- OSCILLATOR INCREMENT UPDATE ---
  // We do NOT reset phase (oscSin/oscCos) to prevent clicks (Phase Continuity)
  float pInc = 2.0f * M_PI * targetFrequency / sampleRate;
  sinInc = sinf(pInc);
  cosInc = cosf(pInc);

  float uInc = pInc * (1.0f + detune);
  uniSinInc = sinf(uInc);
  uniCosInc = cosf(uInc);
}

void MelodyGen::nextStereo(float *outL, float *outR) {
  if (++timer >= currentDurationSamples && currentDurationSamples > 0) {
    timer = 0;
    pickNextNote();
  }

  // --- ENVELOPE LOGIC (Simplified & Fixed) ---
  // Only use sequencer Resting flag. No forced timer-based release.
  if (isResting)
    envTarget = 0.0f;

  if (noteOn)
    noteOn = false;

  // Smoothing
  frequency = frequency * frequencySmoothing +
              targetFrequency * (1.0f - frequencySmoothing);

  // --- PURE RECURSIVE OSCILLATOR ---
  // No per-sample phaseInc calculation.
  // No normalization for now (user says it's destructive).

  float newSin = oscSin * cosInc + oscCos * sinInc;
  float newCos = oscCos * cosInc - oscSin * sinInc;
  oscSin = newSin;
  oscCos = newCos;

  // Unison
  float newUniSin = uniSin * uniCosInc + uniCos * uniSinInc;
  float newUniCos = uniCos * uniCosInc - uniSin * uniSinInc;
  uniSin = newUniSin;
  uniCos = newUniCos;

  // Periodic normalization to prevent amplitude drift
  if (++normCounter > 1024) {
    normCounter = 0;
    float mag = sqrtf(oscSin * oscSin + oscCos * oscCos);
    if (mag > 0.0f) {
      oscSin /= mag;
      oscCos /= mag;
    }
    float uniMag = sqrtf(uniSin * uniSin + uniCos * uniCos);
    if (uniMag > 0.0f) {
      uniSin /= uniMag;
      uniCos /= uniMag;
    }
  }

  // Triangle Integrator
  // Recalculating triInc is cheap and safe
  triInc = (frequency * drift * 4.0f) / sampleRate;

  if (oscSin >= 0)
    tri += triInc;
  else
    tri -= triInc;

  // Simple clamp for triangle
  if (tri > 1.0f)
    tri = 1.0f;
  else if (tri < -1.0f)
    tri = -1.0f;

  // ENVELOPE
  if (env < envTarget)
    env += envAttack * (envTarget - env);
  else
    env += envRelease * (envTarget - env);

  float shapedEnv = env * env;

  if (shapedEnv <= 0.0001f && envTarget == 0.0f) {
    // Keep DSP pipeline warm with logic-level noise
    *outL = 1e-6f;
    *outR = 1e-6f;
    return;
  }

  // AUDIO GENERATION
  float slew = (mode == MODE_ENO) ? 0.0001f : 0.0005f;
  timbre += slew * (timbreTarget - timbre);

  // Sawtooth Generation
  float sInc = (frequency * drift * 2.0f) / sampleRate;
  saw += sInc;
  if (saw > 1.0f) saw -= 2.0f;

  float s = 0.6f * oscSin + 0.4f * uniSin;
  float outMono = 0.0f;
  
  switch (currentWaveform) {
      case SINE: outMono = s; break;
      case TRIANGLE: outMono = tri; break;
      case SAWTOOTH: outMono = saw * 0.8f; break; // Scaled to match perceived loudness
      case SQUARE: outMono = (saw > 0.0f) ? 0.7f : -0.7f; break;
      default: outMono = s; break;
  }
  
  // Eno Mode Timbre Morph (Optional integration)
  if (mode == MODE_ENO) {
      // In Eno mode, maybe blend internal triangle? 
      // For now, stick to explicit waveform control as requested.
      // outMono = (1.0f - timbre) * s + timbre * tri; 
  }

  outMono = fastSat(outMono * 1.2f);
  toneState += tone * (outMono - toneState);
  outMono = toneState;

  float sig = outMono * shapedEnv * currentAccent * 0.6f;

  float panL = 1.0f + stereoDrift;
  float panR = 1.0f - stereoDrift;

  // Normalize Panning
  float norm = 1.0f;
  if (panL > 1.0f)
    norm = 1.0f / panL;
  if (panR > 1.0f && (1.0f / panR) < norm)
    norm = 1.0f / panR;

  *outL = sig * panL * norm;
  *outR = sig * panR * norm;
}