#ifndef FRIPPERTRONICS_CORE_H
#define FRIPPERTRONICS_CORE_H

#include <Arduino.h>
#include <math.h>

#define AUDIO_INLINE inline __attribute__((always_inline))

// ============================================================================
// TABELA DE SENO OTIMIZADA (Utilitário)
// ============================================================================
class FastSine {
private:
  float table[1024];
  const int MASK = 1023;

public:
  FastSine() {
    for (int i = 0; i < 1024; i++)
      table[i] = sinf(i * (2.0f * PI / 1024.0f));
  }
  AUDIO_INLINE IRAM_ATTR float getRad(float rad) {
    // 1024 / 2PI = 162.97466
    int index = (int)(rad * 162.97466f) & MASK;
    return table[index];
  }
};
extern FastSine fastSin;

// ============================================================================
// DIFFUSER (Tape Diffusion / Blur)
// ============================================================================
// Non-redundant component - TapeDelay doesn't have a diffuser.
class Diffuser {
public:
  static constexpr int BUFFER_SIZE = 256;
  float buf[3][BUFFER_SIZE];
  int idx[3];
  float gain;

  Diffuser() : gain(0.7f) {
    memset(buf, 0, sizeof(buf));
    memset(idx, 0, sizeof(idx));
  }

  AUDIO_INLINE IRAM_ATTR float process(float x) {
    static constexpr int delays[3] = {113, 181, 233}; // Prime numbers
    // Compile-time safety check
    static_assert(delays[0] < BUFFER_SIZE && delays[1] < BUFFER_SIZE && delays[2] < BUFFER_SIZE,
                  "Delay values must be less than BUFFER_SIZE");
    for (int i = 0; i < 3; i++) {
      int d = delays[i];
      int &w = idx[i];
      float y = buf[i][w];
      float out = -gain * x + y;
      buf[i][w] = x + gain * out;
      x = out;
      w = (w + 1) % d;
    }
    return x;
  }
};

#endif