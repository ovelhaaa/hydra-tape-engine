// ============================================
// ANALOG TAPE EMULATOR - MULTI-HEAD EDITION
// ============================================
// HARDWARE: YD-ESP32-S3 (N16R8) + DAC PCM5102
// ============================================

#include "MelodyGen.h"
#include "TapeDelay.h"

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
#define SAMPLE_RATE 48000
// AUMENTADO: Buffer DMA maior para compensar latência da PSRAM
#define I2S_DMA_BUF_LEN 256
#define I2S_DMA_BUF_COUNT 4
#define I2S_PORT I2S_NUM_0

// Buffer de processamento mantido para compatibilidade,
// embora AudioOutput processe sample-by-sample.
#define PROCESS_BUFFER_SIZE 512

// ============================================
// CONTROLS & GLOBALS
// ============================================
volatile bool isBypassed = false;
volatile float masterVolume = 0.3f;

// Parameters managed via struct
float p_flutterDepth = 0.35f;
float p_wowDepth = 0.45;
float p_dropoutSeverity = 0.90f;
float p_drive = 3.0f;
float p_noise = 0.035f;
float p_flutterRate = 6.0f;
float p_wowRate = 0.5f;

float p_tapeSpeed = 0.5f;
float p_tapeAge = 0.5f;
float p_headBump = 3.0f;
float p_azimuth = 0.0f;
float p_bpm = 120.0f;
bool p_headsMusical = false;

float p_mood = 0.5f;
float p_rhythm = 0.5f;

bool p_delayActive = false;
float p_delayTime = 500.0f;
float p_feedback = 0.3f;
float p_mix = 1.0f;
int p_activeHeads = 4;
volatile bool p_triggerPlay = false;

TapeParams globalParams = {
    p_flutterDepth, p_wowDepth,    p_dropoutSeverity, p_drive,    p_noise,
    p_tapeSpeed,    p_tapeAge,     p_headBump,        p_azimuth,  p_flutterRate,
    p_wowRate,      p_delayActive, p_delayTime,       p_feedback, p_mix,
    p_activeHeads,  p_bpm,         p_headsMusical};

enum AudioSource { SOURCE_MP3, SOURCE_SYNTH, SOURCE_I2S_IN };
volatile AudioSource p_source = SOURCE_MP3;

TapeModel *tape = nullptr;
MelodyGen *melody = nullptr;

// NOVO: Objetos MP3
AudioGeneratorMP3 *mp3;
AudioFileSourceLittleFS *file;
// AudioOutputTapeInterceptor declarado abaixo

SemaphoreHandle_t paramMutex;

// --- Helpers de Report ---
static void reportParam(const char *name, int percent, float value,
                        const char *unit = "") {
  if (percent >= 0)
    Serial.printf("%-12s %3d%% => %6.2f %s\n", name, percent, value, unit);
  else
    Serial.printf("%-12s N/A  => %6.2f %s\n", name, value, unit);
}
static void reportParamInt(const char *name, int percent, int value,
                           const char *unit = "") {
  if (percent >= 0)
    Serial.printf("%-12s %3d%% => %d %s\n", name, percent, value, unit);
  else
    Serial.printf("%-12s N/A  => %d %s\n", name, value, unit);
}

static float headTimeMs(const TapeParams &p, int headBit) {
  if (!p.delayActive)
    return 0.0f;
  if (p.headsMusical) {
    float beatMs = 60000.0f / p.bpm;
    if (headBit == 1)
      return beatMs * (1.0f / 3.0f);
    if (headBit == 2)
      return beatMs * 0.75f;
    if (headBit == 4)
      return beatMs * 1.0f;
  } else {
    if (headBit == 1)
      return p.delayTimeMs * 0.33f;
    if (headBit == 2)
      return p.delayTimeMs * 0.66f;
    if (headBit == 4)
      return p.delayTimeMs * 1.0f;
  }
  return 0.0f;
}

static void printHeadTimes(const TapeParams &p) {
  float tA = headTimeMs(p, 1);
  float tB = headTimeMs(p, 2);
  float tC = headTimeMs(p, 4);
  Serial.printf("Head Times (ms): A:%6.1f  B:%6.1f  C:%6.1f\n", tA, tB, tC);
}

// ============================================
// CUSTOM AUDIO OUTPUT: INTERCEPTOR & TAPE PROC
// ============================================
class AudioOutputTapeInterceptor : public AudioOutput {
public:
  AudioOutputTapeInterceptor() {
    // Nada específico aqui, SampleRate tratado pelo begin do MP3 gen,
    // mas nós usamos SAMPLE_RATE fixo para I2S.
  }

  virtual ~AudioOutputTapeInterceptor() {}

  virtual bool begin() override {
    // I2S já inicializado no setup()
    return true;
  }

  virtual bool ConsumeSample(int16_t sample[2]) override {
    // 1. Converter Stereo int16 -> Mono float
    float input = (sample[0] + sample[1]) / 2.0f; // Mix simples

    // Normaliza (-32768 a 32767 -> -1.0 a 1.0)
    input /= 32768.0f;

    // 2. Processar via TAPE
    float processed = input;

    // Acesso mutex opcional aqui se tape->process não for thread-safe com
    // updates Mas geralmente updates são atômicos ou protegidos internamente.
    // Como estamos na task de Audio, é safe rodar process().
    if (!isBypassed && tape) {
      processed = tape->process(input);
    }

    // 3. Clipping Suave & Volume
    processed = tanhf(processed);

    // Converter de volta para int32 para o DAC PCM5102 (formato I2S 32-bit
    // alinhado) Original usava escala de 2147483647.0 * 0.90
    const double INT32_MAX_D = 2147483647.0;
    const float VOL_SCALE = (float)(INT32_MAX_D * 0.90f);

    int32_t outSample = (int32_t)(processed * masterVolume * VOL_SCALE);

    // Preenche buffer estéreo (L=R)
    int32_t i2sBuffer[2];
    i2sBuffer[0] = outSample;
    i2sBuffer[1] = outSample; // Mono espelhado

    size_t bytesWritten;
    // Escrita com TIMEOUT para evitar travar se não houver clock (Slave Mode)
    // Se falhar (timeout), apenas retornamos true para manter a lógica do MP3
    // rodando ou false se quisermos parar. Aqui, true evita crash hard.
    i2s_write(I2S_PORT, i2sBuffer, sizeof(i2sBuffer), &bytesWritten,
              pdMS_TO_TICKS(50));

    return true;
  }
};

AudioOutputTapeInterceptor *out;

// ============================================
// TASKS
// ============================================
void audioTask(void *parameter) {
  // esp_task_wdt_add(NULL);

  // Inicializa Tape Engine
  tape = new TapeModel(SAMPLE_RATE);
  tape->updateParams(globalParams);
  tape->updateFilters();

  // Initialize MelodyGen
  melody = new MelodyGen(SAMPLE_RATE);
  melody->setBPM(p_bpm);
  melody->setMood(p_mood);
  melody->setRhythm(p_rhythm);

  // Configura Fonte e Saída MP3
  file = new AudioFileSourceLittleFS("/demo.mp3");
  out = new AudioOutputTapeInterceptor();
  mp3 = new AudioGeneratorMP3();

  // Start with MP3 stopped or running depending on preference,
  // manual play logic handles restarts.
  // Let's ensure MP3 is stopped if we start in SYNTH mode (though default is
  // MP3).
  if (p_source == SOURCE_MP3) {
    Serial.println("Audio Task: Starting MP3...");
    mp3->begin(file, out);
  }

  TapeParams localParams;
  const double INT32_MAX_D = 2147483647.0;
  const float VOL_SCALE = (float)(INT32_MAX_D * 0.90f);

  // Track active source locally to detect changes
  AudioSource activeSource = p_source;

  while (true) {
    // esp_task_wdt_reset();

    // 1. Atualizar Parâmetros (se houver mudança)
    if (xSemaphoreTake(paramMutex, 0) == pdTRUE) {
      localParams = globalParams;
      xSemaphoreGive(paramMutex);
      tape->updateParams(localParams);
      tape->updateFilters();

      // Update MelodyGen params
      if (melody) {
        melody->setBPM(localParams.bpm);
        // Mood/Rhythm are globals, not in TapeParams struct in original code
        // well separate But we can update them here if we added them to struct
        // or just rely on globals (careful with race conditions, but floats are
        // usually atomic enough for audio)
        melody->setMood(p_mood);
        melody->setRhythm(p_rhythm);
      }
    }

    // 2. Handle Source Switching (Thread-Safe)
    if (p_source != activeSource) {
      // Stop previous source if it was MP3
      if (activeSource == SOURCE_MP3) {
        if (mp3->isRunning())
          mp3->stop();
      }

      // Update local state
      activeSource = p_source;

      // Start new source if it is MP3
      if (activeSource == SOURCE_MP3) {
        file->seek(0, SEEK_SET);
        if (mp3->begin(file, out)) {
          Serial.println("MP3 Auto-Started (Switch).");
        } else {
          Serial.println("MP3 Begin Failed (Switch)!");
        }
      }
    }

    // 3. Control Playback Trigger (Manual Restart)
    if (p_triggerPlay) {
      p_triggerPlay = false;
      // Behavior depends on source
      if (activeSource == SOURCE_MP3) {
        if (mp3->isRunning())
          mp3->stop();
        file->seek(0, SEEK_SET);
        if (mp3->begin(file, out))
          Serial.println("MP3 Started.");
        else
          Serial.println("MP3 Begin Failed!");
      } else {
        Serial.println("Synth Restarted (conceptually)");
        // Could reset melody phase or seed if desire
      }
    }

    // 4. Audio Loop Processing
    if (activeSource == SOURCE_MP3) {
      if (mp3->isRunning()) {
        if (!mp3->loop()) {
          mp3->stop();
          Serial.println("MP3 Done. Stopped.");
        }
      } else {
        vTaskDelay(10);
      }
    } else if (activeSource == SOURCE_SYNTH) {
      // SOURCE_SYNTH
      // Generate Block of Samples to maximize efficiency?
      // Or sample-by-sample matching i2s_write blocking behavior.
      // i2s_write with little data is inefficient. Let's do a small batch.
      // Using a small local buffer on stack.
      int32_t synthBuf[64 * 2]; // 64 stereo frames
      size_t bytesWritten;

      for (int i = 0; i < 64; i++) {
        float gen = melody->next();

        // Process via Tape
        float processed = gen;
        if (!isBypassed && tape) {
          processed = tape->process(gen);
        }

        // Soft Clip
        processed = tanhf(processed);

        int32_t sample = (int32_t)(processed * masterVolume * VOL_SCALE);
        synthBuf[i * 2] = sample;
        synthBuf[i * 2 + 1] = sample;
      }

      i2s_write(I2S_PORT, synthBuf, sizeof(synthBuf), &bytesWritten,
                pdMS_TO_TICKS(50));
    } else if (activeSource == SOURCE_I2S_IN) {
      // SOURCE_I2S_IN: Read -> Process -> Write
      // 1. Read Input (Blocking with Timeout)
      int32_t inBuf[64 * 2];
      size_t bytesRead = 0;
      esp_err_t err = i2s_read(I2S_PORT, inBuf, sizeof(inBuf), &bytesRead,
                               pdMS_TO_TICKS(50));

      if (err == ESP_OK && bytesRead > 0) {
        int samplesRead = bytesRead / 4; // 32-bit samples (stereo interleaved?
                                         // no, bits_per_sample=32 -> 4 bytes)
        // Wait, channel_format = RIGHT_LEFT, bits = 32.
        // 1 Sample = 4 bytes. Stereo Frame = 8 bytes.
        // inBuf is int32_t.

        int32_t outBuf[64 * 2];

        for (int i = 0; i < samplesRead; i += 2) { // Step by 2 for Stereo
          // Convert input to float (-1.0 to 1.0)
          // Input is 32-bit.
          int32_t left = inBuf[i];
          // int32_t right = inBuf[i+1];
          // Mix to Mono for Tape Processor
          float input = (float)left / 2147483648.0f;

          // Process
          float processed = input;
          if (!isBypassed && tape) {
            processed = tape->process(input);
          }
          processed = tanhf(processed);

          // Output
          int32_t sample = (int32_t)(processed * masterVolume * VOL_SCALE);
          outBuf[i] = sample;
          outBuf[i + 1] = sample;
        }

        // Write Output (with Timeout)
        size_t bytesWritten;
        i2s_write(I2S_PORT, outBuf, bytesRead, &bytesWritten,
                  pdMS_TO_TICKS(50));

      } else {
        // Timeout reading? Probably no clock or no data.
        // Yield to avoid Watchdog starvation if loop is tight
        vTaskDelay(pdMS_TO_TICKS(10));
      }
    }
  }

  // Se o MP3 consumir muito tempo, adicionar yield ou delay mínimo se buffer
  // vazio? AudioGeneratorMP3::loop() processa um frame. Se isso for rápido
  // demais, ocupará 100% CPU se Buffer I2S estiver vazio. Mas como nosso
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
    neopixelWrite(RGB_PIN, r / 10, g / 10, b / 10);
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// ============================================
// SERIAL COMMANDS (Mantido igual)
// ============================================
void processSerial() {
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    input.toLowerCase();

    if (input == "list" || input == "l") {
      Serial.println("\n--- PARAMETERS ---");
      Serial.printf("PSRAM Free: %d bytes\n", ESP.getFreePsram());
      Serial.printf("Mode: %s\n", p_delayActive ? "ECHO ON" : "BYPASS (SAT)");
      Serial.printf("Bypass: %s\n", isBypassed ? "ON" : "OFF");
      Serial.printf("\n%-12s %4s %10s\n", "Param", "%", "Value");

      reportParam("mix", (int)round(p_mix * 100.0f), p_mix, "");
      reportParam("feedback", (int)round((p_feedback / 1.1f) * 100.0f),
                  p_feedback, "");
      reportParam("vol", (int)round(masterVolume * 100.0f), masterVolume, "");

      reportParam("flutterd", (int)round((p_flutterDepth / 2.0f) * 100.0f),
                  p_flutterDepth, "");
      reportParam("flutterr",
                  (int)round(((p_flutterRate - 0.1f) / 19.9f) * 100.0f),
                  p_flutterRate, "Hz");
      reportParam("wowd", (int)round((p_wowDepth / 2.0f) * 100.0f), p_wowDepth,
                  "");
      reportParam("wowr", (int)round(((p_wowRate - 0.1f) / 4.9f) * 100.0f),
                  p_wowRate, "Hz");

      reportParam("dropout", (int)round(p_dropoutSeverity * 100.0f),
                  p_dropoutSeverity, "");
      reportParam("drive", (int)round(((p_drive - 1.0f) / 9.0f) * 100.0f),
                  p_drive, "x");
      reportParam("noise", (int)round((p_noise / 0.5f) * 100.0f), p_noise, "");

      reportParam("age", (int)round(p_tapeAge * 100.0f), p_tapeAge, "");
      reportParam("speed", (int)round(p_tapeSpeed * 100.0f), p_tapeSpeed, "");
      reportParam("bump", (int)round((p_headBump / 10.0f) * 100.0f), p_headBump,
                  "dB");
      reportParam("azimuth", (int)round(p_azimuth * 100.0f), p_azimuth, "");

      reportParamInt("time", -1, (int)p_delayTime, "ms");
      reportParamInt("heads", -1, p_activeHeads, "(mask)");

      reportParamInt("bpm", -1, (int)p_bpm, "BPM");
      // Campos de melodia removidos do report se não usados, mas struct mantém
      Serial.printf("%-12s %s\n", "headmode",
                    p_headsMusical ? "MUSICAL" : "TIME");

      printHeadTimes(globalParams);

      Serial.println("\n--- COMMANDS ---");
      Serial.println("Syntax: <command> <value>");
      Serial.println("\nGENERAL:");
      Serial.println("  mode <delay|saturation>  - Toggle delay effect");
      Serial.println("  bypass [on|off]          - Toggle audio bypass");
      Serial.println("  vol <0-100>              - Set master volume");
      Serial.println("  play                     - Restart MP3 playback");
      Serial.println("\nDELAY / MULTI-HEAD:");
      Serial.println("  mix <0-100>              - Set delay dry/wet mix");
      Serial.println("  time <10-2000>           - Set base delay time (ms)");
      Serial.println("  feedback <0-100>         - Set delay feedback");
      Serial.println("  heads <1-7>              - Set active heads (bitmask)");
      Serial.println("  bpm <30-300>             - Set BPM for musical mode");
      Serial.println("  headmode <musical|time>  - Set head spacing mode");
      Serial.println("\nTAPE EMULATION:");
      Serial.println("  drive <0-100>            - Tape saturation amount");
      Serial.println("  age <0-100>              - Tape age (HF response)");
      Serial.println("  speed <0-100>            - Tape speed (fidelity)");
      Serial.println("  noise <0-100>            - Tape hiss level");
      Serial.println("  flutterd, flutterr <0-100> - Flutter depth and rate");
      Serial.println("  wowd, wowr <0-100>         - Wow depth and rate");
      Serial.println("  dropout <0-100>          - Signal dropout severity");
      Serial.println("  bump <0-100>             - Head bump amount (LF)");
      Serial.println("  azimuth <0-100>          - Head alignment error");
      Serial.println("------------------------");
      return;
    }

    int spaceIndex = input.indexOf(' ');
    String command =
        (spaceIndex == -1) ? input : input.substring(0, spaceIndex);
    String valueStr = input.substring(spaceIndex + 1);
    int value = valueStr.toInt();

    // --- COMMAND PARSING ---
    if (command == "heads") {
      if (value >= 1 && value <= 7) {
        p_activeHeads = value;
        reportParamInt("heads", -1, p_activeHeads, "(mask)");
      } else {
        Serial.println("Use: heads <1-7> (1=A, 2=B, 4=C, sums allowed)");
      }
    } else if (command == "mode") {
      if (valueStr == "delay") {
        p_delayActive = true;
        Serial.println("Mode: DELAY");
      } else {
        p_delayActive = false;
        Serial.println("Mode: SATURATION");
      }
    } else if (command == "d" || command == "delay") {
      Serial.println("\n--- DELAY PARAMS ---");
      reportParamInt("time", -1, (int)p_delayTime, "ms");
      reportParam("feedback", (int)round((p_feedback / 1.1f) * 100.0f),
                  p_feedback, "");
      reportParam("mix", (int)round(p_mix * 100.0f), p_mix, "");
      reportParamInt("heads", -1, p_activeHeads, "(mask)");
      reportParamInt("bpm", -1, (int)p_bpm, "BPM");
      Serial.printf("%-12s %s\n", "headmode",
                    p_headsMusical ? "MUSICAL" : "TIME");
      printHeadTimes(globalParams);
      Serial.println("--------------------");
    } else if (command == "bypass") {
      if (valueStr == "on" || value == 1) {
        isBypassed = true;
      } else if (valueStr == "off" || value == 0) {
        isBypassed = false;
      } else {
        isBypassed = !isBypassed;
      }
      Serial.printf("Bypass: %s\n", isBypassed ? "ON" : "OFF");
    } else if (command == "t" || command == "tape") {
      Serial.println("\n--- TAPE PARAMS ---");
      reportParam("flutterd", (int)round((p_flutterDepth / 2.0f) * 100.0f),
                  p_flutterDepth, "");
      reportParam("flutterr",
                  (int)round(((p_flutterRate - 0.1f) / 19.9f) * 100.0f),
                  p_flutterRate, "Hz");
      reportParam("wowd", (int)round((p_wowDepth / 2.0f) * 100.0f), p_wowDepth,
                  "");
      reportParam("wowr", (int)round(((p_wowRate - 0.1f) / 4.9f) * 100.0f),
                  p_wowRate, "Hz");
      reportParam("age", (int)round(p_tapeAge * 100.0f), p_tapeAge, "");
      reportParam("speed", (int)round(p_tapeSpeed * 100.0f), p_tapeSpeed, "");
      reportParam("bump", (int)round((p_headBump / 10.0f) * 100.0f), p_headBump,
                  "dB");
      reportParam("azimuth", (int)round(p_azimuth * 100.0f), p_azimuth, "");
      reportParam("dropout", (int)round(p_dropoutSeverity * 100.0f),
                  p_dropoutSeverity, "");
      reportParam("noise", (int)round((p_noise / 0.5f) * 100.0f), p_noise, "");
      reportParam("drive", (int)round(((p_drive - 1.0f) / 9.0f) * 100.0f),
                  p_drive, "x");
      Serial.println("--------------------");
    } else if (command == "mix") {
      p_mix = constrain(value / 100.0f, 0.0f, 1.0f);
      reportParam("mix", value, p_mix, "");
    } else if (command == "time") {
      p_delayTime = constrain((float)value, 10.0f, 2000.0f);
      reportParamInt("time", -1, (int)p_delayTime, "ms");
    } else if (command == "feedback") {
      p_feedback = constrain((value / 100.0f) * 1.1f, 0.0f, 1.1f);
      reportParam("feedback", value, p_feedback, "");
    } else if (command == "volume" || command == "vol") {
      masterVolume = constrain(value / 100.0f, 0.0f, 1.0f);
      reportParam("vol", value, masterVolume, "");
    } else if (command == "bpm") {
      p_bpm = constrain(value, 30, 300);
      // setBPM removed call as MelodyGen is gone, but we keep param for
      // HeadMode
      reportParamInt("bpm", -1, (int)p_bpm, "BPM");
    } else if (command == "headmode" || command == "headm") {
      if (valueStr == "musical" || valueStr == "on") {
        p_headsMusical = true;
        Serial.println("HeadMode: MUSICAL");
      } else if (valueStr == "time" || valueStr == "off") {
        p_headsMusical = false;
        Serial.println("HeadMode: TIME");
      } else {
        p_headsMusical = !p_headsMusical;
        Serial.printf("HeadMode: %s\n", p_headsMusical ? "MUSICAL" : "TIME");
      }
    }
    // Removed: MelodyGen specific commands (scale, key, waveform, mood, rythm)
    // Leaving logic in Serial for now won't break anything, just won't do
    // anything audible or removing them to clean up. Let's remove them to avoid
    // confusion.
    else if (command == "flutterdepth" || command == "flutterd") {
      p_flutterDepth = constrain((value / 100.0f) * 2.0f, 0.0f, 2.0f);
      reportParam("flutterd", value, p_flutterDepth, "");
    } else if (command == "flutterrate" || command == "flutterr") {
      p_flutterRate = 0.1f + constrain((value / 100.0f) * 19.9f, 0.0f, 19.9f);
      reportParam("flutterr", value, p_flutterRate, "Hz");
    } else if (command == "wowdepth" || command == "wowd") {
      p_wowDepth = constrain((value / 100.0f) * 2.0f, 0.0f, 2.0f);
      reportParam("wowd", value, p_wowDepth, "");
    } else if (command == "wowrate" || command == "wowr") {
      p_wowRate = 0.1f + constrain((value / 100.0f) * 4.9f, 0.0f, 4.9f);
      reportParam("wowr", value, p_wowRate, "Hz");
    } else if (command == "dropout") {
      p_dropoutSeverity = constrain(value / 100.0f, 0.0f, 1.0f);
      reportParam("dropout", value, p_dropoutSeverity, "");
    } else if (command == "drive") {
      p_drive = 1.0f + constrain((value / 100.0f) * 9.0f, 0.0f, 9.0f);
      reportParam("drive", value, p_drive, "x");
    } else if (command == "noise") {
      p_noise = constrain((value / 100.0f) * 0.5f, 0.0f, 0.5f);
      reportParam("noise", value, p_noise, "");
    } else if (command == "lpf") {
      p_tapeAge = 1.0f - constrain((value / 100.0f), 0.0f, 1.0f);
      reportParam("lpf", value, p_tapeAge, "");
    } else if (command == "age") {
      p_tapeAge = constrain((value / 100.0f), 0.0f, 1.0f);
      reportParam("age", value, p_tapeAge, "");
    } else if (command == "speed") {
      p_tapeSpeed = constrain((value / 100.0f), 0.0f, 1.0f);
      reportParam("speed", value, p_tapeSpeed, "");
    } else if (command == "bump") {
      p_headBump = constrain((value / 100.0f) * 10.0f, 0.0f, 10.0f);
      reportParam("bump", value, p_headBump, "dB");
    } else if (command == "azimuth") {
      p_azimuth = constrain((value / 100.0f), -1.0f, 1.0f);
      reportParam("azimuth", value, p_azimuth, "");
    } else if (command == "play") {
      // Default play starts current source, or switches to MP3?
      // Let's make 'play' just trigger restart of whatever is active
      p_triggerPlay = true;
      Serial.println("Command: PLAY (Restart)");
    } else if (command == "synth") {
      p_source = SOURCE_SYNTH;
      Serial.println("Source: SYNTH");
    } else if (command == "mp3") {
      if (p_source == SOURCE_MP3) {
        p_triggerPlay = true; // Restart if already active
      } else {
        p_source = SOURCE_MP3; // Switch (transition logic handles start)
      }
      Serial.println("Source: MP3");
    } else if (command == "input" || command == "ext") {
      p_source = SOURCE_I2S_IN;
      Serial.println("Source: I2S INPUT");
    } else {
      Serial.println("Cmd: list, play, synth, mp3, ...");
    }
  }
}

void loop() {
  processSerial();

  // Atualiza Struct de forma thread-safe
  if (xSemaphoreTake(paramMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    globalParams.flutterDepth = p_flutterDepth;
    globalParams.wowDepth = p_wowDepth;
    globalParams.dropoutSeverity = p_dropoutSeverity;
    globalParams.drive = p_drive;
    globalParams.noise = p_noise;
    globalParams.flutterRate = p_flutterRate;
    globalParams.wowRate = p_wowRate;

    globalParams.tapeSpeed = p_tapeSpeed;
    globalParams.tapeAge = p_tapeAge;
    globalParams.headBumpAmount = p_headBump;
    globalParams.azimuthError = p_azimuth;

    globalParams.delayActive = p_delayActive;
    globalParams.delayTimeMs = p_delayTime;
    globalParams.feedback = p_feedback;
    globalParams.dryWet = p_mix;
    globalParams.activeHeads = p_activeHeads;

    globalParams.bpm = p_bpm;
    globalParams.headsMusical = p_headsMusical;

    if (globalParams.drive > 10.0f)
      globalParams.drive = 10.0f;
    if (globalParams.noise > 0.5f)
      globalParams.noise = 0.5f;

    xSemaphoreGive(paramMutex);
  }

  vTaskDelay(pdMS_TO_TICKS(20));
}

// ============================================
// SETUP
// ============================================
void setup() {
  // Force 240MHz
  setCpuFrequencyMhz(240);

  Serial.begin(115200);
  delay(1000);
  Serial.printf("--- STARTING MULTI-HEAD TAPE (MP3 VERSION) ---\n");
  Serial.printf("CPU Freq: %d MHz\n", getCpuFrequencyMhz());

  if (!LittleFS.begin()) {
    Serial.println("LittleFS Mount Failed!");
    // We can try to format but better to warn
    // LittleFS.format();
  } else {
    Serial.println("LittleFS Mounted.");
    if (LittleFS.exists("/demo.mp3")) {
      Serial.println("Found /demo.mp3");
    } else {
      Serial.println("ERROR: /demo.mp3 not found!");
    }
  }

  paramMutex = xSemaphoreCreateMutex();

  // CHECK BOOT BUTTON (GPIO 0) FOR MASTER/SLAVE SELECTION
  // Default: SLAVE (according to request)
  // Hold BOOT during reset: MASTER
  pinMode(0, INPUT_PULLUP);
  bool forceMaster = (digitalRead(0) == LOW); // Button pressed is LOW

  Serial.printf("I2S Config strategy: %s\n",
                forceMaster ? "MASTER (Override)" : "SLAVE (Default)");

  i2s_mode_t activeMode;
  if (forceMaster) {
    activeMode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX);
  } else {
    activeMode = (i2s_mode_t)(I2S_MODE_SLAVE | I2S_MODE_TX | I2S_MODE_RX);
  }

  /*
     USER REQUEST CONFIGURATION:
     Mode: Slave (Default compliant, override with BOOT button)
     Protocol: I2S (Philips) -> I2S_COMM_FORMAT_STAND_I2S
     Sample Rate: 48kHz
     Bits: 32-bit (Pico uses 32-clock slots)
  */

  i2s_config_t i2s_config = {
      // .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX), // ORIGINAL
      // (Commented for reference)
      .mode = activeMode,
      .sample_rate = SAMPLE_RATE,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
      .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
      .communication_format = I2S_COMM_FORMAT_STAND_I2S, // Philips
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      // Buffers de DMA aumentados para segurança com PSRAM
      .dma_buf_count = I2S_DMA_BUF_COUNT,
      .dma_buf_len = I2S_DMA_BUF_LEN,
      .use_apll = true};
  i2s_pin_config_t pin_config = {.bck_io_num = I2S_BCLK,
                                 .ws_io_num = I2S_LRCK,
                                 .data_out_num = I2S_DOUT,
                                 .data_in_num = I2S_DIN};
  i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_PORT, &pin_config);
  i2s_zero_dma_buffer(I2S_PORT);

  // Prioridade aumentada (3) para evitar gargalo de áudio
  xTaskCreatePinnedToCore(audioTask, "Audio", 8192, NULL, 3, NULL, 0);
  xTaskCreatePinnedToCore(ledTask, "LED", 2048, NULL, 1, NULL, 1);

  Serial.println("SYSTEM READY.");
}