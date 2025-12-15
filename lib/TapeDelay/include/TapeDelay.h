#ifndef TAPE_DELAY_IMPROVED_H
#define TAPE_DELAY_IMPROVED_H

#include <Arduino.h>
#include <math.h>
#include "esp_heap_caps.h"

// Macro para garantir inline em funções críticas de áudio
#define AUDIO_INLINE inline __attribute__((always_inline))

// ============================================================================
// DC BLOCKER - Essencial para loops de delay com saturação
// ============================================================================
class DCBlocker {
private:
    float x1, y1;
    const float R = 0.995f;
public:
    DCBlocker() : x1(0), y1(0) {}

    AUDIO_INLINE float process(float input) {
        float output = input - x1 + R * y1;
        x1 = input;
        y1 = output;
        return output;
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

    AUDIO_INLINE float process(float input) {
        float output = b0 * input + b1 * z1 + b2 * z2 - a1 * z1 - a2 * z2;
        // Denormal protection (opcional, mas bom para filtros IIR)
        if (fabsf(output) < 1e-20f) output = 0.0f;
        z2 = z1;
        z1 = input;
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
        if (r & 1) state[0] = white();
        else if (r & 2) state[1] = white();
        else state[2] = white();

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
    DropoutGenerator() : smoothedLevel(1.0f), targetLevel(1.0f),
                         samplesUntilNext(0), dropoutDuration(0),
                         severity(0.5f), seed(987654321) {}

    void setSeverity(float sev) {
        severity = constrain(sev, 0.0f, 1.0f);
    }

    AUDIO_INLINE float process() {
        if (samplesUntilNext <= 0) {
            if (dropoutDuration <= 0) {
                float chance = severity * 0.0005f; // Ajustado para ser probabilidade por sample
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

        float smoothCoeff = (targetLevel < smoothedLevel) ? 0.0005f : 0.002f; // Ataque rápido, release lento
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

    void setCoeff(float coeff) {
        a1 = constrain(coeff, -0.99f, 0.99f);
    }

    void reset() { z1 = 0; }

    AUDIO_INLINE float process(float input) {
        float output = a1 * input + z1;
        z1 = input - a1 * output;
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
    bool  delayActive;
    float delayTimeMs;
    float feedback;
    float dryWet;
    int   activeHeads; // Bitmask: 1=Head1, 2=Head2, 4=Head3
    float bpm; // tempo em BPM, usado quando heads em modo musical
    bool  headsMusical; // se true, usa mapeamento musical para posições das cabeças
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
    AllpassFilter azimuthFilter;
    DCBlocker dcBlocker; // Novo: Previne runaway DC offset

    BiquadFilter headBump;
    BiquadFilter tapeRolloff;
    BiquadFilter outputLPF;

    float* delayLine;
    int32_t bufferSize;
    int32_t writeHead;
    bool usesSPIRAM;

    // Soft clipper assimétrico (mais "quente" que tanh puro)
    AUDIO_INLINE float saturator(float x) {
        if (x > 1.0f) x = 1.0f;
        else if (x < -1.0f) x = -1.0f;

        // Adiciona harmônicos pares leves (assimetria de fita magnetizada)
        float biased = x + 0.1f * x * x;

        // Aproximação rápida de tanh
        // x * (27 + x * x) / (27 + 9 * x * x) é uma boa approx Pade,
        // mas aqui vamos usar um simples soft clip cúbico para performance
        if (biased < -1.5f) return -1.0f;
        if (biased > 1.5f) return 1.0f;
        return biased - (biased * biased * biased) * 0.148f; // Cubic soft clip
    }

    AUDIO_INLINE float readTapeAt(float delaySamples);

public:
    TapeModel(float fs, float maxDelayTimeMs = 2000.0f);
    ~TapeModel();

    void updateFilters();
    void updateParams(const TapeParams& newParams);

    // ATENÇÃO: Esta função deve ir para IRAM se estiver numa interrupção de áudio
    // Implementation in cpp to avoid ODR/relocation issues
    float process(float input);
};

#endif // TAPE_DELAY_IMPROVED_H
