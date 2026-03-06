// ============================================
// ANALOG TAPE EMULATOR - MULTI-HEAD EDITION - SERIAL CLI VERSION
// ============================================
// HARDWARE: YD-ESP32-S3 (N16R8) + DAC PCM5102
// ============================================

#include "MelodyGen.h"
#include "TapeDelay.h"
#include "DisplayUI.h"
#include "ParamDefs.h"

#include "esp_heap_caps.h"
#include <Arduino.h>
#include <driver/i2s.h>
#include <esp_task_wdt.h>
#include <math.h>

// NEW: MP3 & Filesystem Headers
#include "AudioFileSourceLittleFS.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutput.h"
#include <LittleFS.h>

// ============================================
// PINOUT
// ============================================
#define I2S_BCLK 15
#define I2S_LRCK 16
#define I2S_DOUT 17
#define I2S_DIN 18
#define RGB_PIN 48

// ============================================
// AUDIO CONFIG
// ============================================
#define SAMPLE_RATE 44100
#define AUDIO_CHUNK_SIZE 128
#define I2S_DMA_BUF_LEN 512
#define I2S_DMA_BUF_COUNT 4
#define I2S_PORT I2S_NUM_0

// ============================================
// CONTROLS & GLOBALS
// ============================================
bool isBypassed = false;       // Protected by paramMutex
float masterVolume = 0.3f;     // Protected by paramMutex

// Parameters managed via struct
// === SENSIBLE DEFAULTS FOR TAPE CHARACTER ===
float p_flutterDepth = 20.0f;    // 20% - perceptible pitch wobble
float p_wowDepth = 15.0f;        // 15% - subtle pitch drift  
float p_dropoutSeverity = 8.0f;  // 8% - occasional subtle dropouts
float p_drive = 40.0f;           // 40% -> 2.0x gain (moderate saturation)
float p_noise = 30.0f;           // 30% -> ~15% audible hiss (0.015 level)
float p_flutterRate = 6.0f;      // 6 Hz - classic flutter speed
float p_wowRate = 0.8f;          // 0.8 Hz - slow wow

float p_tapeSpeed = 50.0f;       // 50% - center (6-16kHz rolloff)
float p_tapeAge = 40.0f;         // 40% - slightly worn tape
float p_headBump = 30.0f;        // 30% -> 1.5dB bass boost
float p_azimuth = 10.0f;         // 10% - slight azimuth wobble
float p_bpm = 120.0f;
bool p_headsMusical = false;
bool p_guitarFocus = false;
float p_tone = 50.0f;            // 50% - neutral

// === NEW EFFECT MODES ===
bool p_freeze = false;
bool p_reverse = false;
bool p_reverseSmear = false;
bool p_spring = false;
float p_springMix = 50.0f;       // 50% - balanced mix
float p_springDecay = 60.0f;     // 60% - medium-long decay
float p_springDamping = 45.0f;   // 45% - balanced damping

// Gate
float p_gateThreshold = 0.001f; // Default threshold

float p_mood = 50.0f;            // 50% - balanced mood
float p_rhythm = 50.0f;          // 50% - balanced rhythm

// Melody generator params
int p_waveform = 1;    // 1=Saw (richer harmonics)
int p_scale = 3;       // 3=Pentatonic (always musical)
int p_pitch = 60;      // MIDI note (C4)
bool p_enoMode = false;

bool p_delayActive = false;
float p_delayTime = 500.0f;
float p_feedback = 40.0f;        // 40% feedback
float p_mix = 50.0f;             // 50% wet/dry
int p_activeHeads = 4;
volatile bool p_triggerPlay = false;
bool p_delayUnitBpm = false;     // false=ms, true=BPM

TapeParams globalParams = {
    p_flutterDepth, p_wowDepth, p_dropoutSeverity,
    p_drive,        p_noise,    p_tapeSpeed,
    p_tapeAge,      p_headBump, p_azimuth,
    p_flutterRate,  p_wowRate,  p_delayActive,
    p_delayTime,    p_feedback, p_mix,
    p_activeHeads,  p_bpm,      p_headsMusical,
    p_guitarFocus,  p_tone,
    // New effect modes
    false,  // pingPong (not implemented yet)
    p_freeze, p_reverse, p_reverseSmear,
    p_spring, p_springDecay, p_springDamping, p_springMix
};
// Setup params
float p_masterVol = 30.0f;

enum AudioSource { SOURCE_MP3, SOURCE_SYNTH, SOURCE_I2S_IN };
volatile AudioSource p_source =
    SOURCE_SYNTH; // Start with SYNTH to avoid MP3 issues

TapeModel *tape = nullptr;
MelodyGen *melody = nullptr;
DisplayUI *displayUI = nullptr;  // OLED Display Interface
FrippEngine *fripp = nullptr;    // Frippertronics/Eno Engine

// Frippertronics mode flag (when true, use FrippEngine instead of TapeModel)
bool p_frippMode = false;

// Global Frippertronics parameters
FrippParams globalFrippParams = {
    5000.0f,   // delayTimeA (5 seconds)
    7500.0f,   // delayTimeB (7.5 seconds - different for polyrhythm)
    85.0f,     // feedbackA
    85.0f,     // feedbackB
    30.0f,     // crossFeedback
    80.0f,     // inputLevel
    70.0f,     // outputMix
    10.0f,     // driftAmount (shimmer)
    95.0f,     // decayRate
    false,     // enoMode (start in Fripp mode)
    false,     // recording (off by default)
    false      // clearRequested
};

// Bubbles mode pointers and params
BubblesEngine *bubbles = nullptr;
bool p_bubblesMode = false;

// Global Bubbles parameters (defaults from .bak file)
BubblesParams globalBubblesParams = {
    80.0f,     // bpm (slow = more artifacts)
    50.0f,     // feedback
    60.0f,     // mix
    2000.0f,   // feedbackLPF (Hz)
    true       // allpassEnabled
};

// Freeverb mode - can be used with any mode as post-processing
FreeverbEngine *freeverb = nullptr;
bool p_freeverbEnabled = false;

// Global Freeverb parameters
FreeverbParams globalFreeverbParams = {
    80.0f,     // roomSize (0-100)
    50.0f,     // damping (0-100)
    30.0f,     // wet (0-100)
    100.0f,    // dry (0-100)
    100.0f,    // width (0-100)
    false      // enabled
};

// Objetos MP3
AudioGeneratorMP3 *mp3 = nullptr;
AudioFileSourceLittleFS *file = nullptr;

SemaphoreHandle_t paramMutex;

// ============================================
// FORWARD DECLARATIONS & HELPERS
// ============================================
void dualPrintln(String s);
void dualPrint(String s);
void dualPrintf(const char *format, ...);

// Sync p_ variables to globalParams (call after changing any p_ variable)
void syncGlobalParams() {
    if (xSemaphoreTake(paramMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        globalParams.flutterDepth = p_flutterDepth;
        globalParams.wowDepth = p_wowDepth;
        globalParams.dropoutSeverity = p_dropoutSeverity;
        globalParams.drive = p_drive;
        globalParams.noise = p_noise;
        globalParams.tapeSpeed = p_tapeSpeed;
        globalParams.tapeAge = p_tapeAge;
        globalParams.headBumpAmount = p_headBump;
        globalParams.azimuthError = p_azimuth;
        globalParams.flutterRate = p_flutterRate;
        globalParams.wowRate = p_wowRate;
        globalParams.delayActive = p_delayActive;
        globalParams.delayTimeMs = p_delayTime;
        globalParams.feedback = p_feedback;
        globalParams.dryWet = p_mix;
        globalParams.activeHeads = p_activeHeads;
        globalParams.bpm = p_bpm;
        globalParams.headsMusical = p_headsMusical;
        globalParams.guitarFocus = p_guitarFocus;
        globalParams.tone = p_tone;
        globalParams.freeze = p_freeze;
        globalParams.reverse = p_reverse;
        globalParams.reverseSmear = p_reverseSmear;
        globalParams.spring = p_spring;
        globalParams.springDecay = p_springDecay;
        globalParams.springDamping = p_springDamping;
        globalParams.springMix = p_springMix;
        
        // Sync MelodyGen parameters
        if (melody) {
            melody->setWaveform((Waveform)p_waveform);
            melody->setScale((ScaleType)p_scale);
            melody->setKey(p_pitch);
            melody->setMood(p_mood / 100.0f);  // 0-100 to 0.0-1.0
            melody->setRhythm(p_rhythm / 100.0f); // 0-100 to 0.0-1.0
            // Sync Eno mode from both sources (direct or via FrippParams)
            bool useEnoMode = p_enoMode || (p_frippMode && globalFrippParams.enoMode);
            melody->setMode(useEnoMode ? MODE_ENO : MODE_NORMAL);
        }
        
        // Sync FrippEngine parameters (only if allocated)
        if (fripp && fripp->isAllocated() && p_frippMode) {
            fripp->updateParams(globalFrippParams);
        }
        
        // Sync BubblesEngine parameters (only if allocated)
        if (bubbles && bubbles->isAllocated() && p_bubblesMode) {
            bubbles->updateParams(globalBubblesParams);
        }
        
        // Sync FreeverbEngine parameters (always update when allocated, not just enabled)
        // This ensures parameters are ready when user first enables Freeverb
        if (freeverb && freeverb->isAllocated()) {
            freeverb->updateParams(globalFreeverbParams);
        }
        
        // Link Master Volume (Protected by Mutex?)
        // masterVolume is global variable used in audioTask.
        // It is atomic float read/write usually, but let's update it here.
        masterVolume = p_masterVol * 0.01f;
        
        xSemaphoreGive(paramMutex);
    }
}

// ============================================
// NOISE GATE
// ============================================
class NoiseGate {
private:
  float envelope;
  float releaseCoeff;
  float attackCoeff;
  float threshold;
  float gain;
  float attenuation; // New: Floor gain (0.0 = mute, 1.0 = no reduction)

public:
  NoiseGate() : envelope(0), threshold(0.001f), gain(1.0f), attenuation(0.0f) {
    // Fast attack (to open), Slow release (to close)
    attackCoeff = 0.01f;    // 1ms at 48k? roughly
    releaseCoeff = 0.0005f; // Slower release to avoid stutter
  }

  void setThreshold(float t) { threshold = t; }
  void setAttenuation(float a) { attenuation = a; } // 0.0 to 1.0

  // STEREO PROCESS (Linked Gate)
  void processStereo(float inL, float inR, float *outL, float *outR) {
    float absIn = fmaxf(fabsf(inL), fabsf(inR));

    // Envelope Follower
    if (absIn > envelope) {
      envelope += attackCoeff * (absIn - envelope);
    } else {
      envelope += releaseCoeff * (absIn - envelope);
    }

    // Target Gain: If strictly open -> 1.0, else -> attenuation level
    float targetGain = (envelope > threshold) ? 1.0f : attenuation;

    // Smooth Gain Transition
    if (targetGain < gain) {
      gain -= 0.0005f;
      if (gain < targetGain)
        gain = targetGain;
    } else {
      gain += 0.01f;
      if (gain > targetGain)
        gain = targetGain;
    }

    *outL = inL * gain;
    *outR = inR * gain;
  }
};

NoiseGate *gate = nullptr;

// ============================================
// CUSTOM AUDIO OUTPUT
// ============================================
class AudioOutputTapeInterceptor : public AudioOutput {
public:
  AudioOutputTapeInterceptor() {}
  virtual ~AudioOutputTapeInterceptor() {}
  virtual bool begin() override { return true; }

  virtual bool ConsumeSample(int16_t sample[2]) override {
    float input = (sample[0] + sample[1]) / 2.0f;
    input /= 32768.0f;

    float processed = input;
    // Check tape pointer before use to prevent crash
    if (!isBypassed && tape) {
      processed = tape->process(input);
    }
    processed = tanhf(processed);

    const double INT32_MAX_D = 2147483647.0;
    const float VOL_SCALE = (float)(INT32_MAX_D * 0.90f);
    int32_t outSample = (int32_t)(processed * masterVolume * VOL_SCALE);

    int32_t i2sBuffer[2];
    i2sBuffer[0] = outSample;
    i2sBuffer[1] = outSample;

    size_t bytesWritten;
    // Timeout curto para não travar
    i2s_write(I2S_PORT, i2sBuffer, sizeof(i2sBuffer), &bytesWritten,
              pdMS_TO_TICKS(50));
    return true;
  }

  // Helper to write directly to buffer from loop
  void writeStereo(float L, float R, int32_t *buffer, int index) {
    const double INT32_MAX_D = 2147483647.0;
    const float VOL_SCALE = (float)(INT32_MAX_D * 0.90f);

    int32_t sampleL = (int32_t)(L * masterVolume * VOL_SCALE);
    int32_t sampleR = (int32_t)(R * masterVolume * VOL_SCALE);

    buffer[index * 2] = sampleL;
    buffer[index * 2 + 1] = sampleR;
  }
};

AudioOutputTapeInterceptor *out = nullptr;

// ============================================
// TASKS
// ============================================
void audioTask(void *parameter) {
  // Pequeno delay inicial para deixar o core estabilizar
  vTaskDelay(pdMS_TO_TICKS(1000)); // Increased delay
  Serial.println("Audio Task: Started");

  // Attempt allocation in PSRAM
  tape = new TapeModel(SAMPLE_RATE);
  if (tape == nullptr) {
    Serial.println("CRITICAL: TapeModel allocation FAILED!");
    while (1)
      vTaskDelay(pdMS_TO_TICKS(1000)); // Stay in task but don't crash
  }
  Serial.println("Audio Task: TapeModel allocated");
  tape->updateParams(globalParams);
  tape->updateFilters();

  // Allocate FrippEngine for Frippertronics/Eno mode
  fripp = new FrippEngine(SAMPLE_RATE);
  if (fripp && fripp->isAllocated()) {
    fripp->updateParams(globalFrippParams);
    Serial.println("Audio Task: FrippEngine allocated (7s + 11s delays)");
  } else {
    Serial.println("WARN: FrippEngine allocation failed - Fripp mode unavailable");
  }
  
  // Allocate BubblesEngine for Chase Bliss-style reverse with artifacts
  bubbles = new BubblesEngine(SAMPLE_RATE);
  if (bubbles && bubbles->isAllocated()) {
    bubbles->updateParams(globalBubblesParams);
    Serial.println("Audio Task: BubblesEngine allocated (~1.5s buffer)");
  } else {
    Serial.println("WARN: BubblesEngine allocation failed - Bubbles mode unavailable");
  }
  
  // Allocate FreeverbEngine for high-quality reverb (can combine with any mode)
  freeverb = new FreeverbEngine(SAMPLE_RATE);
  if (freeverb && freeverb->isAllocated()) {
    freeverb->updateParams(globalFreeverbParams);
    Serial.println("Audio Task: FreeverbEngine allocated (~100KB)");
  } else {
    Serial.println("WARN: FreeverbEngine allocation failed - Freeverb unavailable");
  }

  melody = new MelodyGen(SAMPLE_RATE);
  if (melody) {
    melody->setBPM(p_bpm);
    melody->setMood(p_mood);
    melody->setRhythm(p_rhythm);
    Serial.println("Audio Task: MelodyGen allocated");
  } else {
    Serial.println("Audio Task: MelodyGen allocation FAILED");
  }

  // Initialize output
  out = new AudioOutputTapeInterceptor();
  if (out)
    dualPrintln("Audio Task: Output allocated");
  else
    dualPrintln("Audio Task: Output allocation FAILED");

  gate = new NoiseGate();
  if (gate)
    gate->setThreshold(p_gateThreshold);

  dualPrintln("Audio Task: Starting audio processing...");

  const double INT32_MAX_D = 2147483647.0;
  const float VOL_SCALE = (float)(INT32_MAX_D * 0.90f);

  while (true) {
    // Watchdog Reset Manual - REMOVED (Invalid context)
    // esp_task_wdt_reset();

    if (xSemaphoreTake(paramMutex, 0) == pdTRUE) {
      TapeParams localParams = globalParams;
      float localGateThresh = p_gateThreshold;
      xSemaphoreGive(paramMutex);
      if (tape) {
        tape->updateParams(localParams);
      }
      if (melody) {
        melody->setBPM(localParams.bpm);
        melody->setMood(p_mood);
        melody->setRhythm(p_rhythm);
      }
      if (gate) {
        gate->setThreshold(localGateThresh);
      }
    }

    // Simple synth processing only for now
    int32_t synthBuf[AUDIO_CHUNK_SIZE * 2];
    size_t bytesWritten;
    // Prepare input buffer for I2S
    int32_t rxBuf[AUDIO_CHUNK_SIZE * 2];
    // Initialize with zeros to avoid noise if read fails or is partial
    memset(rxBuf, 0, sizeof(rxBuf));

    if (p_source == SOURCE_I2S_IN) {
      size_t bytesRead = 0;
      // Non-blocking read (short timeout) to prevent WDT issues
      i2s_read(I2S_PORT, rxBuf, sizeof(rxBuf), &bytesRead, pdMS_TO_TICKS(10));
    }

    for (int i = 0; i < AUDIO_CHUNK_SIZE; i++) {
      float processedL = 0.0f;
      float processedR = 0.0f;

      if (p_source == SOURCE_I2S_IN) {
        // Normalize 32-bit int to -1.0 .. 1.0 float
        processedL = (float)rxBuf[i * 2] / 2147483648.0f;
        processedR = (float)rxBuf[i * 2 + 1] / 2147483648.0f;
      } else if (p_source == SOURCE_SYNTH && melody) {
        melody->nextStereo(&processedL, &processedR);
      } else if (p_source == SOURCE_MP3 && mp3 && file && out) {
        // Process MP3 through the output interceptor
        // MP3 handling is done via ConsumeSample callback
        if (mp3->isRunning()) {
          if (!mp3->loop()) {
            mp3->stop();
            // If looping desired, restart
            file->seek(0, SEEK_SET);
            mp3->begin(file, out);
          }
        } else {
          // MP3 not running, output silence
          processedL = 0.0f;
          processedR = 0.0f;
        }
        // Skip direct processing - MP3 uses callback
        continue;
      }

      // If 'gen' logic was meant to be shared, valid. But here we have
      // branches. We need to ensure passed variables to tape->processStereo are
      // valid. processStereo takes (inL, inR, &outL, &outR). We should pass
      // processedL/R as input to tape loop!

      if (!isBypassed) {
        float outL, outR;
        
        if (p_bubblesMode && bubbles && bubbles->isAllocated()) {
          // === BUBBLES MODE (Chase Bliss-style reverse with artifacts) ===
          bubbles->processStereo(processedL, processedR, &outL, &outR);
          processedL = tanhf(outL);
          processedR = tanhf(outR);
        } else if (p_frippMode && fripp && fripp->isAllocated()) {
          // === FRIPPERTRONICS / ENO MODE ===
          fripp->processStereo(processedL, processedR, &outL, &outR);
          processedL = tanhf(outL);
          processedR = tanhf(outR);
        } else if (tape) {
          // === NORMAL TAPE MODE ===
          tape->processStereo(processedL, processedR, &outL, &outR);
          processedL = tanhf(outL);
          processedR = tanhf(outR);
        } else {
          processedL = tanhf(processedL);
          processedR = tanhf(processedR);
        }
      } else {
        processedL = tanhf(processedL);
        processedR = tanhf(processedR);
      }

      // Apply Stereo Noise Gate
      if (gate) {
        gate->processStereo(processedL, processedR, &processedL, &processedR);
      }
      
      // Apply Freeverb as post-processing (combinable with any mode)
      if (p_freeverbEnabled && freeverb && freeverb->isAllocated()) {
        float reverbL, reverbR;
        freeverb->processStereo(processedL, processedR, &reverbL, &reverbR);
        processedL = reverbL;
        processedR = reverbR;
      }

      // OUTPUT (I2S)
      if (out) {
        out->writeStereo(processedL, processedR, synthBuf, i);
      }
    } // end loop

    // Write block to I2S
    // Use a longer timeout (e.g., 100ms) to ensure we block if buffer is full.
    // If we return early, we might spin.
    esp_err_t wErr = i2s_write(I2S_PORT, synthBuf, sizeof(synthBuf),
                               &bytesWritten, pdMS_TO_TICKS(50));

    // Safety check: if we are somehow running too fast or I2S fails, yield
    // But normally i2s_write blocks.
    if (wErr != ESP_OK || bytesWritten < sizeof(synthBuf)) {
      // If we failed to write full buffer, yield to avoid WDT starvation
      vTaskDelay(1);
    }
  }
}

void ledTask(void *parameter) {
  uint8_t hue = 0;
  while (true) {
    hue++;
    uint8_t r, g, b;
    if (hue < 85) {
      r = hue * 3;
      g = 255 - hue * 3;
      b = 0;
    } else if (hue < 170) {
      r = 255 - (hue - 85) * 3;
      g = 0;
      b = (hue - 85) * 3;
    } else {
      r = 0;
      g = (hue - 170) * 3;
      b = 255 - (hue - 170) * 3;
    }
    rgbLedWrite(RGB_PIN, r / 10, g / 10, b / 10);
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// ============================================
// UI TASK (OLED Display - Runs on Core 0)
// ============================================
void uiTask(void *parameter) {
  // Wait for display to be initialized
  while (displayUI == nullptr) {
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  
  Serial.println("UI Task started on Core " + String(xPortGetCoreID()));
  
  while (true) {
    displayUI->update();
    vTaskDelay(pdMS_TO_TICKS(20));  // 50 Hz update rate
  }
}

// ============================================
// CLI FUNCTIONS
// ============================================
void dualPrint(String s) {
  Serial.print(s);
  Serial0.print(s);
}
void dualPrintln(String s) {
  Serial.println(s);
  Serial0.println(s);
}
void dualPrintf(const char *format, ...) {
  char buf[256];
  va_list args;
  va_start(args, format);
  vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);
  dualPrint(String(buf));
}

// --- VISUAL HELPERS ---
void printBar(String label, float normalizedVal) {
  // normalizedVal expected 0.0 - 1.0
  int totalWidth = 20;
  int filled = (int)(normalizedVal * totalWidth);
  if (filled > totalWidth)
    filled = totalWidth;
  if (filled < 0)
    filled = 0;

  String bar = "[";
  for (int i = 0; i < totalWidth; i++) {
    if (i < filled)
      bar += "#"; // High contrast
    else
      bar += "-";
  }
  bar += "]";

  // Padding label to 12 chars
  while (label.length() < 12)
    label += " ";

  dualPrintf("%s %s %d%%\n", label.c_str(), bar.c_str(),
             (int)(normalizedVal * 100));
}

// --- PRESETS ---
void loadPreset(String name) {
  dualPrintf("\n>>> LOADING PRESET: %s <<<\n", name.c_str());
  if (name == "clean") {
    // Tape Deck in good condition
    p_tapeSpeed = 0.5f;
    p_tapeAge = 0.1f;
    p_headBump = 2.0f;
    p_drive = 1.0f; // Reduced drive
    p_noise = 0.05f;
    p_dropoutSeverity = 0.0f;
    p_flutterDepth = 0.05f;
    p_wowDepth = 0.05f;
    p_tone = 0.4f; // Darker
    p_guitarFocus = true;
  } else if (name == "lofi") {
    // Worn, slow, noisy but usable
    p_tapeSpeed = 0.3f;
    p_tapeAge = 0.6f;
    p_headBump = 4.0f;
    p_drive = 2.0f; // Reduced drive
    p_noise = 0.15f;
    p_dropoutSeverity = 0.1f;
    p_flutterDepth = 0.4f;
    p_wowDepth = 0.3f;
    p_tone = 0.3f; // Darker
  } else if (name == "dub") {
    // Space Echo style: High feed, dark
    p_tapeSpeed = 0.6f;
    p_tapeAge = 0.4f;
    p_feedback = 0.65f;
    p_activeHeads = 6; // Heads 2 & 3
    p_delayActive = true;
    p_tone = 0.25f; // Even darker
    p_drive = 4.0f; // Reduced from 5
    p_guitarFocus = true;
  } else if (name == "broken") {
    // Bad motor, bad tape (Extreme but musical)
    p_flutterRate = 8.0f;
    p_flutterDepth = 0.8f;
    p_wowRate = 3.0f;
    p_wowDepth = 0.6f;
    p_dropoutSeverity = 0.4f;
    p_noise = 0.25f;
    p_tapeAge = 0.9f;
  } else {
    dualPrintln("Unknown preset. Try: clean, lofi, dub, broken");
    return;
  }
}

void printHelp() {
  dualPrintln("\n--- CLI COMMANDS (3-Letter Codes) ---");
  dualPrintln(" [MIXER]");
  dualPrintln("  vol <0-100> : Master Volume");
  dualPrintln("  mix <0-100> : Dry/Wet Mix");
  dualPrintln("  byp <0/1>   : Bypass");
  dualPrintln("  src <0-2>   : Source (0=MP3, 1=Synth, 2=I2S)");
  dualPrintln(" [TAPE ENGINE]");
  dualPrintln("  dly <ms>    : Delay Time (10-2000)");
  dualPrintln("  fbk <0-100> : Feedback");
  dualPrintln("  hds <1-7>   : Active Heads");
  dualPrintln("  mus <0/1>   : Head Mode (0=Free, 1=Musical)");
  dualPrintln("  mod <0/1>   : Engine Mode (0=Saturator, 1=Delay)");
  dualPrintln("  tps <0-100> : Tape Speed (Varispeed)");
  dualPrintln("  tpa <0-100> : Tape Age (Filter)");
  dualPrintln("  drv <0-100> : Drive / Saturation");
  dualPrintln("  nlv <0-100> : Noise Level");
  dualPrintln("  ngt <0-100> : Gate Threshold");
  dualPrintln("  red <0-100> : Gate Reduction % (0=None, 100=Mute)");
  dualPrintln("  hbp <0-100> : Head Bump");
  dualPrintln("  azm <0-100> : Azimuth (50=Center)");
  dualPrintln(" [COLOR]");
  dualPrintln("  gfc <0/1>   : Guitar Focus (BPF)");
  dualPrintln("  ton <0-100> : Tone Control");
  dualPrintln(" [MODULATION]");
  dualPrintln("  dps <0-100> : Dropouts");
  dualPrintln("  ftd/ftr     : Flutter Depth/Rate");
  dualPrintln("  wwd/wwr     : Wow Depth/Rate");
  dualPrintln(" [MELODY GEN]");
  dualPrintln("  wvf <0-3>   : Waveform (Sine, Saw, Tri, Sqr)");
  dualPrintln("  scl <0-4>   : Scale");
  dualPrintln("  ptc <midi>  : Root Key");
  dualPrintln("  moo <0-100> : Mood");
  dualPrintln("  rtm <0-100> : Rhythm Density");
  dualPrintln(" [EFFECT MODES]");
  dualPrintln("  frz <0/1>   : Freeze/Hold");
  dualPrintln("  rev <0/1>   : Reverse Delay");
  dualPrintln("  rvb <0/1>   : Reverse Reverb");
  dualPrintln("  spr <0/1>   : Spring Reverb");
  dualPrintln("  spd <0-100> : Spring Decay");
  dualPrintln("  spf <0-100> : Spring Damping");
  dualPrintln("");
  dualPrintln("  load <preset> : clean, lofi, dub, broken");
  dualPrintln("  list          : Show Dashboard");
}

// --- DASHBOARD STATUS ---
void printStatus() {
  dualPrintln("\n================= TAPE DSP DASHBOARD ==================");

  // MIXER SECTION
  dualPrintln(" [ MIXER ]");
  printBar(" Volume", masterVolume);
  printBar(" Dry/Wet", p_mix);
  dualPrintf(" Source      : %s\n", (p_source == 0)   ? "MP3"
                                    : (p_source == 1) ? "SYNTH"
                                                      : "I2S");
  dualPrintf(" Bypass      : %s\n", isBypassed ? "YES" : "NO");

  // DELAY SECTION
  dualPrintln("\n [ TAPE ENGINE ]");
  char dBuf[32];
  sprintf(dBuf, "%.0f ms", p_delayTime);
  dualPrintf(" Delay Time  : %-12s ", dBuf);
  printBar("Feedback", p_feedback);
  printBar(" Speed", p_tapeSpeed); // Normalized
  printBar(" Age", p_tapeAge);
  dualPrintf(" Heads       : %d (%s)\n", p_activeHeads,
             p_headsMusical ? "Musical" : "Free");

  // COLOR SECTION
  dualPrintln("\n [ COLOR & TONE ]");
  printBar(" Drive", (p_drive - 1.0f) / 9.0f); // Normalize 1-10 -> 0-1
  printBar(" Tone", p_tone);
  printBar(" Noise", p_noise * 2.0f); // Max 0.5 -> 1.0
  dualPrintf(" Guitar Mode : %s\n",
             p_guitarFocus ? "ON (Filtered)" : "OFF (Full Range)");

  // MODULATION SECTION
  dualPrintln("\n [ MODULATION ]");
  printBar(" Wow", p_wowDepth / 2.0f);
  printBar(" Flutter", p_flutterDepth / 2.0f);
  printBar(" Dropouts", p_dropoutSeverity);

  dualPrintln("=======================================================");
}

// Helper to process a line
void executeCommand(String line) {
  if (line.length() == 0)
    return;

  int spaceIdx = line.indexOf(' ');
  String cmd = line;
  String valStr = "";

  if (spaceIdx > 0) {
    cmd = line.substring(0, spaceIdx);
    valStr = line.substring(spaceIdx + 1);
  }

  float val = valStr.toFloat();

  if (xSemaphoreTake(paramMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    auto map100 = [](float v, float minV, float maxV) -> float {
      float norm = constrain(v, 0.0f, 100.0f) / 100.0f;
      return minV + (norm * (maxV - minV));
    };

    // --- MIXER ---
    if (cmd == "vol")
      masterVolume = map100(val, 0.0f, 1.0f);
    else if (cmd == "mix")
      p_mix = map100(val, 0.0f, 1.0f);
    else if (cmd == "byp") {
      isBypassed = (val > 0);
      dualPrintf("Bypass: %s\n", isBypassed ? "ON" : "OFF");
    } else if (cmd == "src") {
      p_source = (AudioSource)constrain((int)val, 0, 2);
      dualPrintf("Source: %d (0=MP3, 1=SYNTH, 2=I2S)\n", p_source);
    }

    // --- TAPE ENGINE ---
    else if (cmd == "dly")
      p_delayTime = constrain(val, 10.0f, 2000.0f);
    else if (cmd == "fbk")
      p_feedback = map100(val, 0.0f, 1.1f);
    else if (cmd == "hds")
      p_activeHeads = constrain((int)val, 1, 7);
    else if (cmd == "tps")
      p_tapeSpeed = map100(val, 0.0f, 1.0f);
    else if (cmd == "tpa")
      p_tapeAge = map100(val, 0.0f, 1.0f);
    else if (cmd == "drv")
      p_drive = map100(val, 1.0f, 10.0f);
    else if (cmd == "nlv")
      p_noise = map100(val, 0.0f, 0.08f); // Max 0.08 (-22dB) for subtle hiss
    else if (cmd == "ngt")
      p_gateThreshold = map100(val, 0.0f, 0.1f);
    else if (cmd == "hbp")
      p_headBump = map100(val, 0.0f, 10.0f);
    else if (cmd == "azm")
      p_azimuth = map100(val, -1.0f, 1.0f);
    else if (cmd == "red") {
      // 0-100 where 100 = Full Silence (0.0 atten), 0 = No Reduction (1.0
      // atten) User likely wants "Reduction Amount". Let's map 0-100 to
      // Gain 1.0 - 0.0 red 0 -> Gain 1.0 (No Effect) red 100 -> Gain 0.0 (Full
      // Mute)
      float attenGain = 1.0f - map100(val, 0.0f, 1.0f);
      if (gate)
        gate->setAttenuation(attenGain);
      dualPrintf("Reducer: -%.0f%%\n", val);
    } else if (cmd == "mus") {
      p_headsMusical = (val > 0);
      dualPrintf("Head Mode: %s\n",
                 p_headsMusical ? "MUSICAL (Triplets)" : "FREE (Speed)");
    } else if (cmd == "mod") {
      p_delayActive = (val > 0);
      dualPrintf("Engine Mode: %s\n",
                 p_delayActive ? "TAPE DELAY" : "TAPE SATURATOR");
    }

    // --- COLOR ---
    else if (cmd == "gfc")
      p_guitarFocus = (val > 0);
    else if (cmd == "ton")
      p_tone = map100(val, 0.0f, 1.0f);

    // --- MODULATION ---
    else if (cmd == "dps")
      p_dropoutSeverity = map100(val, 0.0f, 1.0f);
    else if (cmd == "ftd")
      p_flutterDepth = map100(val, 0.0f, 2.0f);
    else if (cmd == "ftr")
      p_flutterRate = map100(val, 0.1f, 20.0f);
    else if (cmd == "wwd")
      p_wowDepth = map100(val, 0.0f, 2.0f);
    else if (cmd == "wwr")
      p_wowRate = map100(val, 0.1f, 5.0f);

    // --- NEW EFFECT MODES ---
    else if (cmd == "frz") {
      p_freeze = (val > 0);
      dualPrintf("Freeze: %s\n", p_freeze ? "ON" : "OFF");
    }
    else if (cmd == "rev") {
      p_reverse = (val > 0);
      dualPrintf("Reverse: %s\n", p_reverse ? "ON" : "OFF");
    }
    else if (cmd == "rvb") {
      p_reverseSmear = (val > 0);
      p_reverse = p_reverseSmear;  // ReverseSmear implies reverse
      dualPrintf("Reverse Reverb: %s\n", p_reverseSmear ? "ON" : "OFF");
    }
    // --- FREEVERB COMMANDS ---
    else if (cmd == "frv") {
      p_freeverbEnabled = (val > 0);
      globalFreeverbParams.enabled = p_freeverbEnabled;
      dualPrintf("Freeverb: %s\n", p_freeverbEnabled ? "ON" : "OFF");
      if (p_freeverbEnabled && freeverb) freeverb->updateParams(globalFreeverbParams);
    }
    else if (cmd == "frs") {
        globalFreeverbParams.roomSize = map100(val, 0.0f, 100.0f);
        if (freeverb) freeverb->setRoomSize(globalFreeverbParams.roomSize);
        printBar("Room Size", globalFreeverbParams.roomSize / 100.0f);
    }
    else if (cmd == "frd") {
        globalFreeverbParams.damping = map100(val, 0.0f, 100.0f);
        if (freeverb) freeverb->setDamping(globalFreeverbParams.damping);
        printBar("Damping", globalFreeverbParams.damping / 100.0f);
    }
    else if (cmd == "frw") {
        globalFreeverbParams.wet = map100(val, 0.0f, 100.0f);
        if (freeverb) freeverb->setWet(globalFreeverbParams.wet);
        printBar("Verb Wet", globalFreeverbParams.wet / 100.0f);
    }
    else if (cmd == "fry") {
        globalFreeverbParams.dry = map100(val, 0.0f, 100.0f);
        if (freeverb) freeverb->setDry(globalFreeverbParams.dry);
        printBar("Verb Dry", globalFreeverbParams.dry / 100.0f);
    }
    else if (cmd == "frh") {
        globalFreeverbParams.width = map100(val, 0.0f, 100.0f);
        if (freeverb) freeverb->setWidth(globalFreeverbParams.width);
        printBar("Verb Width", globalFreeverbParams.width / 100.0f);
    }
    else if (cmd == "spr") {
      p_spring = (val > 0);
      dualPrintf("Spring Reverb: %s\n", p_spring ? "ON" : "OFF");
    }
    else if (cmd == "spd") {
      p_springDecay = map100(val, 0.0f, 1.0f);
      printBar("Spring Decay", p_springDecay);
    }
    else if (cmd == "spf") {
      p_springDamping = map100(val, 0.0f, 1.0f);
      printBar("Spring Damp", p_springDamping);
    }

    // --- SYSTEM ---
    else if (cmd == "bmp")
      p_bpm = constrain(val, 30.0f, 300.0f);
    else if (cmd == "load")
      loadPreset(valStr);
    else if (cmd == "list")
      printStatus();
    else if (cmd == "?")
      printHelp();

    // --- MELODY GEN ---
    else if (melody) {
      if (cmd == "wvf") { // 0=SINE, 1=SAW, 2=TRI, 3=SQUARE
        melody->setWaveform((Waveform)constrain((int)val, 0, 3));
        dualPrintf("Waveform: %d\n", (int)val);
      } else if (cmd == "ptc") { // Pitch / Key (MIDI)
        melody->setKey((int)val);
        dualPrintf("Key: %d\n", (int)val);
      } else if (cmd == "scl") { // Scale 0-4
        melody->setScale((ScaleType)constrain((int)val, 0, 4));
        dualPrintf("Scale: %d\n", (int)val);
      } else if (cmd == "moo") { // Mood 0-100
        p_mood = map100(val, 0.0f, 1.0f);
        melody->setMood(p_mood);
      } else if (cmd == "rtm") { // Rhythm 0-100
        p_rhythm = map100(val, 0.0f, 1.0f);
        melody->setRhythm(p_rhythm);
      } else if (cmd == "eno") { // ENO MODE
        bool isEno = (val > 0.5f);
        melody->setMode(isEno ? MODE_ENO : MODE_NORMAL);
        if (isEno) {
          p_feedback = 0.85f;
          p_delayTime = 800.0f;
          p_mix = 0.60f;
          p_tapeAge = 0.7f; // Darker
          dualPrintln("Mode: ENO ACTIVATED");
        } else {
          dualPrintln("Mode: NORMAL");
        }
      }
    }

    // Update global params
    globalParams.flutterDepth = p_flutterDepth;
    globalParams.wowDepth = p_wowDepth;
    globalParams.dropoutSeverity = p_dropoutSeverity;
    globalParams.drive = p_drive;
    globalParams.noise = p_noise;
    globalParams.tapeSpeed = p_tapeSpeed;
    globalParams.tapeAge = p_tapeAge;
    globalParams.headBumpAmount = p_headBump;
    globalParams.azimuthError = p_azimuth;
    globalParams.flutterRate = p_flutterRate;
    globalParams.wowRate = p_wowRate;
    globalParams.delayActive = p_delayActive;
    globalParams.delayTimeMs = p_delayTime;
    globalParams.feedback = p_feedback;
    globalParams.dryWet = p_mix;
    globalParams.activeHeads = p_activeHeads;
    globalParams.bpm = p_bpm;
    globalParams.headsMusical = p_headsMusical;
    globalParams.guitarFocus = p_guitarFocus;
    globalParams.tone = p_tone;
    
    // New effect modes
    globalParams.freeze = p_freeze;
    globalParams.reverse = p_reverse;
    globalParams.reverseSmear = p_reverseSmear;
    globalParams.spring = p_spring;
    globalParams.springDecay = p_springDecay;
    globalParams.springDamping = p_springDamping;

    xSemaphoreGive(paramMutex);

    // Visual Feedback
    if (cmd != "?" && cmd != "stat" && cmd != "list" && cmd != "src" &&
        cmd != "load" && cmd != "byp") {
      if (cmd == "vol")
        printBar("Volume", masterVolume);
      else if (cmd == "mix")
        printBar("Mix", p_mix);
      else if (cmd == "fbk")
        printBar("Feedback", p_feedback);
      else if (cmd == "tps")
        printBar("Speed", p_tapeSpeed);
      else if (cmd == "tpa")
        printBar("Age", p_tapeAge);
      else if (cmd == "ton")
        printBar("Tone", p_tone);
      else if (cmd == "drv")
        printBar("Drive", (p_drive - 1.0f) / 9.0f);
      else if (cmd == "dly")
        dualPrintf("Delay: %.0f ms\n", p_delayTime);
      else
        dualPrintf("> %s updated\n", cmd.c_str());
    }
  }
}

void processSerialInput() {
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    executeCommand(line);
  }
  if (Serial0.available()) {
    String line = Serial0.readStringUntil('\n');
    line.trim();
    executeCommand(line);
  }
}

// ============================================
// SETUP
// ============================================
void setup() {
  // Atraso de 3 segundos para estabilização elétrica
  delay(3000);
  setCpuFrequencyMhz(240);

  // 1. MUTEX
  paramMutex = xSemaphoreCreateMutex();
  if (paramMutex == NULL) {
    // If we can't print yet, we are in trouble, but let's try
    // But Serial is not init...
    while (1)
      delay(1000);
  }

  // 2. SERIAL (UART)
  Serial.begin(115200);  // Native USB
  Serial0.begin(115200); // Hardware UART (Pins 43/44)

  dualPrintln("\n\n=== BOOT START ===");
  dualPrintln("SERIAL CLI MODE ACTIVE");

  dualPrintf("PSRAM Size: %d bytes\n", ESP.getPsramSize());

  // Try to mount LittleFS, but don't fail if corrupted
  if (!LittleFS.begin()) {
    dualPrintln("FS Fail - continuing without filesystem");
  } else {
    dualPrintln("FS Mounted");
  }

  // 3. I2S CONFIG (TX ONLY)
  i2s_config_t i2s_config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX),
      .sample_rate = SAMPLE_RATE,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
      .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = I2S_DMA_BUF_COUNT,
      .dma_buf_len = I2S_DMA_BUF_LEN,
      .use_apll = false};
  i2s_pin_config_t pin_config = {.bck_io_num = I2S_BCLK,
                                 .ws_io_num = I2S_LRCK,
                                 .data_out_num = I2S_DOUT,
                                 .data_in_num = I2S_DIN};
  
  // Install I2S driver with error handling
  esp_err_t i2s_err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  if (i2s_err != ESP_OK) {
    dualPrintf("CRITICAL: I2S driver install failed: %d\n", i2s_err);
    // Retry once after small delay
    delay(100);
    i2s_err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
    if (i2s_err != ESP_OK) {
      dualPrintln("I2S FAILED - Audio will not work!");
    }
  }
  
  i2s_err = i2s_set_pin(I2S_PORT, &pin_config);
  if (i2s_err != ESP_OK) {
    dualPrintf("WARN: I2S pin config failed: %d\n", i2s_err);
  }
  
  i2s_zero_dma_buffer(I2S_PORT);

  Serial.println("I2S Configured. Starting Tasks...");

  // Create tasks with proper stack sizes
  BaseType_t ret =
      xTaskCreatePinnedToCore(audioTask, "Audio", 32768, NULL, 5, NULL, 1);
  if (ret == pdPASS) {
    Serial.println("Audio task created successfully");
  } else {
    Serial.println("Audio task creation failed");
  }

  ret = xTaskCreatePinnedToCore(ledTask, "LED", 4096, NULL, 1, NULL, 0);
  if (ret == pdPASS) {
    Serial.println("LED task created successfully");
  } else {
    Serial.println("LED task creation failed");
  }

  // Initialize parameter pointers for UI
  initParamPointers();
  
  // Initialize OLED Display
  displayUI = new DisplayUI();
  if (displayUI->begin()) {
    Serial.println("Display initialized OK");
    
    // Create UI task on Core 0 (Audio runs on Core 1)
    ret = xTaskCreatePinnedToCore(uiTask, "UI", 8192, NULL, 2, NULL, 0);
    if (ret == pdPASS) {
      Serial.println("UI task created on Core 0");
    } else {
      Serial.println("UI task creation failed");
    }
  } else {
    Serial.println("Display init FAILED - check wiring!");
    Serial.println("  SDA: GPIO 1");
    Serial.println("  SCL: GPIO 2");
  }

  printHelp();
  Serial.println("=== SYSTEM READY ===");
}

void loop() {
  processSerialInput();
  vTaskDelay(pdMS_TO_TICKS(10));
}
