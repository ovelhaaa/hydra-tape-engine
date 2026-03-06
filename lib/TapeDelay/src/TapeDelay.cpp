#include "TapeDelay.h"

TapeModel::TapeModel(float fs, float maxDelayTimeMs)
    : sampleRate(fs), noiseGen(fs), flutterPhase(0), wowPhase(0),
      azimuthPhase(0), delayEnableRamp(0.0f), smoothedDelaySamples(0.0f) {
  // Safe buffer calculation
  bufferSize = (int32_t)(fs * (maxDelayTimeMs / 1000.0f));
  size_t bytes = bufferSize * sizeof(float);

  // Attempt PSRAM allocation first
  delayLine = (float *)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
  delayLineR = (float *)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
  usesSPIRAM = true;

  // Fallback to internal RAM
  if (!delayLine || !delayLineR) {
    Serial.println("WARN: PSRAM failed. Using internal RAM.");
    if (delayLine)
      heap_caps_free(delayLine);
    if (delayLineR)
      heap_caps_free(delayLineR);

    bufferSize = (int32_t)(fs * 0.4f);
    delayLine = (float *)heap_caps_malloc(bufferSize * sizeof(float),
                                          MALLOC_CAP_INTERNAL);
    delayLineR = (float *)heap_caps_malloc(bufferSize * sizeof(float),
                                           MALLOC_CAP_INTERNAL);
    usesSPIRAM = false;
  }

  if (delayLine && delayLineR) {
    memset(delayLine, 0, bufferSize * sizeof(float));
    memset(delayLineR, 0, bufferSize * sizeof(float));
  } else {
    Serial.println("CRITICAL: Total memory failure!");
    bufferSize = 0;
  }

  writeHead = 0;

  // Init default params
  currentParams = {};
  currentParams.tapeSpeed = 0.5f;
  currentParams.tapeAge = 0.5f;
  currentParams.headBumpAmount = 0.5f;
  currentParams.bpm = 90.0f;
  currentParams.headsMusical = true;
  currentParams.guitarFocus = false;
  currentParams.tone = 0.5f;
  
  // New effect modes defaults
  currentParams.pingPong = false;
  currentParams.freeze = false;
  currentParams.reverse = false;
  currentParams.reverseSmear = false;
  currentParams.spring = false;
  currentParams.springDecay = 0.5f;
  currentParams.springDamping = 0.5f;
  
  // Freeze state init
  freezeFade = 0.0f;
  freezeHead = 0;
  
  // Spring reverb allpass init (Schroeder delays)
  static const float springCoeffs[6] = {0.7f, 0.65f, 0.6f, 0.6f, 0.5f, 0.5f};
  static const int springTimes[6] = {223, 367, 491, 647, 821, 1039}; // Primes ~5-23ms
  for (int i = 0; i < 6; i++) {
    springAP_L[i].init(springTimes[i]);
    springAP_R[i].init(springTimes[i] + 23); // Stereo spread
    springAP_L[i].setCoeff(springCoeffs[i]);
    springAP_R[i].setCoeff(springCoeffs[i]);
    springLPF_L[i].setLowpass(fs, 2500.0f, 0.5f);
    springLPF_R[i].setLowpass(fs, 2500.0f, 0.5f);
  }
  
  // Reverse smear allpass init
  static const float reverseCoeffs[4] = {0.6f, 0.55f, 0.5f, 0.45f};
  static const int revTimes[4] = {151, 313, 569, 797};
  for (int i = 0; i < 4; i++) {
    reverseAP_L[i].init(revTimes[i]);
    reverseAP_R[i].init(revTimes[i] + 17);
    reverseAP_L[i].setCoeff(reverseCoeffs[i]);
    reverseAP_R[i].setCoeff(reverseCoeffs[i]);
  }

  updateFilters();
  flutterLPF.setLowpass(fs, 15.0f, 0.707f);

  // Guitar Focus Filters Defaults
  inputHPF.setLowpass(fs, 150.0f,
                      0.707f); // Helper call, will be reset in updateFilters
  inputLPF.setLowpass(fs, 5000.0f, 0.707f);
  inputHPFR.setLowpass(fs, 150.0f, 0.707f);
  inputLPFR.setLowpass(fs, 5000.0f, 0.707f);
}

TapeModel::~TapeModel() {
  if (delayLine)
    heap_caps_free(delayLine);
  if (delayLineR)
    heap_caps_free(delayLineR);
}

void TapeModel::updateFilters() {
  // CRITICAL: UI sends 0-100, we need 0.0-1.0 for calculations
  float speedMod = currentParams.tapeSpeed * 0.01f;
  float ageMod = currentParams.tapeAge * 0.01f;
  float toneMod = currentParams.tone * 0.01f;

  // 0. Input Conditioning (Guitar Focus)
  // HPF = 150Hz to remove mud
  // LPF = 5000Hz to smooth pick attack
  // We use HighShelf with negative gain for HPF approximation if needed,
  // but here provided Biquad has setLowpass. We need setHighpass ideally.
  // Assuming setHighShelf with big cut works as HPF replacement or we add
  // setHighpass. Workaround: Use HighShelf cut for Low End removal (Not ideal)
  // -> Actually let's assume standard Biquad usage. Since BiquadFilter only has
  // setLowpass/setLowShelf/setHighShelf, we will use shelves to shape. HPF
  // approx: LowShelf @ 150Hz, -24dB LPF approx: LowPass @ 5000Hz

  // Actually, let's just stick to what we have in BiquadFilter definition.
  // It has: setLowShelf, setHighShelf, setLowpass.
  // We will Simulate HPF with LowShelf -30dB at 150Hz.
  if (currentParams.guitarFocus) {
    inputHPF.setLowShelf(sampleRate, 150.0f, 0.7f, -30.0f);
    inputHPFR.setLowShelf(sampleRate, 150.0f, 0.7f, -30.0f);
    inputLPF.setLowpass(sampleRate, 5000.0f, 0.707f);
    inputLPFR.setLowpass(sampleRate, 5000.0f, 0.707f);
  } else {
    // Neutral - wide open
    inputHPF.setLowShelf(sampleRate, 20.0f, 0.7f, 0.0f);
    inputHPFR.setLowShelf(sampleRate, 20.0f, 0.7f, 0.0f);
    inputLPF.setLowpass(sampleRate, 20000.0f, 0.707f);
    inputLPFR.setLowpass(sampleRate, 20000.0f, 0.707f);
  }

  // 1. Head Bump (Low frequencies) - Adds "body" to the sound
  // 1. Head Bump (Low frequencies) - Adds "body" to the sound
  // TUNED: Stymon-style "Punch" (100-200Hz) instead of "Rumble" (60Hz)
  // TUNED: Stymon-style "Punch" (100-200Hz) instead of "Rumble" (60Hz)
  // TUNED: Fixed at 100Hz (Classic Tape Echo Bump)
  float bumpFreq = 100.0f;
  // HeadBumpAmount is 0-100. We want max ~12dB.
  // 100 * 0.05 = 5dB (Safe for feedback loop).
  float bumpGain = currentParams.headBumpAmount * 0.05f;
  headBump.setLowShelf(sampleRate, bumpFreq, 0.7f, bumpGain);
  headBumpR.setLowShelf(sampleRate, bumpFreq, 0.7f, bumpGain);

  // --- DARK MODE FILTER ---

  // Base Frequency (Ceiling):
  // Drastically reduced. Fast tape now ~10.5kHz (was >16k).
  // Slow tape now ~1.5kHz (very muffled).
  // Base Frequency (Ceiling):
  // TUNED: Raised floor to 6kHz to prevent "Mud/Bass Only" at low speeds
  // Range: 6kHz (Slow) to 16kHz (Fast)
  float baseFreq = 6000.0f + (speedMod * 10000.0f);

  // Age Factor:
  // The AGE parameter destroys highs.
  // If age = 1.0 (100%), reduces cutoff frequency by 90%.
  float ageFactor = 1.0f - (ageMod * 0.90f);

  // TONE CONTROL INTERACTION
  // Tone < 0.5 -> Darkens further
  // Tone > 0.5 -> Brightens (offsets age effect)
  float toneFactor = (toneMod - 0.5f) * 2.0f; // -1.0 to 1.0

  // Apply tone to base Cutoff
  // If Tone is high, we resist the age cutoff.
  if (toneFactor > 0.0f) {
    ageFactor += toneFactor * 0.5f; // Recover up to 50% of lost highs
    if (ageFactor > 1.0f)
      ageFactor = 1.0f;
  } else {
    ageFactor *= (1.0f + toneFactor * 0.5f); // Reduce further up to 50%
  }

  float finalCutoff = baseFreq * ageFactor;

  // Minimum 400Hz limit to ensure it's still "audio" not just "hum"
  if (finalCutoff < 400.0f)
    finalCutoff = 400.0f;

  // Dual cut to eliminate digital brightness:
  // 1. Aggressive high shelf (-50dB)
  tapeRolloff.setHighShelf(sampleRate, finalCutoff, 0.5f, -50.0f);
  tapeRolloffR.setHighShelf(sampleRate, finalCutoff, 0.5f, -50.0f);

  // 2. Low Pass Filter (LPF) to clean up remaining highs
  outputLPF.setLowpass(sampleRate, finalCutoff, 0.707f);
  outputLPFR.setLowpass(sampleRate, finalCutoff, 0.707f);

  // --- FEEDBACK FILTERS (Authentic tape degradation per repeat) ---
  // 1. LPF: More aggressive high cut for vintage darkness
  // --- FEEDBACK FILTERS (Authentic tape degradation per repeat) ---
  // --- FEEDBACK FILTERS (Authentic tape degradation per repeat) ---
  // 1. LPF: Brighter repeated (1.5kHz - 12kHz)
  float fbCutoff = 1500.0f + (speedMod * 10500.0f);
  // slow tape = ~1.5kHz, fast = ~12kHz
  feedbackLPF.setLowpass(sampleRate, fbCutoff, 0.5f);
  feedbackLPFR.setLowpass(sampleRate, fbCutoff, 0.5f);
  
  // 2. HPF: Remove mud accumulation (tape heads lose low frequencies too)
  // 2. HPF: Aggressive mud removal (was 200Hz)
  // TUNED: Increased to 300Hz to fix "absurdly grave" feedback
  feedbackHPF.setHighpass(sampleRate, 300.0f, 0.5f);
  feedbackHPFR.setHighpass(sampleRate, 300.0f, 0.5f);
  
  // 3. Allpass: Phase smearing for vintage character (head gap simulation)
  float allpassCoeff = 0.3f + ageMod * 0.4f;  // More aged = more smear
  feedbackAllpass.setCoeff(allpassCoeff);
  feedbackAllpassR.setCoeff(allpassCoeff);

  // 4. Spring Reverb Updates (Moved from process loop to save CPU)
  // Limit max feedback to 0.85 to prevent instability/denormals
  float springDecayMod = currentParams.springDecay * 0.01f;
  float springDampMod = currentParams.springDamping * 0.01f;

  float springCoeff = 0.4f + springDecayMod * 0.45f;  // 0.4-0.85
  float dampFreq = 1500.0f + springDampMod * 3000.0f;
  
  for (int i = 0; i < 6; i++) {
    springAP_L[i].setCoeff(springCoeff);
    springAP_R[i].setCoeff(springCoeff);
    springLPF_L[i].setLowpass(sampleRate, dampFreq, 0.5f);
    springLPF_R[i].setLowpass(sampleRate, dampFreq, 0.5f);
  }
}

void TapeModel::updateParams(const TapeParams &newParams) {
  // Detect activation to reset ramp
  if (!currentParams.delayActive && newParams.delayActive) {
    delayEnableRamp = 0.0f;
    // Reset DC Blockers to avoid popping
    dcBlocker = DCBlocker();
    dcBlockerR = DCBlocker();

    // CLEAR BUFFERS to prevent garbage feedback
    if (delayLine)
      memset(delayLine, 0, bufferSize * sizeof(float));
    if (delayLineR)
      memset(delayLineR, 0, bufferSize * sizeof(float));

    // Also reset smoothed delay to target to avoid swoop if time changed while
    // off
    float targetDelay = newParams.delayTimeMs * sampleRate * 0.001f;
    smoothedDelaySamples = targetDelay;
  }

  currentParams = newParams;
  dropout.setSeverity(newParams.dropoutSeverity);
  updateFilters();
}

AUDIO_INLINE float TapeModel::readTapeAt(float delaySamples, float *buffer) {
  if (!buffer || bufferSize == 0)
    return 0.0f;

  if (delaySamples < 2.0f)
    delaySamples = 2.0f;
  if (delaySamples > bufferSize - 4.0f)
    delaySamples = (float)bufferSize - 4.0f;

  float readPos = (float)writeHead - delaySamples;

  if (readPos < 0.0f)
    readPos += bufferSize;
  else if (readPos >= bufferSize)
    readPos -= bufferSize;

  int32_t r = (int32_t)readPos;
  float f = readPos - r;

  int32_t i1 = r;
  int32_t i2 = (r > 0) ? r - 1 : bufferSize - 1;
  int32_t i0 = (r < bufferSize - 1) ? r + 1 : 0;
  int32_t i3 = (i0 < bufferSize - 1) ? i0 + 1 : 0;

  float d1 = buffer[i1];
  float d0 = buffer[i0];
  float d2 = buffer[i2];
  float d3 = buffer[i3];

  // Hermite Interpolation
  float c0 = d1;
  float c1 = 0.5f * (d0 - d2);
  float c2 = d2 - 2.5f * d1 + 2.0f * d0 - 0.5f * d3;
  float c3 = 0.5f * (d3 - d1) + 1.5f * (d1 - d0);

  return ((c3 * f + c2) * f + c1) * f + c0;
}

// === REVERSE DELAY: Read buffer in opposite direction ===
// Creates true reverse effect by reading the delay buffer backwards
IRAM_ATTR float TapeModel::readTapeReverse(float delaySamples, float *buffer) {
  if (!buffer || bufferSize <= 0) return 0.0f;
  
  if (delaySamples < 2.0f)
    delaySamples = 2.0f;
  if (delaySamples > bufferSize - 4.0f)
    delaySamples = (float)bufferSize - 4.0f;

  // REVERSE: Read from position that moves in opposite direction
  // Instead of (writeHead - delay), we use (writeHead - (delaySamples - playbackPos))
  // Where playbackPos cycles through the delay length
  
  // Calculate a reverse read position within the delay window
  // This creates the effect of audio playing backwards
  int32_t delayInt = (int32_t)delaySamples;
  
  // Use static variables to track reverse playback position
  static int32_t reverseCounter = 0;
  static int32_t reverseWindowSize = 0;
  
  // Reset counter when window size changes significantly
  if (abs(delayInt - reverseWindowSize) > 1000) {
    reverseCounter = 0;
    reverseWindowSize = delayInt;
  }
  
  // Increment counter (wraps within delay window)
  reverseCounter++;
  if (reverseCounter >= delayInt) {
    reverseCounter = 0;
  }
  
  // Read position: start from oldest sample and move towards newest
  // This is the opposite of normal playback
  float readPos = (float)writeHead - delaySamples + (float)reverseCounter;
  
  // Safe modulo wrapping
  int32_t readPosInt = (int32_t)readPos;
  readPosInt = readPosInt % bufferSize;
  if (readPosInt < 0) readPosInt += bufferSize;
  
  int32_t r = readPosInt;
  float f = readPos - floorf(readPos);

  // Safe index calculation
  int32_t i1 = r;
  int32_t i0 = (r > 0) ? r - 1 : bufferSize - 1;
  int32_t i2 = (r < bufferSize - 1) ? r + 1 : 0;
  int32_t i3 = (i2 < bufferSize - 1) ? i2 + 1 : 0;

  float d1 = buffer[i1];
  float d0 = buffer[i0];
  float d2 = buffer[i2];
  float d3 = buffer[i3];

  // Hermite Interpolation
  float c0 = d1;
  float c1 = 0.5f * (d0 - d2);
  float c2 = d2 - 2.5f * d1 + 2.0f * d0 - 0.5f * d3;
  float c3 = 0.5f * (d3 - d1) + 1.5f * (d1 - d0);

  return ((c3 * f + c2) * f + c1) * f + c0;
}

// Tube-like Asymmetric Saturation (Smoother)
AUDIO_INLINE float saturator(float x) {
  // 1. Asymmetry (Tube Bias) - Adds even harmonics
  // Lower bias influence for cleaner headroom
  if (x > 0.5f)
    x = 0.5f + (x - 0.5f) * 0.9f;

  // 2. Soft Clipping (Tapered)
  if (x > 2.0f)
    return 1.0f; // Extended headroom
  if (x < -2.0f)
    return -1.0f;

  // Smooth cubic clipper (Gentler slope)
  return x - (0.08f * x * x * x);
}

IRAM_ATTR float TapeModel::process(float input) {
  if (!delayLine)
    return input;

  TapeParams *p = &currentParams;

  // --- MODULATION ---
  float flutterInc = TWO_PI * p->flutterRate / sampleRate;
  flutterPhase += flutterInc;
  if (flutterPhase > TWO_PI)
    flutterPhase -= TWO_PI;

  float wowInc = TWO_PI * p->wowRate / sampleRate;
  wowPhase += wowInc;
  if (wowPhase > TWO_PI)
    wowPhase -= TWO_PI;

  // Wow and Flutter combined
  float rawMod =
      (sinf(flutterPhase) * p->flutterDepth) + (sinf(wowPhase) * p->wowDepth);
  // Filter motion to simulate capstan inertia
  float mod = flutterLPF.process(rawMod);

  azimuthPhase += (0.2f / sampleRate);
  if (azimuthPhase > 1.0f)
    azimuthPhase = 0.0f;
  float tri = (azimuthPhase < 0.5f) ? (azimuthPhase * 2.0f)
                                    : (2.0f - azimuthPhase * 2.0f);
  float azimuthMod = 0.5f + (tri * 1.5f);

  bool useAzimuth = (p->azimuthError > 0.01f);
  if (useAzimuth) {
    azimuthFilter.setCoeff(-0.90f * p->azimuthError * azimuthMod);
  }

  // --- RAMP LOGIC ---
  if (p->delayActive) {
    // Slower ramp for stability (~250ms)
    delayEnableRamp += (1.0f / (0.25f * sampleRate));
    if (delayEnableRamp > 1.0f)
      delayEnableRamp = 1.0f;
  } else {
    delayEnableRamp = 0.0f;
  }

  // --- SMOOTH DELAY TIME ---
  float targetDelay = p->delayTimeMs * sampleRate * 0.001f;
  // Simple one-pole smoothing
  smoothedDelaySamples += 0.001f * (targetDelay - smoothedDelaySamples);

  // --- LEITURA (MANTIDA IGUAL) ---
  float tapeSignal = 0.0f;
  float modDepth = 2.0f; // Wow intenso mantido

  if (!p->delayActive) {
    tapeSignal = readTapeAt(200.0f + mod * 40.0f * modDepth, delayLine);
  } else {
    float baseDelay = smoothedDelaySamples;
    float headGainSum = 0.0f;

    if (p->headsMusical) {
      float beatMs = 60000.0f / p->bpm;
      float d1 =
          (beatMs * 0.333f * sampleRate * 0.001f) + (mod * 40.0f * modDepth);
      float d2 =
          (beatMs * 0.75f * sampleRate * 0.001f) + (mod * 60.0f * modDepth);
      float d3 =
          (beatMs * 1.0f * sampleRate * 0.001f) + (mod * 80.0f * modDepth);

      if (p->activeHeads & 1) {
        tapeSignal += readTapeAt(d1, delayLine) * 1.0f;
        headGainSum += 1.0f;
      }
      if (p->activeHeads & 2) {
        tapeSignal += readTapeAt(d2, delayLine) * 0.75f;
        headGainSum += 0.75f;
      }
      if (p->activeHeads & 4) {
        tapeSignal += readTapeAt(d3, delayLine) * 0.55f;
        headGainSum += 0.55f;
      }
    } else {
      float d1 = (baseDelay * 0.33f) + (mod * 40.0f * modDepth);
      float d2 = (baseDelay * 0.66f) + (mod * 60.0f * modDepth);
      float d3 = baseDelay + (mod * 80.0f * modDepth);

      if (p->activeHeads & 1) {
        tapeSignal += readTapeAt(d1, delayLine) * 1.0f;
        headGainSum += 1.0f;
      }
      if (p->activeHeads & 2) {
        tapeSignal += readTapeAt(d2, delayLine) * 0.75f;
        headGainSum += 0.75f;
      }
      if (p->activeHeads & 4) {
        tapeSignal += readTapeAt(d3, delayLine) * 0.55f;
        headGainSum += 0.55f;
      }
    }

    // Normalize gain to prevent explosion with multiple heads
    if (headGainSum > 0.0f)
      tapeSignal /= headGainSum;
  }

  // --- DEGRADAÇÃO & FILTROS ---
  float dropoutGain = dropout.process();
  tapeSignal *= dropoutGain;

  if (p->noise > 0.001f) {
    float hiss =
        noiseGen.next() * p->noise * (1.0f + (2.0f * (1.0f - dropoutGain)));
    tapeSignal += hiss;
  }

  if (useAzimuth) {
    tapeSignal = azimuthFilter.process(tapeSignal);
  }

  // SPLIT PATH: Capture signal for feedback BEFORE aggressive shelf/LPF
  // coloration
  float signalForFeedback = tapeSignal;

  // Output Filters (Coloration for Mix)
  // Equalização "Dark" aplicada aqui
  tapeSignal = headBump.process(tapeSignal);
  tapeSignal = tapeRolloff.process(tapeSignal);
  tapeSignal = outputLPF.process(tapeSignal);

  // --- FEEDBACK & DRIVE (ENHANCED TAPE DEGRADATION) ---
  float feedSig = 0.0f;
  if (p->delayActive) {
    // Start from pre-shelf signal
    feedSig = signalForFeedback;

    // === AUTHENTIC TAPE DEGRADATION CHAIN ===
    // 1. LPF: Aggressive high cut (darkens each repeat)
    feedSig = feedbackLPF.process(feedSig);
    
    // 2. HPF: Remove mud accumulation (tape heads lose lows too)
    feedSig = feedbackHPF.process(feedSig);
    
    // 3. Allpass: Phase smearing (head gap simulation)
    feedSig = feedbackAllpass.process(feedSig);
    
    // 4. Progressive saturation (accumulates per repeat)
    feedSig = tanhf(feedSig * 1.3f) / 1.3f;

    // 5. Clamp user feedback to safe limit internally
    // 5. Clamp user feedback to safe limit internally
    // Scale 0-100 -> 0.0-1.0
    float safeFeedback = p->feedback * 0.01f;
    if (safeFeedback > 0.85f)
      safeFeedback = 0.85f;

    feedSig *= safeFeedback;

    // 6. Apply Ramp
    feedSig *= delayEnableRamp;

    // 7. Constrain feedback energy prevents explosion
    feedSig = constrain(feedSig, -1.2f, 1.2f);
  }

  // 4. Drive first, then add limited feedback
  // 4. Drive first, then add limited feedback
  // Scale Drive: 0-100 -> 0.0-5.0 Gain
  float inDriven = input * (p->drive * 0.05f);
  float recSig = inDriven + feedSig;

  // DC Block processed here (Record Path) instead of feedback path
  recSig = dcBlocker.process(recSig);

  if (recSig > 4.0f)
    recSig = 4.0f;
  else if (recSig < -4.0f)
    recSig = -4.0f;

  delayLine[writeHead] = saturator(recSig);

  writeHead++;
  if (writeHead >= bufferSize)
    writeHead = 0;

  // Scale Mix 0-100 -> 0.0-1.0
  float mix = p->dryWet * 0.01f;
  return outputLimiter((input * (1.0f - mix)) + (tapeSignal * mix));
}

// STEREO PROCESS
IRAM_ATTR void TapeModel::processStereo(float inL, float inR, float *outL,
                                        float *outR) {
  if (!delayLine || !delayLineR) {
    *outL = inL;
    *outR = inR;
    return;
  }

  TapeParams *p = &currentParams;

  // --- SHARED MODULATION (Resources Saved!) ---
  float flutterInc = 6.28318f * p->flutterRate / sampleRate;
  flutterPhase += flutterInc;
  if (flutterPhase > 6.28318f)
    flutterPhase -= 6.28318f;

  float wowInc = 6.28318f * p->wowRate / sampleRate;
  wowPhase += wowInc;
  if (wowPhase > 6.28318f)
    wowPhase -= 6.28318f;

  // Scale Depths: 0-100 -> 0.0-1.0
  float rawMod =
      (sinf(flutterPhase) * (p->flutterDepth * 0.01f)) + (sinf(wowPhase) * (p->wowDepth * 0.01f));
  float mod = flutterLPF.process(rawMod);

  azimuthPhase += (0.2f / sampleRate);
  if (azimuthPhase > 1.0f)
    azimuthPhase = 0.0f;
  float tri = (azimuthPhase < 0.5f) ? (azimuthPhase * 2.0f)
                                    : (2.0f - azimuthPhase * 2.0f);
  float azimuthMod = 0.5f + (tri * 1.5f);

  bool useAzimuth = (p->azimuthError > 0.01f);
  if (useAzimuth) {

    // Scale Azimuth: 0-100 -> 0.0-1.0
    float azCoeff = -0.90f * (p->azimuthError * 0.01f) * azimuthMod;
    azimuthFilter.setCoeff(azCoeff);
    azimuthFilterR.setCoeff(
        azCoeff); // Share coefficient, apply to both filters
  }

  // --- SHARED NOISE & DROPOUT ---
  float dropoutGain = dropout.process();
  float hiss = 0.0f;
  if (p->noise > 0.001f) {
    // Shared noise source (Mono noise), scaled 0-100 -> 0-0.1 (doubled for audibility)
    hiss = noiseGen.next() * (p->noise * 0.001f) * (1.0f + (2.0f * (1.0f - dropoutGain)));
  }

  // --- RAMP LOGIC (STEREO SHARED) ---
  // Using same ramp for stereo as param is global
  if (p->delayActive) {
    delayEnableRamp += 0.001f;
    if (delayEnableRamp > 1.0f)
      delayEnableRamp = 1.0f;
  } else {
    delayEnableRamp = 0.0f;
  }

  // --- SMOOTH DELAY TIME (STEREO SHARED) ---
  float targetDelay = p->delayTimeMs * sampleRate * 0.001f;
  // Reduced smoothing speed to prevent extreme pitch shifts (artifacts)
  smoothedDelaySamples += 0.0001f * (targetDelay - smoothedDelaySamples);

  // --- CHANNEL PROCESSING HELPER (Inline-ish logic) ---
  auto processChannel = [&](float input, float *buffer, BiquadFilter &hb,
                            BiquadFilter &tr, BiquadFilter &outLPF,
                            AllpassFilter &az, DCBlocker &dc, BiquadFilter &iHP,
                            BiquadFilter &iLP, BiquadFilter &fbLPF, 
                            BiquadFilter &fbHPF, AllpassFilter &fbAllpass) -> float {
    // INPUT CONDITIONING
    float condInput = input;
    condInput = iHP.process(condInput);
    condInput = iLP.process(condInput);

    float tapeSig = 0.0f;
    float modDepth = 2.0f;
    float headGainSum = 0.0f;

    // READ (with REVERSE support)
    if (!p->delayActive) {
      tapeSig = readTapeAt(200.0f + mod * 40.0f * modDepth, buffer);
    } else {
      float baseDelay = smoothedDelaySamples;
      float d1, d2, d3;

      if (p->headsMusical) {
        float beatMs = 60000.0f / p->bpm;
        d1 = beatMs * 0.333f * sampleRate * 0.001f;
        d2 = beatMs * 0.75f * sampleRate * 0.001f;
        d3 = beatMs * 1.0f * sampleRate * 0.001f;
      } else {
        d1 = baseDelay * 0.33f;
        d2 = baseDelay * 0.66f;
        d3 = baseDelay;
      }

      d1 += mod * 40.0f * modDepth;
      d2 += mod * 60.0f * modDepth;
      d3 += mod * 80.0f * modDepth;

      // === REVERSE MODE: Use readTapeReverse ===
      if (p->reverse) {
        // Reverse works best with single long delay (head 3)
        tapeSig = readTapeReverse(d3, buffer);
        headGainSum = 1.0f;
      } else {
        // Normal multi-head reading
        if (p->activeHeads & 1) {
          tapeSig += readTapeAt(d1, buffer) * 1.0f;
          headGainSum += 1.0f;
        }
        if (p->activeHeads & 2) {
          tapeSig += readTapeAt(d2, buffer) * 0.75f;
          headGainSum += 0.75f;
        }
        if (p->activeHeads & 4) {
          tapeSig += readTapeAt(d3, buffer) * 0.55f;
          headGainSum += 0.55f;
        }
      }

      if (headGainSum > 0.0f)
        tapeSig /= headGainSum;
    }

    // DEGRADE
    tapeSig *= dropoutGain;
    // MOVED NOISE TO OUTPUT STAGE to prevent "Rumble" at low tape speeds
    // tapeSig += hiss;

    if (useAzimuth)
      tapeSig = az.process(tapeSig);

    tapeSig = hb.process(tapeSig);
    tapeSig = tr.process(tapeSig);
    tapeSig = outLPF.process(tapeSig);

    // --- FEEDBACK & DRIVE (ENHANCED TAPE DEGRADATION) ---
    float feedSig = 0.0f;
    if (p->delayActive) {
      feedSig = tapeSig;
      
      // === AUTHENTIC TAPE DEGRADATION CHAIN ===
      // 1. LPF: Aggressive high cut (darkens each repeat)
      feedSig = fbLPF.process(feedSig);
      
      // 2. HPF: Remove mud accumulation
      feedSig = fbHPF.process(feedSig);
      
      // 3. Allpass: Phase smearing (head gap simulation)
      feedSig = fbAllpass.process(feedSig);
      
      // 4. Progressive saturation (accumulates per repeat)
      feedSig = tanhf(feedSig * 1.3f) / 1.3f;

      // Scale 0-100 -> 0.0-1.0
      float safeFeedback = p->feedback * 0.01f;
      if (safeFeedback > 0.88f)
        safeFeedback = 0.88f;
      feedSig *= safeFeedback;

      feedSig = feedbackCompressor(feedSig);
      feedSig *= delayEnableRamp;

      // Damping (Fade out feedback if silence persists)
      if (fabsf(input) < 1e-6f && fabsf(feedSig) < 1e-4f) {
        feedSig *= 0.9995f;
      }
    }

    // Summing
    // Scale Drive: 0-100 -> 0.0-5.0 Gain
    float inDriven = input * (p->drive * 0.05f);
    float recSig = inDriven + feedSig;

    // Apply DC Block to Record Signal (Input + Feedback) before saturation
    recSig = dc.process(recSig);

    // Soft Clipper Limit for Buffer
    if (recSig > 4.0f)
      recSig = 4.0f;
    else if (recSig < -4.0f)
      recSig = -4.0f;

    // Write to buffer
    // FREEZE FIX: Stop writing input when frozen (Loop existing content)
    if (!p->freeze) {
      buffer[writeHead] = saturator(recSig);
    }

    // Scale Mix 0-100 -> 0.0-1.0
    float mix = p->dryWet * 0.01f;
    return outputLimiter((input * (1.0f - mix)) + (tapeSig * mix));
  };

  // PROCESS LEFT
  *outL =
      processChannel(inL, delayLine, headBump, tapeRolloff, outputLPF,
                     azimuthFilter, dcBlocker, inputHPF, inputLPF, feedbackLPF,
                     feedbackHPF, feedbackAllpass);

  // PROCESS RIGHT
  *outR = processChannel(inR, delayLineR, headBumpR, tapeRolloffR, outputLPFR,
                         azimuthFilterR, dcBlockerR, inputHPFR, inputLPFR,
                         feedbackLPFR, feedbackHPFR, feedbackAllpassR);

  // === POST-PROCESSING: NEW EFFECT MODES ===
  
  // Inject Noise here (Post-Filter, Pre-Reverb)
  // This bypasses the dark Speed LPF, keeping hiss "fresh"
  // Attenuate slightly as it's adding to full mix
  if (p->noise > 0.001f) {
      *outL += hiss * 0.5f;
      *outR += hiss * 0.5f;
  }

  // --- REVERSE SMEAR (Allpass diffusion for Reverse Reverb) ---
  if (p->reverseSmear && p->reverse) {
    for (int i = 0; i < 4; i++) {
      *outL = reverseAP_L[i].process(*outL);
      *outR = reverseAP_R[i].process(*outR);
    }
  }
  
  // --- SPRING REVERB (6-stage allpass cascade with tape damping) ---
  if (p->spring) {
    // Save dry signal for mix
    float dryL = *outL;
    float dryR = *outR;
    
    // OPTIMIZED: Coeffs updated in updateFilters()
    // Dynamic stage count based on decay (Scale Decay 0-100 -> 0-1)
    int stages = 3 + (int)((p->springDecay * 0.01f) * 3);   // 3-6 stages
    
    // Process wet signal through allpass cascade
    float wetL = *outL;
    float wetR = *outR;
    for (int i = 0; i < stages && i < 6; i++) {
      wetL = springAP_L[i].process(wetL);
      wetR = springAP_R[i].process(wetR);
      
      wetL = springLPF_L[i].process(wetL);
      wetR = springLPF_R[i].process(wetR);
    }
    
    // Apply mix (0-100% wet)
    float wetMix = p->springMix * 0.01f;  // 0-100 -> 0-1
    *outL = dryL * (1.0f - wetMix) + wetL * wetMix;
    *outR = dryR * (1.0f - wetMix) + wetR * wetMix;
    
    // Apply limiter again after spring
    *outL = outputLimiter(*outL);
    *outR = outputLimiter(*outR);
  }
  
  // --- FREEZE CROSSFADE ---
  if (p->freeze) {
    freezeFade += 0.0002f;  // ~100ms fade @ 48kHz
    if (freezeFade > 1.0f) freezeFade = 1.0f;
  } else {
    freezeFade -= 0.0008f;  // Faster unfade
    if (freezeFade < 0.0f) freezeFade = 0.0f;
  }

  // Advance Head ONCE for both channels (sync)
  // FIXED: writeHead MUST advance even in freeze mode to read the loop!
  writeHead++;
  if (writeHead >= bufferSize)
    writeHead = 0;
}