#include "TapeDelay.h"

TapeModel::TapeModel(float fs, float maxDelayTimeMs)
    : sampleRate(fs), noiseGen(fs, 0u), noiseGenR(fs, 2000u), flutterPhase(0), wowPhase(0),
      azimuthPhase(0), flutterInc(0.0f), wowInc(0.0f), azimuthInc(0.0f),
      delaySmoothAlpha(0.0f), delayRampInc(0.0f),
      mechNoiseSlowAlpha(0.0f), mechNoiseFastAlpha(0.0f), mechNoiseL(22222u), mechNoiseR(33333u),
      delayEnableRamp(0.0f), smoothedDelaySamples(0.0f), smoothedAzCoeff(0.0f), springFB_L(0.0f), springFB_R(0.0f) {
  tanhLUT.init();
  magneticsL.lut = &tanhLUT;
  magneticsR.lut = &tanhLUT;
  feedbackTapeSaturation.lut = &tanhLUT;
  feedbackTapeSaturationR.lut = &tanhLUT;
  // Safe buffer calculation
  bufferSize = (int32_t)(fs * (maxDelayTimeMs / 1000.0f));
  delayBufferCapacity = bufferSize + TAPE_BUFFER_GUARD;
  size_t bytes = delayBufferCapacity * sizeof(float);

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
    delayBufferCapacity = bufferSize + TAPE_BUFFER_GUARD;
    delayLine = (float *)heap_caps_malloc(delayBufferCapacity * sizeof(float),
                                          MALLOC_CAP_INTERNAL);
    delayLineR = (float *)heap_caps_malloc(delayBufferCapacity * sizeof(float),
                                           MALLOC_CAP_INTERNAL);
    usesSPIRAM = false;
  }

  if (delayLine && delayLineR) {
    memset(delayLine, 0, delayBufferCapacity * sizeof(float));
    memset(delayLineR, 0, delayBufferCapacity * sizeof(float));
  } else {
    Serial.println("CRITICAL: Total memory failure!");
    bufferSize = 0;
    delayBufferCapacity = 0;
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
  currentParams.springMix = 50.0f;
  magneticsL.updateParams(fs, currentParams);
  magneticsR.updateParams(fs, currentParams);
  
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
  flutterLPFR.setLowpass(fs, 15.0f, 0.707f);

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

  flutterInc = TWO_PI * currentParams.flutterRate / sampleRate;
  wowInc = TWO_PI * currentParams.wowRate / sampleRate;
  azimuthInc = 0.2f / sampleRate;
  delaySmoothAlpha = 1.0f - expf(-1.0f / (0.200f * sampleRate));
  delayRampInc = 1.0f / (0.250f * sampleRate);
  mechNoiseSlowAlpha = hydra::dsp::mechNoiseOnePoleAlpha(
      sampleRate, hydra::dsp::kMechNoiseSlowTimeSeconds);
  mechNoiseFastAlpha = hydra::dsp::mechNoiseOnePoleAlpha(
      sampleRate, hydra::dsp::kMechNoiseFastTimeSeconds);

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
    inputHPF.setHighpass(sampleRate, 150.0f, 0.707f);
    inputHPFR.setHighpass(sampleRate, 150.0f, 0.707f);
    inputLPF.setLowpass(sampleRate, 5000.0f, 0.707f);
    inputLPFR.setLowpass(sampleRate, 5000.0f, 0.707f);
  } else {
    // Neutral - wide open
    inputHPF.setHighpass(sampleRate, 20.0f, 0.707f);
    inputHPFR.setHighpass(sampleRate, 20.0f, 0.707f);
    inputLPF.setLowpass(sampleRate, 20000.0f, 0.707f);
    inputLPFR.setLowpass(sampleRate, 20000.0f, 0.707f);
  }

  // 1. Playback head bump (low frequencies) - adds body to the wet output.
  float bumpFreq = 100.0f;
  float bumpGain = currentParams.headBumpAmount * 0.05f;
  headBump.setLowShelf(sampleRate, bumpFreq, 0.7f, bumpGain);
  headBumpR.setLowShelf(sampleRate, bumpFreq, 0.7f, bumpGain);

  // Feedback uses a lighter head bump than the wet output (0-2 dB).
  float feedbackHeadBumpGain = constrain(currentParams.headBumpAmount * 0.02f, 0.0f, 2.0f);
  feedbackHeadBump.setLowShelf(sampleRate, bumpFreq, 0.7f, feedbackHeadBumpGain);
  feedbackHeadBumpR.setLowShelf(sampleRate, bumpFreq, 0.7f, feedbackHeadBumpGain);

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
  // 1. High shelf (-12dB) shifted an octave higher to prevent extreme resonance overlap
  tapeRolloff.setHighShelf(sampleRate, finalCutoff * 2.0f, 0.5f, -12.0f);
  tapeRolloffR.setHighShelf(sampleRate, finalCutoff * 2.0f, 0.5f, -12.0f);
 
  // 2. Low Pass Filter (LPF) to clean up remaining highs
  outputLPF.setLowpass(sampleRate, finalCutoff, 0.707f);
  outputLPFR.setLowpass(sampleRate, finalCutoff, 0.707f);
  dropoutLPF.setCutoff(sampleRate, finalCutoff);
  dropoutLPFR.setCutoff(sampleRate, finalCutoff);

  // --- FEEDBACK FILTERS (light physical tape degradation per repeat) ---
  // Progressive high loss follows tape age, but avoids reusing the wet output LPF.
  float feedbackWearAmount = ageMod;
  float fbCutoff = (9000.0f + (speedMod * 7000.0f)) * (1.0f - feedbackWearAmount * 0.65f);
  if (fbCutoff < 1600.0f)
    fbCutoff = 1600.0f;
  feedbackGapLoss.setLowpass(sampleRate, fbCutoff, 0.5f);
  feedbackGapLossR.setLowpass(sampleRate, fbCutoff, 0.5f);

  // Remove mud accumulation (tape heads lose low frequencies too).
  feedbackHPF.setHighpass(sampleRate, 300.0f, 0.5f);
  feedbackHPFR.setHighpass(sampleRate, 300.0f, 0.5f);

  // Phase smearing for vintage character (head gap simulation).
  float allpassCoeff = 0.3f + feedbackWearAmount * 0.4f;
  feedbackPhase.setCoeff(allpassCoeff);
  feedbackPhaseR.setCoeff(allpassCoeff);
  feedbackTapeSaturation.updateParams(sampleRate, currentParams);
  feedbackTapeSaturationR.updateParams(sampleRate, currentParams);

  float flutterCutoff = currentParams.flutterRate * 1.6f;
  if (flutterCutoff < 4.0f) flutterCutoff = 4.0f;
  if (flutterCutoff > 12.0f) flutterCutoff = 12.0f;
  flutterLPF.setLowpass(sampleRate, flutterCutoff, 0.707f);
  flutterLPFR.setLowpass(sampleRate, flutterCutoff, 0.707f);

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
    magneticsL.reset();
    magneticsR.reset();
    feedbackTapeSaturation.reset();
    feedbackTapeSaturationR.reset();

    // CLEAR BUFFERS to prevent garbage feedback
    if (delayLine)
      memset(delayLine, 0, delayBufferCapacity * sizeof(float));
    if (delayLineR)
      memset(delayLineR, 0, delayBufferCapacity * sizeof(float));

    // Also reset smoothed delay to target to avoid swoop if time changed while
    // off
    float targetDelay = newParams.delayTimeMs * sampleRate * 0.001f;
    smoothedDelaySamples = targetDelay;
  }

  currentParams = newParams;
  magneticsL.updateParams(sampleRate, currentParams);
  magneticsR.updateParams(sampleRate, currentParams);
  dropout.setSeverity(newParams.dropoutSeverity);
  updateFilters();
}

AUDIO_INLINE float TapeModel::hermite4(float ym1, float y0, float y1, float y2, float f) {
  float c0 = y0;
  float c1 = 0.5f * (y1 - ym1);
  float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
  float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
  return ((c3 * f + c2) * f + c1) * f + c0;
}

AUDIO_INLINE void TapeModel::mirrorDelayGuard(float *buffer) {
  if (!buffer || bufferSize <= 0) return;
  for (int i = 0; i < TAPE_BUFFER_GUARD; ++i) {
    buffer[bufferSize + i] = buffer[i];
  }
}

AUDIO_INLINE float TapeModel::mechanicalMod(float scrape, BiquadFilter &flutterFilter) {
  float capstan = fastSin(wowPhase) + 0.18f * fastSin(2.0f * wowPhase + 0.7f);
  float flutter = flutterFilter.process(fastSin(flutterPhase) + 0.35f * scrape);

  float flutterAmp = smoothedDelaySamples * (currentParams.flutterDepth * 0.001f);
  float wowAmp = smoothedDelaySamples * (currentParams.wowDepth * 0.001f);
  return (wowAmp * capstan) + (flutterAmp * flutter) +
         (hydra::dsp::kScrapeModAmount * smoothedDelaySamples * scrape);
}

AUDIO_INLINE float TapeModel::readTapeAt(float delaySamples, float *buffer) {
  if (!buffer || bufferSize == 0)
    return 0.0f;

  if (delaySamples < HERMITE_MIN_DELAY)
    delaySamples = HERMITE_MIN_DELAY;
  if (delaySamples > bufferSize - HERMITE_MARGIN)
    delaySamples = (float)bufferSize - HERMITE_MARGIN;

  float readPos = (float)writeHead - delaySamples;
  if (readPos < 0.0f)
    readPos += bufferSize;

  int32_t r = (int32_t)readPos;
  float f = readPos - r;

  // Guard samples mirror the beginning of the circular buffer at the end, so
  // forward Catmull-Rom taps are contiguous on ESP32-S3/PSRAM reads. Only the
  // single previous-sample tap needs a wrap branch at the physical start.
  int32_t prev = (r > 0) ? r - 1 : bufferSize - 1;
  return hermite4(buffer[prev], buffer[r], buffer[r + 1], buffer[r + 2], f);
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

IRAM_ATTR float TapeModel::process(float input) {
  if (!delayLine)
    return input;

  TapeParams *p = &currentParams;

  // --- MODULATION ---
  flutterPhase += flutterInc;
  if (flutterPhase > TWO_PI)
    flutterPhase -= TWO_PI;

  wowPhase += wowInc;
  if (wowPhase > TWO_PI)
    wowPhase -= TWO_PI;

  float scrape = mechNoiseL.lowRate(mechNoiseSlowAlpha, mechNoiseFastAlpha);
  float mod = mechanicalMod(scrape, flutterLPF);

  azimuthPhase += azimuthInc;
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
    delayEnableRamp += delayRampInc;
    if (delayEnableRamp > 1.0f)
      delayEnableRamp = 1.0f;
  } else {
    delayEnableRamp = 0.0f;
  }

  // --- SMOOTH DELAY TIME ---
  float targetDelay = p->delayTimeMs * sampleRate * 0.001f;
  // Simple one-pole smoothing
  smoothedDelaySamples += delaySmoothAlpha * (targetDelay - smoothedDelaySamples);

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
  dropout.beginFrame(sampleRate);
  DropoutFrame dropoutValue = dropout.value(0, sampleRate);
  if (dropoutValue.hfLoss < 0.9995f || dropoutValue.ampGain < 0.9995f) {
    float dynamicDropoutCutoff = (6000.0f + (p->tapeSpeed * 100.0f)) *
                                 (0.35f + 0.65f * dropoutValue.hfLoss);
    dropoutLPF.setCutoffFast(sampleRate, dynamicDropoutCutoff);
    tapeSignal = dropoutLPF.process(tapeSignal);
    tapeSignal *= dropoutValue.ampGain;
  }

  if (p->noise > 0.001f) {
    float hiss = noiseGen.next() * p->noise *
                 (1.0f + (2.0f * (1.0f - dropoutValue.ampGain)));
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

  // --- FEEDBACK & DRIVE (light physical tape degradation) ---
  float feedSig = 0.0f;
  if (p->delayActive) {
    feedSig = feedbackHPF.process(signalForFeedback);

    float safeFeedback = p->feedback * 0.01f;
    if (safeFeedback > 0.85f)
      safeFeedback = 0.85f;

    const float feedbackWearAmount = p->tapeAge * 0.01f;
    float feedbackDrive = 1.0f + (safeFeedback * 0.35f) +
                          constrain(fabsf(input) * 0.20f, 0.0f, 0.25f) +
                          (feedbackWearAmount * 0.10f);
    feedSig *= feedbackDrive;
    feedSig = feedbackHeadBump.process(feedSig);
    feedSig = feedbackGapLoss.process(feedSig);
    feedSig = feedbackPhase.process(feedSig);
    feedSig = feedbackTapeSaturation.process(feedSig);
    feedSig *= safeFeedback;

    // Stability is enforced after the physical feedback block.
    feedSig = constrain(feedSig, -1.2f, 1.2f);
    feedSig *= delayEnableRamp;
  }

  float inDriven = input * (p->drive * 0.05f);
  float recSig = inDriven + feedSig;

  // DC Block processed here (Record Path) instead of feedback path
  recSig = dcBlocker.process(recSig);

  if (recSig > 4.0f)
    recSig = 4.0f;
  else if (recSig < -4.0f)
    recSig = -4.0f;

  delayLine[writeHead] = magneticsL.process(recSig);
  if (writeHead < TAPE_BUFFER_GUARD) {
    mirrorDelayGuard(delayLine);
  }

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

  // --- SHARED MECHANICAL MODULATION ---
  flutterPhase += flutterInc;
  if (flutterPhase > TWO_PI)
    flutterPhase -= TWO_PI;

  wowPhase += wowInc;
  if (wowPhase > TWO_PI)
    wowPhase -= TWO_PI;

  // --- SMOOTH DELAY TIME (STEREO SHARED) ---
  float targetDelay = p->delayTimeMs * sampleRate * 0.001f;
  smoothedDelaySamples += delaySmoothAlpha * (targetDelay - smoothedDelaySamples);

  // Per-channel deterministic low-rate noise models tension/friction wander.
  // This avoids injecting white noise directly into the delay tap while keeping
  // stereo motion subtly decorrelated.
  float scrapeL = mechNoiseL.lowRate(mechNoiseSlowAlpha, mechNoiseFastAlpha);
  float scrapeR = mechNoiseR.lowRate(mechNoiseSlowAlpha, mechNoiseFastAlpha);
  float modL = mechanicalMod(scrapeL, flutterLPF);
  float modR = mechanicalMod(scrapeR, flutterLPFR);

  azimuthPhase += azimuthInc;
  if (azimuthPhase > 1.0f)
    azimuthPhase = 0.0f;
  float tri = (azimuthPhase < 0.5f) ? (azimuthPhase * 2.0f)
                                    : (2.0f - azimuthPhase * 2.0f);
  float azimuthMod = 0.5f + (tri * 1.5f);

  bool useAzimuth = (p->azimuthError > 0.01f);
  if (useAzimuth) {
    float targetAz = -0.90f * (p->azimuthError * 0.01f) * azimuthMod;
    smoothedAzCoeff += 0.001f * (targetAz - smoothedAzCoeff);
    azimuthFilter.setCoeff(smoothedAzCoeff);
    azimuthFilterR.setCoeff(smoothedAzCoeff);
  }

  // --- SHARED NOISE & DROPOUT ---
  dropout.beginFrame(sampleRate);
  DropoutFrame dropoutL = dropout.value(0, sampleRate);
  DropoutFrame dropoutR = dropout.value(1, sampleRate);
  float dropoutAvg = 0.5f * (dropoutL.ampGain + dropoutR.ampGain);
  float hissL = 0.0f, hissR = 0.0f;
  if (p->noise > 0.001f) {
    float noiseMult = (p->noise * 0.001f) * (1.0f + (2.0f * (1.0f - dropoutAvg)));
    hissL = noiseGen.next() * noiseMult;
    hissR = noiseGenR.next() * noiseMult;
  }

  // --- RAMP LOGIC (STEREO SHARED) ---
  if (p->delayActive) {
    delayEnableRamp += delayRampInc;
    if (delayEnableRamp > 1.0f)
      delayEnableRamp = 1.0f;
  } else {
    delayEnableRamp = 0.0f;
  }

  // --- CHANNEL PROCESSING HELPER (Inline-ish logic) ---
  auto processChannel = [&](float input, float *buffer, BiquadFilter &hb,
                            BiquadFilter &tr, BiquadFilter &outLPF,
                            OnePoleLP &dropoutFilter, DropoutFrame dropoutValue,
                            AllpassFilter &az, DCBlocker &dc, TapeMagnetics &mag, BiquadFilter &iHP,
                            BiquadFilter &iLP, BiquadFilter &feedbackHeadBump,
                            BiquadFilter &feedbackGapLoss, BiquadFilter &fbHPF,
                            AllpassFilter &feedbackPhase, TapeMagnetics &feedbackTapeSaturation,
                            float channelMod) -> float {
    // --- input conditioning ---
    float condInput = input;
    condInput = iHP.process(condInput);
    condInput = iLP.process(condInput);

    // --- delay read ---
    float tapeSig = 0.0f;
    float headGainSum = 0.0f;

    if (!p->delayActive) {
      tapeSig = readTapeAt(200.0f + channelMod, buffer);
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

      d1 += channelMod * 0.33f;
      d2 += channelMod * 0.66f;
      d3 += channelMod;

      if (p->reverse) {
        tapeSig = readTapeReverse(d3, buffer);
        headGainSum = 1.0f;
      } else {
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

    // --- dropout/gap loss ---
    if (dropoutValue.hfLoss < 0.9995f || dropoutValue.ampGain < 0.9995f) {
      float dynamicDropoutCutoff = (6000.0f + (p->tapeSpeed * 100.0f)) *
                                   (0.35f + 0.65f * dropoutValue.hfLoss);
      dropoutFilter.setCutoffFast(sampleRate, dynamicDropoutCutoff);
      tapeSig = dropoutFilter.process(tapeSig);
      tapeSig *= dropoutValue.ampGain;
    }
    if (useAzimuth)
      tapeSig = az.process(tapeSig);

    // --- playback head EQ ---
    const float feedbackWearAmount = p->tapeAge * 0.01f;
    const float signalForFeedback = tapeSig;
    tapeSig = hb.process(tapeSig);
    tapeSig = tr.process(tapeSig);
    tapeSig = outLPF.process(tapeSig);

    // --- feedback return ---
    float feedSig = 0.0f;
    if (p->delayActive) {
      feedSig = signalForFeedback;
      feedSig = fbHPF.process(feedSig);

      float safeFeedback = p->feedback * 0.01f;
      if (safeFeedback > 0.88f)
        safeFeedback = 0.88f;

      // Light physical chain inside the loop: lower gains than the wet output,
      // progressive wear, phase smear, and feedback/input-dependent drive.
      float feedbackDrive = 1.0f + (safeFeedback * 0.35f) +
                            constrain(fabsf(condInput) * 0.20f, 0.0f, 0.25f) +
                            (feedbackWearAmount * 0.10f);
      feedSig *= feedbackDrive;
      feedSig = feedbackHeadBump.process(feedSig);
      feedSig = feedbackGapLoss.process(feedSig);
      feedSig = feedbackPhase.process(feedSig);
      feedSig = feedbackTapeSaturation.process(feedSig);
      feedSig *= safeFeedback;

      // Limit total feedback energy after the physical block, not before it.
      feedSig = feedbackCompressor(feedSig);
      feedSig = constrain(feedSig, -1.2f, 1.2f);
      feedSig *= delayEnableRamp;

      if (fabsf(input) < 1e-6f && fabsf(feedSig) < 1e-4f) {
        feedSig *= 0.9995f;
      }
    }

    // --- record amplifier ---
    float inDriven = condInput * (p->drive * 0.05f);
    float recSig = inDriven + feedSig;

    recSig = dc.process(recSig);

    if (recSig > 4.0f)
      recSig = 4.0f;
    else if (recSig < -4.0f)
      recSig = -4.0f;

    // --- magnetic saturation/write ---
    if (!p->freeze) {
      buffer[writeHead] = mag.process(recSig);
    }

    float mix = p->dryWet * 0.01f;
    return outputLimiter((input * (1.0f - mix)) + (tapeSig * mix));
  };

  // PROCESS LEFT
  *outL =
      processChannel(inL, delayLine, headBump, tapeRolloff, outputLPF,
                     dropoutLPF, dropoutL, azimuthFilter, dcBlocker, magneticsL, inputHPF, inputLPF, feedbackHeadBump,
                     feedbackGapLoss, feedbackHPF, feedbackPhase, feedbackTapeSaturation, modL);

  // PROCESS RIGHT
  *outR = processChannel(inR, delayLineR, headBumpR, tapeRolloffR, outputLPFR,
                         dropoutLPFR, dropoutR, azimuthFilterR, dcBlockerR, magneticsR, inputHPFR, inputLPFR,
                         feedbackHeadBumpR, feedbackGapLossR, feedbackHPFR, feedbackPhaseR, feedbackTapeSaturationR, modR);

  // Inject Noise here (Post-Filter, Pre-Reverb)
  if (p->noise > 0.001f) {
      *outL += hissL * 0.5f;
      *outR += hissR * 0.5f;
  }

  // --- REVERSE SMEAR (Allpass diffusion for Reverse Reverb) ---
  if (p->reverseSmear && p->reverse) {
    for (int i = 0; i < 4; i++) {
      *outL = reverseAP_L[i].process(*outL);
      *outR = reverseAP_R[i].process(*outR);
    }
  }
  
  // --- SPRING REVERB (Recirculating Schroeder cascade) ---
  if (p->spring) {
    float dryL = *outL;
    float dryR = *outR;
    
    int stages = 3 + (int)((p->springDecay * 0.01f) * 3);
    
    float inWithFBL = *outL + springFB_L * (p->springDecay * 0.01f * 0.85f);
    float inWithFBR = *outR + springFB_R * (p->springDecay * 0.01f * 0.85f);
    
    float wetL = inWithFBL;
    float wetR = inWithFBR;
    for (int i = 0; i < stages && i < 6; i++) {
      wetL = springAP_L[i].process(wetL);
      wetR = springAP_R[i].process(wetR);
      wetL = springLPF_L[i].process(wetL);
      wetR = springLPF_R[i].process(wetR);
    }
    springFB_L = wetL; springFB_R = wetR;
    
    float wetMix = p->springMix * 0.01f;
    *outL = outputLimiter(dryL * (1.0f - wetMix) + wetL * wetMix);
    *outR = outputLimiter(dryR * (1.0f - wetMix) + wetR * wetMix);
  }
  
  // --- FREEZE CROSSFADE ---
  if (p->freeze) {
    freezeFade += 0.0002f;
    if (freezeFade > 1.0f) freezeFade = 1.0f;
  } else {
    freezeFade -= 0.0008f;
    if (freezeFade < 0.0f) freezeFade = 0.0f;
  }

  if (writeHead < TAPE_BUFFER_GUARD) {
    mirrorDelayGuard(delayLine);
    mirrorDelayGuard(delayLineR);
  }

  writeHead++;
  if (writeHead >= bufferSize)
    writeHead = 0;
}