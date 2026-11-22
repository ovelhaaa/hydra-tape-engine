// Frozen diagnostic LUT from fa43cde; production never includes this file.
struct FastTanhLUT {
  static constexpr int N = 1024;
  float table[N + 1]{};

  void init() {
    for (int i = 0; i <= N; ++i) {
      float x = -4.0f + 8.0f * (float(i) / float(N));
      table[i] = std::tanh(x);
    }
  }

  HYDRA_AUDIO_INLINE float process(float x) const {
    x = clampf(x, -4.0f, 4.0f);
    float u = (x + 4.0f) * (float(N) * 0.125f);
    int i = static_cast<int>(u);
    if (i >= N) return table[N];
    float f = u - float(i);
    return table[i] + f * (table[i + 1] - table[i]);
  }
};

