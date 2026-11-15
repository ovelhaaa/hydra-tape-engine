// ============================================
// ANALOG TAPE EMULATOR - MULTI-HEAD EDITION - SERIAL CLI VERSION
// ============================================
// HARDWARE: YD-ESP32-S3 (N16R8) + DAC PCM5102
// ============================================

#include "MelodyGen.h"
#include "TapeDelay.h"
#include "hydra_dsp_esp32_adapter.h"
#include "DisplayUI.h"
#include "ParamDefs.h"

#include "esp_cpu.h"
#include "esp_heap_caps.h"
#include <Arduino.h>
#include <driver/i2s.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>
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
#define SAMPLE_RATE 44100  // Hardware path runs at 44.1 kHz; web/WASM uses the browser AudioContext rate (normally 48 kHz).
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
bool p_pingPong = false;
float p_springMix = 50.0f;       // 50% - balanced mix
float p_springDecay = 60.0f;     // 60% - medium-long decay
float p_springDamping = 45.0f;   // 45% - balanced damping

// === ACCIDENTAL WARP V1.3 ===
bool p_warpActive = false;
float p_warpSpeed = 1.0f;        // 1.0 = octave up (+12st, v_eff = 2.0x)
float p_warpGrainMs = 80.0f;     // 80ms default window
float p_warpStereo = 0.5f;       // 50% stereo phase offset
float p_warpFeedback = 0.0f;     // 0% feedback (pure single pass)
int p_warpMode = 0;              // 0 = 2-Taps Musical Hermite

// === DSP PERFORMANCE TELEMETRY ===
constexpr double kDspBlockBudgetUs = 1000000.0 * AUDIO_CHUNK_SIZE / SAMPLE_RATE;
struct DspPerformance {
  uint32_t blocks = 0;
  int64_t totalUs = 0;
  int64_t lastUs = 0;
  int64_t peakUs = 0;
  uint32_t overruns = 0;
  uint32_t i2sWriteFailures = 0;
  float budgetLoadEma = 0.0f;
};
DspPerformance g_dsp_perf;
portMUX_TYPE g_dsp_perf_mux = portMUX_INITIALIZER_UNLOCKED;

// === PASS A1 PROFILING TELEMETRY ===
constexpr size_t kBenchHistBins = 8192;
enum BenchState : uint8_t { BENCH_IDLE = 0, BENCH_WARMUP = 1, BENCH_RUNNING = 2, BENCH_DONE = 3 };
struct BlockBenchState {
  volatile BenchState state = BENCH_IDLE;
  uint32_t warmupRemain = 0;
  uint32_t targetBlocks = 0;
  uint32_t collected = 0;
  int64_t sumUs = 0;
  int64_t minUs = INT64_MAX;
  int64_t peakUs = 0;
  uint32_t overruns = 0;
  uint32_t startI2sFail = 0;
  uint32_t endI2sFail = 0;
  uint32_t startHeap = 0;
  uint32_t endHeap = 0;
  uint32_t minFreeHeap = 0;
  uint32_t startPsram = 0;
  uint32_t endPsram = 0;
  int64_t startWallUs = 0;
  int64_t endWallUs = 0;
  int64_t lastParamUpdateUs = 0;
  int64_t maxParamUpdateUs = 0;
  uint32_t* hist = nullptr;
};
BlockBenchState g_bench;

enum StageProfState : uint8_t { STAGE_IDLE = 0, STAGE_WARMUP = 1, STAGE_RUNNING = 2, STAGE_DONE = 3 };
struct StageProfileState {
  volatile StageProfState state = STAGE_IDLE;
  uint32_t warmupRemain = 0;
  uint32_t targetBlocks = 0;
  uint32_t collected = 0;
  uint64_t cycSource = 0;
  uint64_t cycDsp = 0;
  uint64_t cycExtTanh = 0;
  uint64_t cycGate = 0;
  uint64_t cycFreeverb = 0;
  uint64_t cycPcmOut = 0;
  uint64_t cycTotal = 0;
};
StageProfileState g_stage_prof;
volatile bool g_membench_req = false;
volatile bool g_membench_done = false;
volatile bool p_genActive = true;

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

volatile bool dspParamsDirty = true;

static hydra_dsp_params makeTapeParams() {
  hydra_dsp_params params{};
  params.flutterDepth = p_flutterDepth;
  params.wowDepth = p_wowDepth;
  params.dropoutSeverity = p_dropoutSeverity;
  params.drive = p_drive;
  params.noise = p_noise;
  params.tapeSpeed = p_tapeSpeed;
  params.tapeAge = p_tapeAge;
  params.headBumpAmount = p_headBump;
  params.azimuthError = p_azimuth;
  params.flutterRate = p_flutterRate;
  params.wowRate = p_wowRate;
  params.delayActive = p_delayActive;
  params.delayTimeMs = p_delayTime;
  params.feedback = p_feedback;
  params.dryWet = p_mix;
  // The ESP32 UI uses one control for both mix parameters.
  params.delayWet = p_mix;
  params.activeHeads = p_activeHeads;
  params.bpm = p_bpm;
  params.headsMusical = p_headsMusical;
  params.guitarFocus = p_guitarFocus;
  params.tone = p_tone;
  params.pingPong = p_pingPong;
  params.freeze = p_freeze;
  params.reverse = p_reverse;
  params.reverseSmear = p_reverseSmear;
  params.spring = p_spring;
  params.springDecay = p_springDecay;
  params.springDamping = p_springDamping;
  params.springMix = p_springMix;
  params.warpActive = p_warpActive ? 1.0f : 0.0f;
  params.warpSpeed = p_warpSpeed;
  params.warpGrainMs = p_warpGrainMs;
  params.warpStereo = p_warpStereo;
  params.warpFeedback = p_warpFeedback;
  params.warpMode = static_cast<float>(p_warpMode);
  return params;
}

hydra_dsp_params globalParams = makeTapeParams();
// Setup params
float p_masterVol = 30.0f;

enum AudioSource { SOURCE_MP3, SOURCE_SYNTH, SOURCE_I2S_IN };
volatile AudioSource p_source =
    SOURCE_SYNTH; // Start with SYNTH to avoid MP3 issues

HydraDspEsp32Adapter *tape = nullptr;
MelodyGen *melody = nullptr;
DisplayUI *displayUI = nullptr;  // OLED Display Interface
FrippEngine *fripp = nullptr;    // Frippertronics/Eno Engine

// Frippertronics mode flag (when true, route audio through FrippEngine)
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
        globalParams = makeTapeParams();
        
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
        dspParamsDirty = true;
        
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
  tape = new HydraDspEsp32Adapter(SAMPLE_RATE);
  if (tape == nullptr || !tape->isValid()) {
    Serial.println("CRITICAL: Hydra DSP adapter initialization FAILED!");
    while (1)
      vTaskDelay(pdMS_TO_TICKS(1000)); // Stay in task but don't crash
  }
  Serial.println("Audio Task: Hydra DSP adapter allocated");
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
    melody->setMood(p_mood / 100.0f);
    melody->setRhythm(p_rhythm / 100.0f);
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

  g_bench.hist = (uint32_t*)heap_caps_malloc(kBenchHistBins * sizeof(uint32_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!g_bench.hist) {
    g_bench.hist = (uint32_t*)heap_caps_malloc(kBenchHistBins * sizeof(uint32_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }
  dualPrintf("Audio Task: ptrs tape=%p freeverb=%p melody=%p gate=%p hist=%p\n",
             (void*)tape, (void*)freeverb, (void*)melody, (void*)gate, (void*)g_bench.hist);

  const double INT32_MAX_D = 2147483647.0;
  const float VOL_SCALE = (float)(INT32_MAX_D * 0.90f);

  while (true) {
    // Watchdog Reset Manual - REMOVED (Invalid context)
    // esp_task_wdt_reset();

    if (dspParamsDirty && xSemaphoreTake(paramMutex, 0) == pdTRUE) {
      hydra_dsp_params localParams = globalParams;
      float localGateThresh = p_gateThreshold;
      float localMood = p_mood;
      float localRhythm = p_rhythm;
      dspParamsDirty = false;
      xSemaphoreGive(paramMutex);
      const int64_t updStartUs = esp_timer_get_time();
      if (tape) {
        tape->updateParams(localParams);
      }
      if (melody) {
        melody->setBPM(localParams.bpm);
        melody->setMood(localMood / 100.0f);
        melody->setRhythm(localRhythm / 100.0f);
      }
      if (gate) {
        gate->setThreshold(localGateThresh);
      }
      const int64_t updUs = esp_timer_get_time() - updStartUs;
      g_bench.lastParamUpdateUs = updUs;
      if (updUs > g_bench.maxParamUpdateUs) g_bench.maxParamUpdateUs = updUs;
    }

    if (g_membench_req) {
      g_membench_req = false;
      constexpr int kLen = 1024;
      constexpr int kIters = 2048;
      float* sramBuf = (float*)heap_caps_malloc((kLen + 4) * sizeof(float), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
      float* psramBuf = (float*)heap_caps_malloc((kLen + 4) * sizeof(float), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      if (sramBuf && psramBuf) {
        for (int k = 0; k < kLen + 4; ++k) {
          float v = sinf(0.01f * k);
          sramBuf[k] = v;
          psramBuf[k] = v;
        }
        auto hermiteBench = [](const float* b, bool strided) -> uint32_t {
          volatile float sink = 0.0f;
          uint32_t idx = 10;
          const uint32_t c0 = esp_cpu_get_cycle_count();
          for (int k = 0; k < kIters; ++k) {
            idx = strided ? ((idx + 271u) & 1023u) : ((idx + 1u) & 1023u);
            uint32_t r = (idx < 2u) ? 2u : (idx > 1020u ? 1020u : idx);
            float f = 0.37f;
            float ym1 = b[r - 1], y0 = b[r], y1 = b[r + 1], y2 = b[r + 2];
            float c0h = y0, c1 = 0.5f * (y1 - ym1);
            float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
            float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
            sink += ((c3 * f + c2) * f + c1) * f + c0h;
          }
          return esp_cpu_get_cycle_count() - c0;
        };
        volatile float vx = 0.4321f, vy = 1.732f, acc = 0.0f;
        volatile int32_t vi = 123456, vj = 88200, vacc = 0;
        uint32_t cycSramSeq = hermiteBench(sramBuf, false);
        uint32_t cycSramStr = hermiteBench(sramBuf, true);
        uint32_t cycPsramSeq = hermiteBench(psramBuf, false);
        uint32_t cycPsramStr = hermiteBench(psramBuf, true);

        constexpr uint32_t kBigLen = 192000;
        float* psramBig = (float*)heap_caps_malloc((kBigLen + 8) * sizeof(float), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        uint32_t cycBigSeq = 0, cycBigStr = 0, cycBig4TapWr = 0;
        volatile float bigSink = 0.0f;
        if (psramBig) {
          for (uint32_t k = 0; k < kBigLen + 8; ++k) psramBig[k] = (float)(k & 255) * 0.001f;
          // Sequential Hermite4 over 192k PSRAM (forces continuous cache-line misses across >DCache)
          uint32_t wh = 25000;
          uint32_t c0b = esp_cpu_get_cycle_count();
          for (int k = 0; k < kIters; ++k) {
            uint32_t r = wh + 2u; if (r >= kBigLen) r -= kBigLen;
            float f = 0.37f;
            float ym1 = psramBig[r - 1], y0 = psramBig[r], y1 = psramBig[r + 1], y2 = psramBig[r + 2];
            float c0h = y0, c1 = 0.5f * (y1 - ym1);
            float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
            float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
            bigSink += ((c3 * f + c2) * f + c1) * f + c0h;
            if (++wh >= kBigLen) wh = 0;
          }
          cycBigSeq = esp_cpu_get_cycle_count() - c0b;
          // Far-strided Hermite4 over 192k PSRAM (+5003 stride defeats cache spatial locality completely)
          wh = 25000;
          c0b = esp_cpu_get_cycle_count();
          for (int k = 0; k < kIters; ++k) {
            wh += 5003u; if (wh >= kBigLen) wh -= kBigLen;
            uint32_t r = (wh < 2u) ? 2u : wh;
            float f = 0.37f;
            float ym1 = psramBig[r - 1], y0 = psramBig[r], y1 = psramBig[r + 1], y2 = psramBig[r + 2];
            float c0h = y0, c1 = 0.5f * (y1 - ym1);
            float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
            float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
            bigSink += ((c3 * f + c2) * f + c1) * f + c0h;
          }
          cycBigStr = esp_cpu_get_cycle_count() - c0b;
          // 4 sequential-advancing heads + 1 write over 192k PSRAM (TapeCore simulation: 200ms, 165ms, 330ms, 500ms)
          wh = 30000;
          c0b = esp_cpu_get_cycle_count();
          for (int k = 0; k < kIters; ++k) {
            static const uint32_t offs[4] = {8820, 7276, 14553, 22050};
            float sum = 0.0f;
            for (int t = 0; t < 4; ++t) {
              uint32_t r = (wh >= offs[t]) ? (wh - offs[t]) : (wh + kBigLen - offs[t]);
              if (r < 1u) r = 1u; else if (r >= kBigLen - 2u) r = kBigLen - 3u;
              float ym1 = psramBig[r - 1], y0 = psramBig[r], y1 = psramBig[r + 1], y2 = psramBig[r + 2];
              float f = 0.37f;
              float c0h = y0, c1 = 0.5f * (y1 - ym1);
              float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
              float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
              sum += ((c3 * f + c2) * f + c1) * f + c0h;
            }
            psramBig[wh] = sum * 0.1f;
            if (++wh >= kBigLen) wh = 0;
          }
          cycBig4TapWr = esp_cpu_get_cycle_count() - c0b;
          heap_caps_free(psramBig);
        }
        uint32_t t0 = esp_cpu_get_cycle_count();
        for (int k = 0; k < kIters; ++k) { acc += tanhf(vx + 0.0001f * k); }
        uint32_t cycTanh = esp_cpu_get_cycle_count() - t0;
        t0 = esp_cpu_get_cycle_count();
        for (int k = 0; k < kIters; ++k) { acc += sinf(vx + 0.001f * k); }
        uint32_t cycSin = esp_cpu_get_cycle_count() - t0;
        t0 = esp_cpu_get_cycle_count();
        for (int k = 0; k < kIters; ++k) { acc += expf(-vx - 0.0001f * k); }
        uint32_t cycExp = esp_cpu_get_cycle_count() - t0;
        t0 = esp_cpu_get_cycle_count();
        for (int k = 0; k < kIters; ++k) { acc += powf(10.0f, (k & 15) * 0.025f); }
        uint32_t cycPow = esp_cpu_get_cycle_count() - t0;
        t0 = esp_cpu_get_cycle_count();
        for (int k = 0; k < kIters; ++k) { acc += floorf(vx + 0.01f * k); }
        uint32_t cycFloor = esp_cpu_get_cycle_count() - t0;
        t0 = esp_cpu_get_cycle_count();
        for (int k = 0; k < kIters; ++k) { acc += fmodf(vx + 0.13f * k, 1.0f); }
        uint32_t cycFmod = esp_cpu_get_cycle_count() - t0;
        t0 = esp_cpu_get_cycle_count();
        for (int k = 0; k < kIters; ++k) { acc += (vx + 0.001f * k) / vy; }
        uint32_t cycFdiv = esp_cpu_get_cycle_count() - t0;
        t0 = esp_cpu_get_cycle_count();
        for (int k = 0; k < kIters; ++k) { vacc += (vi + k) % vj; }
        uint32_t cycImod = esp_cpu_get_cycle_count() - t0;
        dualPrintf("\n=== MEM & MATH MICROBENCH (%d iters @ 240 MHz) ===\n", kIters);
        dualPrintf("SRAM  Hermite4 Seq : %.1f cyc/call (%.3f us/128)\n", (float)cycSramSeq / kIters, (float)cycSramSeq / kIters * 128.0f / 240.0f);
        dualPrintf("SRAM  Hermite4 Str : %.1f cyc/call (%.3f us/128)\n", (float)cycSramStr / kIters, (float)cycSramStr / kIters * 128.0f / 240.0f);
        dualPrintf("PSRAM Hermite4 Seq : %.1f cyc/call (%.3f us/128)\n", (float)cycPsramSeq / kIters, (float)cycPsramSeq / kIters * 128.0f / 240.0f);
        dualPrintf("PSRAM Hermite4 Str : %.1f cyc/call (%.3f us/128)\n", (float)cycPsramStr / kIters, (float)cycPsramStr / kIters * 128.0f / 240.0f);
        dualPrintf("PSRAM 192k HermSeq : %.1f cyc/call (%.3f us/128) [big=%.3f]\n", (float)cycBigSeq / kIters, (float)cycBigSeq / kIters * 128.0f / 240.0f, (double)bigSink);
        dualPrintf("PSRAM 192k HermStr : %.1f cyc/call (%.3f us/128)\n", (float)cycBigStr / kIters, (float)cycBigStr / kIters * 128.0f / 240.0f);
        dualPrintf("PSRAM 192k 4Tap+Wr : %.1f cyc/step (%.3f us/128)\n", (float)cycBig4TapWr / kIters, (float)cycBig4TapWr / kIters * 128.0f / 240.0f);
        dualPrintf("tanhf()            : %.1f cyc/call (%.3f us/128)\n", (float)cycTanh / kIters, (float)cycTanh / kIters * 128.0f / 240.0f);
        dualPrintf("sinf()             : %.1f cyc/call (%.3f us/128)\n", (float)cycSin / kIters, (float)cycSin / kIters * 128.0f / 240.0f);
        dualPrintf("expf()             : %.1f cyc/call (%.3f us/128)\n", (float)cycExp / kIters, (float)cycExp / kIters * 128.0f / 240.0f);
        dualPrintf("powf()             : %.1f cyc/call (%.3f us/128)\n", (float)cycPow / kIters, (float)cycPow / kIters * 128.0f / 240.0f);
        dualPrintf("floorf()           : %.1f cyc/call (%.3f us/128)\n", (float)cycFloor / kIters, (float)cycFloor / kIters * 128.0f / 240.0f);
        dualPrintf("fmodf()            : %.1f cyc/call (%.3f us/128)\n", (float)cycFmod / kIters, (float)cycFmod / kIters * 128.0f / 240.0f);
        dualPrintf("float div (__divsf3): %.1f cyc/call (%.3f us/128)\n", (float)cycFdiv / kIters, (float)cycFdiv / kIters * 128.0f / 240.0f);
        dualPrintf("int32 modulo (%%)   : %.1f cyc/call (%.3f us/128)\n", (float)cycImod / kIters, (float)cycImod / kIters * 128.0f / 240.0f);
        dualPrintf("sink=(%.3f,%d) InternalLargest=%u PSRAMLargest=%u\n", (double)acc, (int)vacc,
                   heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                   heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
        dualPrintln("===================================================");
      }
      if (sramBuf) heap_caps_free(sramBuf);
      if (psramBuf) heap_caps_free(psramBuf);
      g_membench_done = true;
    }

    // Simple synth processing only for now
    int32_t synthBuf[AUDIO_CHUNK_SIZE * 2];
    size_t bytesWritten = 0;
    // Prepare input buffer for I2S
    int32_t rxBuf[AUDIO_CHUNK_SIZE * 2];
    // Initialize with zeros to avoid noise if read fails or is partial
    memset(rxBuf, 0, sizeof(rxBuf));

    if (p_source == SOURCE_I2S_IN) {
      size_t bytesRead = 0;
      // Non-blocking read (short timeout) to prevent WDT issues
      i2s_read(I2S_PORT, rxBuf, sizeof(rxBuf), &bytesRead, pdMS_TO_TICKS(10));
    }

    int64_t block_start_us = esp_timer_get_time();

    if (g_stage_prof.state == STAGE_WARMUP || g_stage_prof.state == STAGE_RUNNING) {
      const uint32_t blk0 = esp_cpu_get_cycle_count();
      uint32_t blkSrc = 0, blkDsp = 0, blkTanh = 0, blkGate = 0, blkFv = 0, blkOut = 0;
      for (int i = 0; i < AUDIO_CHUNK_SIZE; i++) {
        float processedL = 0.0f;
        float processedR = 0.0f;
        uint32_t c0 = esp_cpu_get_cycle_count();
        if (p_source == SOURCE_I2S_IN) {
          processedL = (float)rxBuf[i * 2] / 2147483648.0f;
          processedR = (float)rxBuf[i * 2 + 1] / 2147483648.0f;
        } else if (p_source == SOURCE_SYNTH && p_genActive && melody) {
          melody->nextStereo(&processedL, &processedR);
        } else if (p_source == SOURCE_MP3 && mp3 && file && out) {
          if (mp3->isRunning()) {
            if (!mp3->loop()) {
              mp3->stop();
              file->seek(0, SEEK_SET);
              mp3->begin(file, out);
            }
          }
          continue;
        }
        uint32_t c1 = esp_cpu_get_cycle_count();
        blkSrc += (c1 - c0);

        c0 = c1;
        float outL = processedL, outR = processedR;
        if (!isBypassed) {
          if (p_bubblesMode && bubbles && bubbles->isAllocated()) {
            bubbles->processStereo(processedL, processedR, &outL, &outR);
          } else if (p_frippMode && fripp && fripp->isAllocated()) {
            fripp->processStereo(processedL, processedR, &outL, &outR);
          } else if (tape) {
            tape->processStereo(processedL, processedR, &outL, &outR);
          }
        }
        c1 = esp_cpu_get_cycle_count();
        blkDsp += (c1 - c0);

        c0 = c1;
        processedL = tanhf(outL);
        processedR = tanhf(outR);
        c1 = esp_cpu_get_cycle_count();
        blkTanh += (c1 - c0);

        c0 = c1;
        if (gate) {
          gate->processStereo(processedL, processedR, &processedL, &processedR);
        }
        c1 = esp_cpu_get_cycle_count();
        blkGate += (c1 - c0);

        c0 = c1;
        if (p_freeverbEnabled && freeverb && freeverb->isAllocated()) {
          float reverbL, reverbR;
          freeverb->processStereo(processedL, processedR, &reverbL, &reverbR);
          processedL = reverbL;
          processedR = reverbR;
        }
        c1 = esp_cpu_get_cycle_count();
        blkFv += (c1 - c0);

        c0 = c1;
        if (out) {
          out->writeStereo(processedL, processedR, synthBuf, i);
        }
        c1 = esp_cpu_get_cycle_count();
        blkOut += (c1 - c0);
      }
      const uint32_t blkTot = esp_cpu_get_cycle_count() - blk0;
      if (g_stage_prof.state == STAGE_WARMUP) {
        if (--g_stage_prof.warmupRemain == 0) {
          g_stage_prof.collected = 0;
          g_stage_prof.cycSource = 0;
          g_stage_prof.cycDsp = 0;
          g_stage_prof.cycExtTanh = 0;
          g_stage_prof.cycGate = 0;
          g_stage_prof.cycFreeverb = 0;
          g_stage_prof.cycPcmOut = 0;
          g_stage_prof.cycTotal = 0;
          g_stage_prof.state = STAGE_RUNNING;
        }
      } else if (g_stage_prof.state == STAGE_RUNNING) {
        g_stage_prof.cycSource += blkSrc;
        g_stage_prof.cycDsp += blkDsp;
        g_stage_prof.cycExtTanh += blkTanh;
        g_stage_prof.cycGate += blkGate;
        g_stage_prof.cycFreeverb += blkFv;
        g_stage_prof.cycPcmOut += blkOut;
        g_stage_prof.cycTotal += blkTot;
        if (++g_stage_prof.collected >= g_stage_prof.targetBlocks) {
          g_stage_prof.state = STAGE_DONE;
        }
      }
    } else {
      for (int i = 0; i < AUDIO_CHUNK_SIZE; i++) {
        float processedL = 0.0f;
        float processedR = 0.0f;

        if (p_source == SOURCE_I2S_IN) {
          // Normalize 32-bit int to -1.0 .. 1.0 float
          processedL = (float)rxBuf[i * 2] / 2147483648.0f;
          processedR = (float)rxBuf[i * 2 + 1] / 2147483648.0f;
        } else if (p_source == SOURCE_SYNTH && p_genActive && melody) {
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
    }

    // Includes generation/DSP and PCM packing, with I2S calls outside the timer.
    const int64_t durationUs = esp_timer_get_time() - block_start_us;
    const float budgetLoad = 100.0f * durationUs / kDspBlockBudgetUs;
    portENTER_CRITICAL(&g_dsp_perf_mux);
    g_dsp_perf.lastUs = durationUs;
    g_dsp_perf.totalUs += durationUs;
    ++g_dsp_perf.blocks;
    if (durationUs > g_dsp_perf.peakUs) g_dsp_perf.peakUs = durationUs;
    if (durationUs > kDspBlockBudgetUs) ++g_dsp_perf.overruns;
    g_dsp_perf.budgetLoadEma = 0.95f * g_dsp_perf.budgetLoadEma + 0.05f * budgetLoad;
    const uint32_t curI2sFail = g_dsp_perf.i2sWriteFailures;
    portEXIT_CRITICAL(&g_dsp_perf_mux);

    if (g_bench.state == BENCH_WARMUP) {
      if (g_bench.warmupRemain > 0) --g_bench.warmupRemain;
      if (g_bench.warmupRemain == 0 && g_bench.hist) {
        memset(g_bench.hist, 0, kBenchHistBins * sizeof(uint32_t));
        g_bench.collected = 0;
        g_bench.sumUs = 0;
        g_bench.minUs = INT64_MAX;
        g_bench.peakUs = 0;
        g_bench.overruns = 0;
        g_bench.startI2sFail = curI2sFail;
        g_bench.startHeap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        g_bench.minFreeHeap = g_bench.startHeap;
        g_bench.startPsram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        g_bench.startWallUs = esp_timer_get_time();
        g_bench.state = BENCH_RUNNING;
      }
    } else if (g_bench.state == BENCH_RUNNING && g_bench.hist) {
      const uint32_t bin = (durationUs < 0) ? 0u : (durationUs >= (int64_t)(kBenchHistBins - 1) ? (uint32_t)(kBenchHistBins - 1) : (uint32_t)durationUs);
      ++g_bench.hist[bin];
      g_bench.sumUs += durationUs;
      if (durationUs < g_bench.minUs) g_bench.minUs = durationUs;
      if (durationUs > g_bench.peakUs) g_bench.peakUs = durationUs;
      if (durationUs > kDspBlockBudgetUs) ++g_bench.overruns;
      ++g_bench.collected;
      if ((g_bench.collected & 63u) == 0u) {
        const uint32_t hNow = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        if (hNow < g_bench.minFreeHeap) g_bench.minFreeHeap = hNow;
      }
      if (g_bench.collected >= g_bench.targetBlocks) {
        g_bench.endWallUs = esp_timer_get_time();
        g_bench.endI2sFail = curI2sFail;
        g_bench.endHeap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        if (g_bench.endHeap < g_bench.minFreeHeap) g_bench.minFreeHeap = g_bench.endHeap;
        g_bench.endPsram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        g_bench.state = BENCH_DONE;
      }
    }

    // Write block to I2S
    // Use a longer timeout (e.g., 100ms) to ensure we block if buffer is full.
    // If we return early, we might spin.
    esp_err_t wErr = i2s_write(I2S_PORT, synthBuf, sizeof(synthBuf),
                               &bytesWritten, pdMS_TO_TICKS(50));

    // Safety check: if we are somehow running too fast or I2S fails, yield
    // But normally i2s_write blocks.
    if (wErr != ESP_OK || bytesWritten != sizeof(synthBuf)) {
      portENTER_CRITICAL(&g_dsp_perf_mux);
      ++g_dsp_perf.i2sWriteFailures;
      portEXIT_CRITICAL(&g_dsp_perf_mux);
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
    p_tapeSpeed = 70.0f;
    p_tapeAge = 10.0f;
    p_headBump = 10.0f;
    p_drive = 20.0f;
    p_noise = 3.0f;
    p_dropoutSeverity = 0.0f;
    p_flutterDepth = 2.0f;
    p_wowDepth = 1.5f;
    p_tone = 65.0f;
    p_guitarFocus = true;
  } else if (name == "lofi") {
    // Worn, slow, noisy but usable
    p_tapeSpeed = 35.0f;
    p_tapeAge = 65.0f;
    p_headBump = 45.0f;
    p_drive = 45.0f;
    p_noise = 25.0f;
    p_dropoutSeverity = 12.0f;
    p_flutterDepth = 18.0f;
    p_wowDepth = 15.0f;
    p_tone = 35.0f;
  } else if (name == "dub") {
    // Space Echo style: High feed, dark
    p_tapeSpeed = 50.0f;
    p_tapeAge = 45.0f;
    p_feedback = 65.0f;
    p_activeHeads = 6; // Heads 2 & 3
    p_delayActive = true;
    p_tone = 30.0f;
    p_drive = 45.0f;
    p_noise = 12.0f;
    p_flutterDepth = 8.0f;
    p_wowDepth = 12.0f;
    p_headBump = 45.0f;
    p_guitarFocus = true;
  } else if (name == "broken") {
    // Bad motor, bad tape (Extreme but musical)
    p_flutterRate = 8.0f;
    p_flutterDepth = 40.0f;
    p_wowRate = 3.0f;
    p_wowDepth = 30.0f;
    p_dropoutSeverity = 35.0f;
    p_noise = 45.0f;
    p_tapeAge = 90.0f;
    p_tapeSpeed = 25.0f;
    p_tone = 25.0f;
  } else if (name == "warp_octave") {
    p_delayActive = true;
    p_delayTime = 200.0f;
    p_mix = 50.0f;
    p_warpActive = true;
    p_warpSpeed = 1.0f;
    p_warpGrainMs = 80.0f;
    p_warpStereo = 0.5f;
    p_warpFeedback = 0.0f;
    p_warpMode = 0;
  } else if (name == "warp_cascade") {
    p_delayActive = true;
    p_delayTime = 250.0f;
    p_mix = 60.0f;
    p_warpActive = true;
    p_warpSpeed = 1.0f;
    p_warpGrainMs = 90.0f;
    p_warpStereo = 0.5f;
    p_warpFeedback = 60.0f;
    p_warpMode = 0;
  } else {
    dualPrintln("Unknown preset. Try: clean, lofi, dub, broken, warp_octave, warp_cascade");
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
  dualPrintln("  ftd <0-100> : Flutter Depth % (maps to 0-45 core range)");
  dualPrintln("  ftr <0.1-20>: Flutter Rate in Hz");
  dualPrintln("  wwd <0-100> : Wow Depth % (maps to 0-35 core range)");
  dualPrintln("  wwr <0.1-5> : Wow Rate in Hz");
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
  dualPrintln("  spm <0-100> : Spring Mix");
  dualPrintln("  spd <0-100> : Spring Decay");
  dualPrintln("  spf <0-100> : Spring Damping");
  dualPrintln(" [ACCIDENTAL WARP V1.3]");
  dualPrintln("  wrp <0/1>   : Warp Enable/Bypass");
  dualPrintln("  wsp <0-2.0> : Warp Speed Multiplier (1.0 = +1 Octave)");
  dualPrintln("  wgr <10-400>: Warp Grain Window in ms");
  dualPrintln("  wst <0-1.0> : Warp Stereo Phase Offset");
  dualPrintln("  wfb <0-95>  : Warp Feedback %");
  dualPrintln("  wmd <0-3>   : Warp Mode (0=2T-Herm, 1=1T-Herm, 2=1T-Raw, 3=2T-Raw)");
  dualPrintln("  perf        : DSP Performance & Telemetry Stats");
  dualPrintln("");
  dualPrintln("  load <preset> : clean, lofi, dub, broken, warp_octave, warp_cascade");
  dualPrintln("  list          : Show Dashboard");
}

void printDspPerformanceStats() {
  dualPrintln("\n=== HYDRA DSP PERFORMANCE TELEMETRY ===");
  dualPrintf("Block Size          : %d frames\n", AUDIO_CHUNK_SIZE);
  dualPrintf("Sample Rate         : %d Hz\n", SAMPLE_RATE);
  dualPrintf("CPU Clock           : %u MHz\n", getCpuFrequencyMhz());
  portENTER_CRITICAL(&g_dsp_perf_mux);
  const DspPerformance perf = g_dsp_perf;
  portEXIT_CRITICAL(&g_dsp_perf_mux);
  dualPrintf("Block Time Budget   : %.1f us\n", kDspBlockBudgetUs);
  dualPrintf("Blocks Processed    : %u\n", perf.blocks);
  const int64_t avgUs = perf.blocks ? perf.totalUs / perf.blocks : 0;
  dualPrintf("Avg DSP Block Time  : %lld us\n", avgUs);
  dualPrintf("Last DSP Block Time : %lld us\n", perf.lastUs);
  dualPrintf("Peak DSP Block Time : %lld us\n", perf.peakUs);
  dualPrintf("DSP Budget Use (EMA): %.1f %%\n", perf.budgetLoadEma);
  dualPrintf("DSP Budget Overruns : %u\n", perf.overruns);
  dualPrintf("I2S TX Write Failures: %u\n", perf.i2sWriteFailures);
  dualPrintf("Internal Free Heap  : %u bytes\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  dualPrintf("SPIRAM (PSRAM) Free : %u bytes\n", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  dualPrintf("Warp Active         : %s\n", p_warpActive ? "YES" : "NO");
  if (p_warpActive) {
    dualPrintf("  Mode              : %d\n", p_warpMode);
    dualPrintf("  Speed Multiplier  : %.2fx (v_eff = %.2fx)\n", p_warpSpeed, 1.0f + p_warpSpeed);
    dualPrintf("  Grain Window      : %.1f ms\n", p_warpGrainMs);
    dualPrintf("  Stereo Offset     : %.2f\n", p_warpStereo);
    dualPrintf("  Warp Feedback     : %.1f %%\n", p_warpFeedback);
  }
  dualPrintln("========================================\n");
}

// --- DASHBOARD STATUS ---
void printStatus() {
  dualPrintln("\n================= TAPE DSP DASHBOARD ==================");

  // MIXER SECTION
  dualPrintln(" [ MIXER ]");
  printBar(" Volume", masterVolume);
  printBar(" Dry/Wet", p_mix / 100.0f);
  dualPrintf(" Source      : %s\n", (p_source == 0)   ? "MP3"
                                    : (p_source == 1) ? "SYNTH"
                                                      : "I2S");
  dualPrintf(" Bypass      : %s\n", isBypassed ? "YES" : "NO");

  // DELAY SECTION
  dualPrintln("\n [ TAPE ENGINE ]");
  char dBuf[32];
  sprintf(dBuf, "%.0f ms", p_delayTime);
  dualPrintf(" Delay Time  : %-12s ", dBuf);
  printBar("Feedback", p_feedback / 100.0f);
  printBar(" Speed", p_tapeSpeed / 100.0f);
  printBar(" Age", p_tapeAge / 100.0f);
  dualPrintf(" Heads       : %d (%s)\n", p_activeHeads,
             p_headsMusical ? "Musical" : "Free");

  // WARP SECTION
  dualPrintln("\n [ ACCIDENTAL WARP ]");
  dualPrintf(" Warp Active : %s\n", p_warpActive ? "YES" : "NO");
  if (p_warpActive) {
    dualPrintf(" Mode        : %d (%s)\n", p_warpMode,
               p_warpMode == 0 ? "2-Taps Hermite" :
               p_warpMode == 1 ? "1-Tap Hermite" :
               p_warpMode == 2 ? "1-Tap Raw Int" : "2-Taps Raw Int");
    dualPrintf(" Speed Mult  : %.2fx\n", p_warpSpeed);
    dualPrintf(" Grain Size  : %.1f ms\n", p_warpGrainMs);
    dualPrintf(" Stereo Wid  : %.2f\n", p_warpStereo);
    printBar(" Warp FB", p_warpFeedback / 95.0f);
  }

  // COLOR SECTION
  dualPrintln("\n [ COLOR & TONE ]");
  printBar(" Drive", p_drive / 100.0f);
  printBar(" Tone", p_tone / 100.0f);
  printBar(" Noise", p_noise / 100.0f);
  dualPrintf(" Guitar Mode : %s\n",
             p_guitarFocus ? "ON (Filtered)" : "OFF (Full Range)");

  // MODULATION SECTION
  dualPrintln("\n [ MODULATION ]");
  printBar(" Wow", p_wowDepth / 35.0f);
  printBar(" Flutter", p_flutterDepth / 45.0f);
  printBar(" Dropouts", p_dropoutSeverity / 40.0f);

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

  if (cmd == "bench") {
    uint32_t blocks = 1024;
    uint32_t warmup = 256;
    if (valStr.length() > 0) {
      int sp2 = valStr.indexOf(' ');
      if (sp2 > 0) {
        long b = valStr.substring(0, sp2).toInt();
        long w = valStr.substring(sp2 + 1).toInt();
        if (b > 0) blocks = (uint32_t)b;
        if (w > 0) warmup = (uint32_t)w;
      } else {
        long b = valStr.toInt();
        if (b > 0) blocks = (uint32_t)b;
      }
    }
    if (!g_bench.hist) {
      dualPrintln("BENCH ERR: histogram not allocated");
      return;
    }
    g_bench.targetBlocks = blocks;
    g_bench.warmupRemain = (warmup > 0) ? warmup : 1;
    g_bench.state = BENCH_WARMUP;
    const int64_t deadlineUs = esp_timer_get_time() + ((int64_t)(blocks + warmup) * 15000LL) + 10000000LL;
    while (g_bench.state != BENCH_DONE && esp_timer_get_time() < deadlineUs) {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (g_bench.state != BENCH_DONE) {
      dualPrintf("BENCH TIMEOUT (state=%u collected=%lu/%lu)\n",
                 (unsigned)g_bench.state, (unsigned long)g_bench.collected, (unsigned long)g_bench.targetBlocks);
      g_bench.state = BENCH_IDLE;
      return;
    }
    const uint32_t n = g_bench.collected;
    const uint32_t r50 = (n + 1u) / 2u;
    const uint32_t r95 = (uint32_t)((95ULL * n + 99ULL) / 100ULL);
    const uint32_t r99 = (uint32_t)((99ULL * n + 99ULL) / 100ULL);
    uint32_t p50 = 0, p95 = 0, p99 = 0;
    bool got50 = false, got95 = false, got99 = false;
    uint32_t cum = 0;
    for (uint32_t bin = 0; bin < kBenchHistBins; ++bin) {
      cum += g_bench.hist[bin];
      if (!got50 && cum >= r50) { p50 = bin; got50 = true; }
      if (!got95 && cum >= r95) { p95 = bin; got95 = true; }
      if (!got99 && cum >= r99) { p99 = bin; got99 = true; break; }
    }
    const double meanUs = (n > 0) ? ((double)g_bench.sumUs / (double)n) : 0.0;
    const double meanPct = meanUs * 100.0 / kDspBlockBudgetUs;
    const double p99Pct = (double)p99 * 100.0 / kDspBlockBudgetUs;
    const double wallMs = (double)(g_bench.endWallUs - g_bench.startWallUs) / 1000.0;
    const uint32_t satBins = g_bench.hist[kBenchHistBins - 1];
    dualPrintf("\n=== BENCH RESULT (FW %s %s) ===\n", __DATE__, __TIME__);
    dualPrintf("Cfg: src=%d gen=%d byp=%d mod=%d dly=%.0f fbk=%.0f hds=%d tps=%.0f tpa=%.0f drv=%.0f nlv=%.0f ftd=%.0f wwd=%.0f dps=%.0f wrp=%d wmd=%d wsp=%.2f wgr=%.0f wst=%.2f wfb=%.0f fv=%d ngt=%.4f ppg=%d frz=%d rev=%d spr=%d\n",
               (int)p_source, p_genActive ? 1 : 0, isBypassed ? 1 : 0, p_delayActive ? 1 : 0,
               (double)p_delayTime, (double)p_feedback, p_activeHeads,
               (double)p_tapeSpeed, (double)p_tapeAge, (double)p_drive, (double)p_noise,
               (double)p_flutterDepth, (double)p_wowDepth, (double)p_dropoutSeverity,
               p_warpActive ? 1 : 0, p_warpMode, (double)p_warpSpeed, (double)p_warpGrainMs,
               (double)p_warpStereo, (double)p_warpFeedback, p_freeverbEnabled ? 1 : 0,
               (double)p_gateThreshold, p_pingPong ? 1 : 0, p_freeze ? 1 : 0, p_reverse ? 1 : 0, p_spring ? 1 : 0);
    dualPrintf("Blocks=%lu Warmup=%lu WallMs=%.1f BudgetUs=%.2f\n",
               (unsigned long)n, (unsigned long)warmup, wallMs, kDspBlockBudgetUs);
    dualPrintf("MeanUs=%.2f MinUs=%lld P50Us=%lu P95Us=%lu P99Us=%lu PeakUs=%lld\n",
               meanUs, (long long)g_bench.minUs, (unsigned long)p50, (unsigned long)p95,
               (unsigned long)p99, (long long)g_bench.peakUs);
    dualPrintf("BudgetMeanPct=%.2f%% BudgetP99Pct=%.2f%% Overruns=%lu (%.2f%%) SatBins=%lu\n",
               meanPct, p99Pct, (unsigned long)g_bench.overruns,
               (n > 0) ? (100.0 * (double)g_bench.overruns / (double)n) : 0.0,
               (unsigned long)satBins);
    dualPrintf("I2SFailDelta=%lu HeapStart=%lu HeapEnd=%lu HeapMin=%lu PsramStart=%lu PsramEnd=%lu MaxParamUpdUs=%lld\n",
               (unsigned long)(g_bench.endI2sFail - g_bench.startI2sFail),
               (unsigned long)g_bench.startHeap, (unsigned long)g_bench.endHeap,
               (unsigned long)g_bench.minFreeHeap, (unsigned long)g_bench.startPsram,
               (unsigned long)g_bench.endPsram, (long long)g_bench.maxParamUpdateUs);
    dualPrintln("=========================================");
    g_bench.state = BENCH_IDLE;
    return;
  }

  if (cmd == "stageprof") {
    uint32_t blocks = 512;
    uint32_t warmup = 64;
    if (valStr.length() > 0) {
      int sp2 = valStr.indexOf(' ');
      if (sp2 > 0) {
        long b = valStr.substring(0, sp2).toInt();
        long w = valStr.substring(sp2 + 1).toInt();
        if (b > 0) blocks = (uint32_t)b;
        if (w > 0) warmup = (uint32_t)w;
      } else {
        long b = valStr.toInt();
        if (b > 0) blocks = (uint32_t)b;
      }
    }
    g_stage_prof.targetBlocks = blocks;
    g_stage_prof.warmupRemain = (warmup > 0) ? warmup : 1;
    g_stage_prof.state = STAGE_WARMUP;
    const int64_t deadlineUs = esp_timer_get_time() + ((int64_t)(blocks + warmup) * 20000LL) + 5000000LL;
    while (g_stage_prof.state != STAGE_DONE && esp_timer_get_time() < deadlineUs) {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (g_stage_prof.state != STAGE_DONE) {
      dualPrintf("STAGEPROF TIMEOUT (state=%u collected=%lu/%lu)\n",
                 (unsigned)g_stage_prof.state, (unsigned long)g_stage_prof.collected, (unsigned long)g_stage_prof.targetBlocks);
      g_stage_prof.state = STAGE_IDLE;
      return;
    }
    const double n = (double)g_stage_prof.collected;
    const double toUs = 1.0 / (240.0 * n);
    const double usSrc = (double)g_stage_prof.cycSource * toUs;
    const double usDsp = (double)g_stage_prof.cycDsp * toUs;
    const double usTanh = (double)g_stage_prof.cycExtTanh * toUs;
    const double usGate = (double)g_stage_prof.cycGate * toUs;
    const double usFv = (double)g_stage_prof.cycFreeverb * toUs;
    const double usOut = (double)g_stage_prof.cycPcmOut * toUs;
    const double usTot = (double)g_stage_prof.cycTotal * toUs;
    const double usSum = usSrc + usDsp + usTanh + usGate + usFv + usOut;
    const double usOvh = usTot - usSum;
    auto pctTot = [usTot](double u) -> double { return (usTot > 0.0) ? (100.0 * u / usTot) : 0.0; };
    auto pctBud = [](double u) -> double { return 100.0 * u / kDspBlockBudgetUs; };
    dualPrintf("\n=== STAGE PROFILE (%lu blocks, warmup %lu @ 240MHz) ===\n",
               (unsigned long)g_stage_prof.collected, (unsigned long)warmup);
    dualPrintf("A1 Source/Gen/In : %8.2f us/blk (%5.1f%% tot | %5.1f%% budget) [%lu cyc/blk]\n",
               usSrc, pctTot(usSrc), pctBud(usSrc), (unsigned long)(g_stage_prof.cycSource / g_stage_prof.collected));
    dualPrintf("A2 TapeCore/DSP  : %8.2f us/blk (%5.1f%% tot | %5.1f%% budget) [%lu cyc/blk]\n",
               usDsp, pctTot(usDsp), pctBud(usDsp), (unsigned long)(g_stage_prof.cycDsp / g_stage_prof.collected));
    dualPrintf("A3a Ext tanhf()  : %8.2f us/blk (%5.1f%% tot | %5.1f%% budget) [%lu cyc/blk]\n",
               usTanh, pctTot(usTanh), pctBud(usTanh), (unsigned long)(g_stage_prof.cycExtTanh / g_stage_prof.collected));
    dualPrintf("A3b NoiseGate    : %8.2f us/blk (%5.1f%% tot | %5.1f%% budget) [%lu cyc/blk]\n",
               usGate, pctTot(usGate), pctBud(usGate), (unsigned long)(g_stage_prof.cycGate / g_stage_prof.collected));
    dualPrintf("A3c Freeverb     : %8.2f us/blk (%5.1f%% tot | %5.1f%% budget) [%lu cyc/blk]\n",
               usFv, pctTot(usFv), pctBud(usFv), (unsigned long)(g_stage_prof.cycFreeverb / g_stage_prof.collected));
    dualPrintf("A3d PCM Out      : %8.2f us/blk (%5.1f%% tot | %5.1f%% budget) [%lu cyc/blk]\n",
               usOut, pctTot(usOut), pctBud(usOut), (unsigned long)(g_stage_prof.cycPcmOut / g_stage_prof.collected));
    dualPrintf("Loop/Timer Ovh   : %8.2f us/blk (%5.1f%% tot | %5.1f%% budget)\n",
               usOvh, pctTot(usOvh), pctBud(usOvh));
    dualPrintf("TOTAL BLOCK      : %8.2f us/blk (100.0%% tot | %5.1f%% budget) [%lu cyc/blk]\n",
               usTot, pctBud(usTot), (unsigned long)(g_stage_prof.cycTotal / g_stage_prof.collected));
    dualPrintln("=====================================================");
    g_stage_prof.state = STAGE_IDLE;
    return;
  }

  if (cmd == "membench") {
    g_membench_done = false;
    g_membench_req = true;
    const int64_t deadlineUs = esp_timer_get_time() + 2000000LL;
    while (!g_membench_done && esp_timer_get_time() < deadlineUs) {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (!g_membench_done) dualPrintln("MEMBENCH TIMEOUT");
    return;
  }

  if (cmd == "resetperf") {
    portENTER_CRITICAL(&g_dsp_perf_mux);
    g_dsp_perf.blocks = 0;
    g_dsp_perf.totalUs = 0;
    g_dsp_perf.lastUs = 0;
    g_dsp_perf.peakUs = 0;
    g_dsp_perf.overruns = 0;
    g_dsp_perf.i2sWriteFailures = 0;
    g_dsp_perf.budgetLoadEma = 0.0f;
    portEXIT_CRITICAL(&g_dsp_perf_mux);
    g_bench.lastParamUpdateUs = 0;
    g_bench.maxParamUpdateUs = 0;
    dualPrintln("Perf counters reset.");
    return;
  }

  if (xSemaphoreTake(paramMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    auto map100 = [](float v, float minV, float maxV) -> float {
      float norm = constrain(v, 0.0f, 100.0f) / 100.0f;
      return minV + (norm * (maxV - minV));
    };

    // --- MIXER ---
    if (cmd == "vol")
      masterVolume = map100(val, 0.0f, 1.0f);
    else if (cmd == "mix")
      p_mix = constrain(val, 0.0f, 100.0f);
    else if (cmd == "byp") {
      isBypassed = (val > 0);
      dualPrintf("Bypass: %s\n", isBypassed ? "ON" : "OFF");
    } else if (cmd == "gen") {
      p_genActive = (val > 0);
      dualPrintf("Generator: %s\n", p_genActive ? "ON" : "OFF");
    } else if (cmd == "src") {
      p_source = (AudioSource)constrain((int)val, 0, 2);
      dualPrintf("Source: %d (0=MP3, 1=SYNTH, 2=I2S)\n", p_source);
    }

    // --- TAPE ENGINE ---
    else if (cmd == "dly")
      p_delayTime = constrain(val, 10.0f, 2000.0f);
    else if (cmd == "fbk")
      p_feedback = constrain(val, 0.0f, 100.0f);
    else if (cmd == "hds")
      p_activeHeads = constrain((int)val, 1, 7);
    else if (cmd == "tps")
      p_tapeSpeed = constrain(val, 0.0f, 100.0f);
    else if (cmd == "tpa")
      p_tapeAge = constrain(val, 0.0f, 100.0f);
    else if (cmd == "drv")
      p_drive = constrain(val, 0.0f, 100.0f);
    else if (cmd == "nlv")
      p_noise = constrain(val, 0.0f, 100.0f);
    else if (cmd == "ngt")
      p_gateThreshold = map100(val, 0.0f, 0.1f);
    else if (cmd == "hbp")
      p_headBump = constrain(val, 0.0f, 100.0f);
    else if (cmd == "azm")
      p_azimuth = constrain(val, 0.0f, 100.0f);
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
      p_tone = constrain(val, 0.0f, 100.0f);

    // --- MODULATION ---
    else if (cmd == "dps")
      p_dropoutSeverity = map100(val, 0.0f, 40.0f);
    else if (cmd == "ftd")
      p_flutterDepth = map100(val, 0.0f, 45.0f);
    else if (cmd == "ftr")
      p_flutterRate = constrain(val, 0.1f, 20.0f);
    else if (cmd == "wwd")
      p_wowDepth = map100(val, 0.0f, 35.0f);
    else if (cmd == "wwr")
      p_wowRate = constrain(val, 0.1f, 5.0f);

    // --- NEW EFFECT MODES ---
    else if (cmd == "ppg") {
      p_pingPong = (val > 0);
      dualPrintf("Ping-Pong: %s\n", p_pingPong ? "ON" : "OFF");
    }
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
    else if (cmd == "spm") {
      p_springMix = constrain(val, 0.0f, 100.0f);
      printBar("Spring Mix", p_springMix / 100.0f);
    }
    else if (cmd == "spd") {
      p_springDecay = constrain(val, 0.0f, 100.0f);
      printBar("Spring Decay", p_springDecay / 100.0f);
    }
    else if (cmd == "spf") {
      p_springDamping = constrain(val, 0.0f, 100.0f);
      printBar("Spring Damp", p_springDamping / 100.0f);
    }

    // --- ACCIDENTAL WARP V1.3 ---
    else if (cmd == "wrp") {
      p_warpActive = (val > 0);
      dualPrintf("Accidental Warp: %s\n", p_warpActive ? "ON" : "OFF");
    }
    else if (cmd == "wsp") {
      p_warpSpeed = constrain(val, 0.0f, 2.0f);
      dualPrintf("Warp Speed: %.2fx (v_eff = %.2fx)\n", p_warpSpeed, 1.0f + p_warpSpeed);
    }
    else if (cmd == "wgr") {
      p_warpGrainMs = constrain(val, 10.0f, 400.0f);
      dualPrintf("Warp Grain: %.1f ms\n", p_warpGrainMs);
    }
    else if (cmd == "wst") {
      p_warpStereo = constrain(val, 0.0f, 1.0f);
      dualPrintf("Warp Stereo Offset: %.2f\n", p_warpStereo);
    }
    else if (cmd == "wfb") {
      p_warpFeedback = constrain(val, 0.0f, 95.0f);
      dualPrintf("Warp Feedback: %.1f%%\n", p_warpFeedback);
    }
    else if (cmd == "wmd") {
      p_warpMode = constrain((int)val, 0, 3);
      dualPrintf("Warp Mode: %d\n", p_warpMode);
    }
    else if (cmd == "perf") {
      printDspPerformanceStats();
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
        p_mood = constrain(val, 0.0f, 100.0f);
        melody->setMood(p_mood / 100.0f);
      } else if (cmd == "rtm") { // Rhythm 0-100
        p_rhythm = constrain(val, 0.0f, 100.0f);
        melody->setRhythm(p_rhythm / 100.0f);
      } else if (cmd == "eno") { // ENO MODE
        bool isEno = (val > 0.5f);
        melody->setMode(isEno ? MODE_ENO : MODE_NORMAL);
        if (isEno) {
          p_feedback = 85.0f;
          p_delayTime = 800.0f;
          p_mix = 60.0f;
          p_tapeAge = 70.0f; // Darker
          dualPrintln("Mode: ENO ACTIVATED");
        } else {
          dualPrintln("Mode: NORMAL");
        }
      }
    }

    globalParams = makeTapeParams();
    dspParamsDirty = true;

    xSemaphoreGive(paramMutex);

    // Visual Feedback
    if (cmd != "?" && cmd != "stat" && cmd != "list" && cmd != "src" &&
        cmd != "load" && cmd != "byp" && cmd != "gen" && cmd != "perf") {
      if (cmd == "vol")
        printBar("Volume", masterVolume);
      else if (cmd == "mix")
        printBar("Mix", p_mix / 100.0f);
      else if (cmd == "fbk")
        printBar("Feedback", p_feedback / 100.0f);
      else if (cmd == "tps")
        printBar("Speed", p_tapeSpeed / 100.0f);
      else if (cmd == "tpa")
        printBar("Age", p_tapeAge / 100.0f);
      else if (cmd == "ton")
        printBar("Tone", p_tone / 100.0f);
      else if (cmd == "drv")
        printBar("Drive", p_drive / 100.0f);
      else if (cmd == "nlv")
        printBar("Noise", p_noise / 100.0f);
      else if (cmd == "ftd")
        printBar("Flutter", p_flutterDepth / 45.0f);
      else if (cmd == "wwd")
        printBar("Wow", p_wowDepth / 35.0f);
      else if (cmd == "dps")
        printBar("Dropouts", p_dropoutSeverity / 40.0f);
      else if (cmd == "dly")
        dualPrintf("Delay: %.0f ms\n", p_delayTime);
      else if (cmd == "wsp")
        printBar("Warp Spd", p_warpSpeed / 2.0f);
      else if (cmd == "wgr")
        printBar("Warp Grn", (p_warpGrainMs - 10.0f) / 390.0f);
      else if (cmd == "wst")
        printBar("Warp Str", p_warpStereo);
      else if (cmd == "wfb")
        printBar("Warp FB", p_warpFeedback / 95.0f);
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
