// Frozen Phase 6.1 diagnostic model, fa43cde. Never used by production.
struct TapeMagnetics {
  const FastTanhLUT* lut = nullptr;
  float fs = 48000.0f;
  float m = 0.0f;
  float biasPhase = 0.0f;
  float biasGain = 0.0f;

  float drive = 1.0f;
  float biasAmount = 0.08f;
  float biasHz = 18000.0f;
  float biasPhaseInc = 18000.0f / 48000.0f;
  float biasGainStep = 1.0f / 24.0f;
  float coercivity = 0.12f;
  float remanence = 0.985f;
  float satNorm = 1.0f;

  void updateParams(float sampleRate, const TapeParams& params) {
    fs = std::max(1.0f, sampleRate);
    float driveNorm = clampf(params.drive * 0.01f, 0.0f, 1.0f);
    float ageNorm = clampf(params.tapeAge * 0.01f, 0.0f, 1.0f);
    float speedNorm = clampf(params.tapeSpeed * 0.01f, 0.0f, 1.0f);

    drive = 0.9f + driveNorm * 0.35f;
    biasAmount = 0.035f + (1.0f - speedNorm) * 0.035f + driveNorm * 0.035f;
    biasHz = std::min(18000.0f, fs * 0.45f);
    biasPhaseInc = biasHz / fs;
    // Linear full-scale transition. Clamping reaches exact zero in silence.
    biasGainStep = 1.0f / std::max(1.0f, std::round(fs * 0.0005f));
    coercivity = 0.07f + ageNorm * 0.10f;
    remanence = 0.970f + ageNorm * 0.024f;
    satNorm = 1.0f / std::max(0.55f, 0.82f + 0.18f * remanence);
  }

  HYDRA_DSP_IRAM HYDRA_AUDIO_INLINE float process(float x, bool allowBias) {
    if (!lut) return x;
    biasPhase += biasPhaseInc;
    while (biasPhase >= 1.0f) {
      biasPhase -= 1.0f;
    }
    float tri = 4.0f * std::fabs(biasPhase - 0.5f) - 1.0f;

    const bool biasRequested = allowBias && std::fabs(x) > 1e-7f;
    biasGain = clampf(biasGain + (biasRequested ? biasGainStep : -biasGainStep), 0.0f, 1.0f);
    float xb = x * drive + (biasAmount * biasGain * tri);
    float h = xb + coercivity * m;
    float y = lut->process(h);

    m = remanence * m + (1.0f - remanence) * y;

    return satNorm * (0.82f * y + 0.18f * m);
  }

  inline void flushDenormals() {
    if (std::fabs(m) < 1e-20f) m = 0.0f;
  }

  inline void reset() {
    m = 0.0f;
    biasPhase = 0.0f;
    biasGain = 0.0f;
  }
};
