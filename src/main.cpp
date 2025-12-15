// ============================================
// ANALOG TAPE EMULATOR - MULTI-HEAD EDITION
// ============================================
// HARDWARE: YD-ESP32-S3 (N16R8) + DAC PCM5102
// ============================================

#include <Arduino.h>
#include <driver/i2s.h>
#include <math.h>
#include <esp_task_wdt.h>
#include "esp_heap_caps.h" 
#include "TapeDelay.h"
#include "MelodyGen.h"

// ============================================
// PINOUT
// ============================================
#define I2S_BCLK        15
#define I2S_LRCK        16
#define I2S_DOUT        17
#define I2S_DIN         18 
#define RGB_PIN         48

// ============================================
// AUDIO CONFIG
// ============================================
#define SAMPLE_RATE     48000
#define I2S_BUFFER_SIZE 128
#define I2S_PORT        I2S_NUM_0

// ============================================
// CONTROLS & GLOBALS
// ============================================
#define BOOT_BUTTON_PIN 0
volatile bool isBypassed = false;
volatile float masterVolume = 0.3f; 

// Parameters are now managed via the struct, but we keep local variables for Serial parsing
float p_flutterDepth    = 0.35f;
float p_wowDepth        = 0.45;
float p_dropoutSeverity = 0.90f;
float p_drive           = 3.0f;
float p_noise           = 0.035f;
float p_flutterRate     = 6.0f;
float p_wowRate         = 0.5f;

// New parameters
float p_tapeSpeed       = 0.5f; // 0.0 to 1.0 (center)
float p_tapeAge         = 0.5f; // 0.0 to 1.0
float p_headBump        = 3.0f; // dB
float p_azimuth         = 0.0f;
float p_bpm             = 120.0f;
bool  p_headsMusical    = false;

// Melody Params
float p_mood            = 0.5f;
float p_rhythm          = 0.5f;

// --- Delay Params ---
bool  p_delayActive     = false;
float p_delayTime       = 500.0f;
float p_feedback        = 0.3f;
float p_mix             = 1.0f;
int   p_activeHeads     = 4; // Bitmask: 1=Head A, 2=Head B, 4=Head C (Default: C only)

TapeParams globalParams = {
    p_flutterDepth, p_wowDepth, p_dropoutSeverity, p_drive, p_noise,
    p_tapeSpeed, p_tapeAge, p_headBump, p_azimuth,
    p_flutterRate, p_wowRate,
    p_delayActive, p_delayTime, p_feedback, p_mix,
    p_activeHeads
    , p_bpm, p_headsMusical
};

// Global pointers for tasks
TapeModel* tape = nullptr;
MelodyGen* melody = nullptr;

// Mutex for thread-safe parameter updates
SemaphoreHandle_t paramMutex;

// Helper: report parameter changes. percent < 0 => N/A
static void reportParam(const char* name, int percent, float value, const char* unit = "") {
    if (percent >= 0) Serial.printf("%-12s %3d%% => %6.2f %s\n", name, percent, value, unit);
    else Serial.printf("%-12s N/A  => %6.2f %s\n", name, value, unit);
}
static void reportParamInt(const char* name, int percent, int value, const char* unit = "") {
    if (percent >= 0) Serial.printf("%-12s %3d%% => %d %s\n", name, percent, value, unit);
    else Serial.printf("%-12s N/A  => %d %s\n", name, value, unit);
}

// Compute nominal head time (ms) for a given head bit (1,2,4)
static float headTimeMs(const TapeParams& p, int headBit) {
    if (!p.delayActive) return 0.0f;
    if (p.headsMusical) {
        float beatMs = 60000.0f / p.bpm; // quarter note in ms
        if (headBit == 1) return beatMs * (1.0f/3.0f); // eighth-note triplet
        if (headBit == 2) return beatMs * 0.75f;       // dotted eighth
        if (headBit == 4) return beatMs * 1.0f;        // quarter
    } else {
        if (headBit == 1) return p.delayTimeMs * 0.33f;
        if (headBit == 2) return p.delayTimeMs * 0.66f;
        if (headBit == 4) return p.delayTimeMs * 1.0f;
    }
    return 0.0f;
}

static void printHeadTimes(const TapeParams& p) {
    float tA = headTimeMs(p, 1);
    float tB = headTimeMs(p, 2);
    float tC = headTimeMs(p, 4);
    Serial.printf("Head Times (ms): A:%6.1f  B:%6.1f  C:%6.1f\n", tA, tB, tC);
}

// ============================================
// TASKS
// ============================================
void audioTask(void* parameter) {
    esp_task_wdt_add(NULL);
    tape = new TapeModel(SAMPLE_RATE);
    melody = new MelodyGen(SAMPLE_RATE);
    
    // Init Melody Defaults
    melody->setBPM(p_bpm);
    melody->setMood(p_mood);
    melody->setRhythm(p_rhythm);

    // Initial param update
    tape->updateParams(globalParams);
    tape->updateFilters();

    size_t bytes;
    int32_t* buf = (int32_t*)malloc(I2S_BUFFER_SIZE * 2 * sizeof(int32_t));
    const double INT32_MAX_D = 2147483647.0;
    const float VOL_SCALE = (float)(INT32_MAX_D * 0.90f); 

    TapeParams localParams;

    while (true) {
        esp_task_wdt_reset(); 

        // Thread-safe update of parameters
        if (xSemaphoreTake(paramMutex, 0) == pdTRUE) {
            localParams = globalParams;
            xSemaphoreGive(paramMutex);
            tape->updateParams(localParams);
            // Note: updateFilters is expensive, so we might want to call it only when relevant params change.
            // But since this is a demo, we rely on the loop calling it or just static filters for now unless explicitly changed?
            // The provided TapeModel doesn't auto-update filters in updateParams.
            // We'll add logic in the loop to trigger it if needed, or just let it be initial.
            // For now, let's assume filters are updated if we detect speed/age change,
            // but checking change inside audio loop is standard.
            // However, TapeModel::updateFilters() involves trig and pow calls, maybe too heavy for every block?
            // Actually it's just coeffs calc, so it's fine once per block (128 samples).
            tape->updateFilters();
        }

        for (int i = 0; i < I2S_BUFFER_SIZE; i++) {
            float gen = melody->next();
            float processed = gen;
            if (!isBypassed && tape) {
                 processed = tape->process(gen);
            }
            if (processed > 1.0f) processed = 1.0f;
            else if (processed < -1.0f) processed = -1.0f;
            int32_t sample = (int32_t)(processed * masterVolume * VOL_SCALE);
            buf[i*2]   = sample; buf[i*2+1] = sample;
        }
        i2s_write(I2S_PORT, buf, I2S_BUFFER_SIZE * 8, &bytes, portMAX_DELAY);
    }
    if (buf) free(buf);
}

void ledTask(void* parameter) {
    uint8_t hue = 0;
    while(true) {
        hue++;
        uint8_t r, g, b;
        if(hue < 85) { r = hue * 3; g = 255 - hue * 3; b = 0; } 
        else if(hue < 170) { r = 255 - (hue - 85) * 3; g = 0; b = (hue - 85) * 3; } 
        else { r = 0; g = (hue - 170) * 3; b = 255 - (hue - 170) * 3; }
        neopixelWrite(RGB_PIN, r/10, g/10, b/10); 
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// ============================================
// SERIAL COMMANDS (MULTI-HEAD)
// ============================================
void processSerial() {
    if (Serial.available() > 0) {
        String input = Serial.readStringUntil('\n');
        input.trim(); input.toLowerCase();

        if (input == "list" || input == "l") {
            Serial.println("\n--- PARAMETERS ---");
            Serial.printf("PSRAM Free: %d bytes\n", ESP.getFreePsram());
            Serial.printf("Mode: %s\n", p_delayActive ? "ECHO ON" : "BYPASS (SAT)");
            Serial.printf("Bypass: %s\n", isBypassed ? "ON" : "OFF");
            Serial.printf("\n%-12s %4s %10s\n", "Param", "%", "Value");

            reportParam("mix", (int)round(p_mix * 100.0f), p_mix, "");
            reportParam("feedback", (int)round((p_feedback / 1.1f) * 100.0f), p_feedback, "");
            reportParam("vol", (int)round(masterVolume * 100.0f), masterVolume, "");

            reportParam("flutterd", (int)round((p_flutterDepth / 2.0f) * 100.0f), p_flutterDepth, "");
            reportParam("fluterr", (int)round(((p_flutterRate - 0.1f) / 19.9f) * 100.0f), p_flutterRate, "Hz");
            reportParam("wowd", (int)round((p_wowDepth / 2.0f) * 100.0f), p_wowDepth, "");
            reportParam("wowr", (int)round(((p_wowRate - 0.1f) / 4.9f) * 100.0f), p_wowRate, "Hz");

            reportParam("dropout", (int)round(p_dropoutSeverity * 100.0f), p_dropoutSeverity, "");
            reportParam("drive", (int)round(((p_drive - 1.0f) / 9.0f) * 100.0f), p_drive, "x");
            reportParam("noise", (int)round((p_noise / 0.5f) * 100.0f), p_noise, "");

            reportParam("age", (int)round(p_tapeAge * 100.0f), p_tapeAge, "");
            reportParam("speed", (int)round(p_tapeSpeed * 100.0f), p_tapeSpeed, "");
            reportParam("bump", (int)round((p_headBump / 10.0f) * 100.0f), p_headBump, "dB");
            reportParam("azimuth", (int)round(p_azimuth * 100.0f), p_azimuth, "");

            reportParamInt("time", -1, (int)p_delayTime, "ms");
            reportParamInt("heads", -1, p_activeHeads, "(mask)");

            reportParamInt("bpm", -1, (int)p_bpm, "BPM");
            reportParam("mood", (int)(p_mood*100), p_mood, "");
            reportParam("rythm", (int)(p_rhythm*100), p_rhythm, "");
            Serial.printf("%-12s %s\n", "headmode", p_headsMusical ? "MUSICAL" : "TIME");

            // Show nominal head times (ms)
            printHeadTimes(globalParams);

            Serial.println("------------------------");
            return;
        }

        int spaceIndex = input.indexOf(' ');
        String command = (spaceIndex == -1) ? input : input.substring(0, spaceIndex);
        String valueStr = input.substring(spaceIndex + 1);
        int value = valueStr.toInt();

        // --- COMANDO HEADS ---
        if (command == "heads") {
            if (value >= 1 && value <= 7) {
                p_activeHeads = value;
                reportParamInt("heads", -1, p_activeHeads, "(mask)");
            } else {
                Serial.println("Use: heads <1-7> (1=A, 2=B, 4=C, sums allowed)");
            }
        }
        else if (command == "mode") {
            if (valueStr == "delay") { p_delayActive = true; Serial.println("Mode: DELAY"); }
            else { p_delayActive = false; Serial.println("Mode: SATURATION"); }
        }
        else if (command == "d" || command == "delay") {
            // Delay related parameters
            Serial.println("\n--- DELAY PARAMS ---");
            reportParamInt("time", -1, (int)p_delayTime, "ms");
            reportParam("feedback", (int)round((p_feedback / 1.1f) * 100.0f), p_feedback, "");
            reportParam("mix", (int)round(p_mix * 100.0f), p_mix, "");
            reportParamInt("heads", -1, p_activeHeads, "(mask)");
            reportParamInt("bpm", -1, (int)p_bpm, "BPM");
            Serial.printf("%-12s %s\n", "headmode", p_headsMusical ? "MUSICAL" : "TIME");
            printHeadTimes(globalParams);
            Serial.println("--------------------");
        }
        else if (command == "bypass") {
            // Accept: bypass on/off, bypass 1/0, or just 'bypass' to toggle
            if (valueStr == "on" || value == 1) { isBypassed = true; }
            else if (valueStr == "off" || value == 0) { isBypassed = false; }
            else { isBypassed = !isBypassed; }
            Serial.printf("Bypass: %s\n", isBypassed ? "ON" : "OFF");
        }
        else if (command == "t" || command == "tape") {
            // Tape-related parameters
            Serial.println("\n--- TAPE PARAMS ---");
            reportParam("flutterd", (int)round((p_flutterDepth / 2.0f) * 100.0f), p_flutterDepth, "");
            reportParam("fluterr", (int)round(((p_flutterRate - 0.1f) / 19.9f) * 100.0f), p_flutterRate, "Hz");
            reportParam("wowd", (int)round((p_wowDepth / 2.0f) * 100.0f), p_wowDepth, "");
            reportParam("wowr", (int)round(((p_wowRate - 0.1f) / 4.9f) * 100.0f), p_wowRate, "Hz");
            reportParam("age", (int)round(p_tapeAge * 100.0f), p_tapeAge, "");
            reportParam("speed", (int)round(p_tapeSpeed * 100.0f), p_tapeSpeed, "");
            reportParam("bump", (int)round((p_headBump / 10.0f) * 100.0f), p_headBump, "dB");
            reportParam("azimuth", (int)round(p_azimuth * 100.0f), p_azimuth, "");
            reportParam("dropout", (int)round(p_dropoutSeverity * 100.0f), p_dropoutSeverity, "");
            reportParam("noise", (int)round((p_noise / 0.5f) * 100.0f), p_noise, "");
            reportParam("drive", (int)round(((p_drive - 1.0f) / 9.0f) * 100.0f), p_drive, "x");
            Serial.println("--------------------");
        }
        else if (command == "mix") { p_mix = constrain(value / 100.0f, 0.0f, 1.0f); reportParam("mix", value, p_mix, ""); }
        else if (command == "time") { p_delayTime = constrain((float)value, 10.0f, 2000.0f); reportParamInt("time", -1, (int)p_delayTime, "ms"); }
        else if (command == "feedback") { p_feedback = constrain((value / 100.0f) * 1.1f, 0.0f, 1.1f); reportParam("feedback", value, p_feedback, ""); }
        else if (command == "volume" || command == "vol") { masterVolume = constrain(value / 100.0f, 0.0f, 1.0f); reportParam("vol", value, masterVolume, ""); }
        else if (command == "bpm") { 
            p_bpm = constrain(value, 30, 300); 
            if(melody) melody->setBPM(p_bpm);
            reportParamInt("bpm", -1, (int)p_bpm, "BPM"); 
        }
        else if (command == "headmode" || command == "headm") {
            if (valueStr == "musical" || valueStr == "on" ) { p_headsMusical = true; Serial.println("HeadMode: MUSICAL"); }
            else if (valueStr == "time" || valueStr == "off") { p_headsMusical = false; Serial.println("HeadMode: TIME"); }
            else { p_headsMusical = !p_headsMusical; Serial.printf("HeadMode: %s\n", p_headsMusical ? "MUSICAL" : "TIME"); }
        }
        else if (command == "waveform") {
            if (melody) {
                if (valueStr == "sine") melody->setWaveform(SINE);
                else if (valueStr == "sawtooth") melody->setWaveform(SAWTOOTH);
                else if (valueStr == "triangle") melody->setWaveform(TRIANGLE);
                else if (valueStr == "square") melody->setWaveform(SQUARE);
                Serial.println("Waveform Changed");
            }
        }
        // --- MELODY COMMANDS ---
        else if (command == "scale") {
            if (melody) {
                if (valueStr == "major") melody->setScale(MAJOR);
                else if (valueStr == "minor") melody->setScale(MINOR);
                else if (valueStr == "pentatonic") melody->setScale(PENTATONIC_MIN);
                else if (valueStr == "blues") melody->setScale(BLUES);
                Serial.printf("Scale: %s\n", valueStr.c_str());
            }
        }
        else if (command == "key") {
            if (melody) {
                // Simple parsing for C, D, E...
                int k = 0;
                if (valueStr.startsWith("c")) k = 0;
                else if (valueStr.startsWith("d")) k = 2;
                else if (valueStr.startsWith("e")) k = 4;
                else if (valueStr.startsWith("f")) k = 5;
                else if (valueStr.startsWith("g")) k = 7;
                else if (valueStr.startsWith("a")) k = 9;
                else if (valueStr.startsWith("b")) k = 11;
                melody->setKey(k);
                Serial.printf("Key set (approx): %s\n", valueStr.c_str());
            }
        }
        else if (command == "mood") {
            p_mood = constrain(value / 100.0f, 0.0f, 1.0f);
            if (melody) melody->setMood(p_mood);
            reportParam("mood", value, p_mood, "");
        }
        else if (command == "rythm" || command == "rhythm") {
            p_rhythm = constrain(value / 100.0f, 0.0f, 1.0f);
            if (melody) melody->setRhythm(p_rhythm);
            reportParam("rythm", value, p_rhythm, "");
        }
        else if (command == "flutterdepth" || command == "flutterd") { p_flutterDepth = constrain((value / 100.0f) * 2.0f, 0.0f, 2.0f); reportParam("flutterd", value, p_flutterDepth, ""); }
        else if (command == "flutterrate" || command == "fluterr") { p_flutterRate = 0.1f + constrain((value / 100.0f) * 19.9f, 0.0f, 19.9f); reportParam("fluterr", value, p_flutterRate, "Hz"); }
        else if (command == "wowdepth" || command == "wowd") { p_wowDepth = constrain((value / 100.0f) * 2.0f, 0.0f, 2.0f); reportParam("wowd", value, p_wowDepth, ""); }
        else if (command == "wowrate" || command == "wowr") { p_wowRate = 0.1f + constrain((value / 100.0f) * 4.9f, 0.0f, 4.9f); reportParam("wowr", value, p_wowRate, "Hz"); }
        else if (command == "dropout") { p_dropoutSeverity = constrain(value / 100.0f, 0.0f, 1.0f); reportParam("dropout", value, p_dropoutSeverity, ""); }
        else if (command == "drive") { p_drive = 1.0f + constrain((value / 100.0f) * 9.0f, 0.0f, 9.0f); reportParam("drive", value, p_drive, "x"); }
        else if (command == "noise") { p_noise = constrain((value / 100.0f) * 0.5f, 0.0f, 0.5f); reportParam("noise", value, p_noise, ""); }
        else if (command == "lpf") { p_tapeAge = 1.0f - constrain((value / 100.0f), 0.0f, 1.0f); reportParam("lpf", value, p_tapeAge, ""); }
        else if (command == "age") { p_tapeAge = constrain((value / 100.0f), 0.0f, 1.0f); reportParam("age", value, p_tapeAge, ""); }
        else if (command == "speed") { p_tapeSpeed = constrain((value / 100.0f), 0.0f, 1.0f); reportParam("speed", value, p_tapeSpeed, ""); }
        else if (command == "bump") { p_headBump = constrain((value / 100.0f) * 10.0f, 0.0f, 10.0f); reportParam("bump", value, p_headBump, "dB"); }
        else if (command == "azimuth") { p_azimuth = constrain((value / 100.0f), -1.0f, 1.0f); reportParam("azimuth", value, p_azimuth, ""); }
        else { Serial.println("Cmd desconhecido. Use 'list'"); }
    }
}

void loop() { 
    bool newState = (digitalRead(BOOT_BUTTON_PIN) == LOW);
    if (newState != isBypassed) {
        isBypassed = newState;
        Serial.printf("Bypass: %s\n", isBypassed ? "ON" : "OFF");
    }

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

        if (globalParams.drive > 10.0f) globalParams.drive = 10.0f;
        if (globalParams.noise > 0.5f) globalParams.noise = 0.5f;

        xSemaphoreGive(paramMutex);
    }

    vTaskDelay(pdMS_TO_TICKS(20)); 
}

// ============================================
// SETUP
// ============================================
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("--- STARTING MULTI-HEAD TAPE ---");

    paramMutex = xSemaphoreCreateMutex();

    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 4, .dma_buf_len = 128, .use_apll = true
    };
    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_BCLK, .ws_io_num = I2S_LRCK,
        .data_out_num = I2S_DOUT, .data_in_num = I2S_DIN
    };
    i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_PORT, &pin_config);
    i2s_zero_dma_buffer(I2S_PORT);

    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

    xTaskCreatePinnedToCore(audioTask, "Audio", 8192, NULL, 2, NULL, 0);
    xTaskCreatePinnedToCore(ledTask,   "LED",   2048, NULL, 1, NULL, 1);

    Serial.println("SYSTEM READY.");
}
