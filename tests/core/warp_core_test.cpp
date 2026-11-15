#include "check.h"
#include "hydra_dsp.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

constexpr float kSampleRate = 48000.0f;
constexpr float kTwoPi = 6.28318530717958647692f;

hydra_dsp_handle* create_test_handle() {
  hydra_dsp_handle* h = nullptr;
  CHECK(hydra_dsp_create(kSampleRate, 2000.0f, &h) == 0);
  CHECK(h != nullptr);
  CHECK(hydra_dsp_prepare(h, 64, HYDRA_DSP_CHANNELS_STEREO) == 0);
  return h;
}

// 1. Validar complementaridade matemática das janelas triangulares
void test_dual_tap_window_complementarity() {
  constexpr int kSteps = 1000;
  for (int i = 0; i < kSteps; ++i) {
    float phase1 = static_cast<float>(i) / static_cast<float>(kSteps);
    float phase2 = phase1 + 0.5f;
    if (phase2 >= 1.0f) phase2 -= 1.0f;

    float gain1 = 1.0f - std::fabs(2.0f * phase1 - 1.0f);
    float gain2 = 1.0f - std::fabs(2.0f * phase2 - 1.0f);

    float sum = gain1 + gain2;
    CHECK(std::fabs(sum - 1.0f) < 1e-6f);
    CHECK(gain1 >= 0.0f && gain1 <= 1.0f);
    CHECK(gain2 >= 0.0f && gain2 <= 1.0f);
  }
}

// 2. Validar que o Accidental Warp produz transposição ASCENDENTE (+1 oitava a speedMultiplier=1.0)
void test_warp_ascending_pitch_shift() {
  hydra_dsp_handle* h = create_test_handle();

  hydra_dsp_params p{};
  p.flutterDepth = 0.0f;
  p.wowDepth = 0.0f;
  p.dropoutSeverity = 0.0f;
  p.drive = 20.0f;
  p.noise = 0.0f;
  p.tapeSpeed = 50.0f;
  p.tapeAge = 0.0f;
  p.headBumpAmount = 0.0f;
  p.azimuthError = 0.0f;
  p.flutterRate = 0.1f;
  p.wowRate = 0.1f;
  p.delayActive = 1.0f;
  p.delayTimeMs = 200.0f;
  p.feedback = 0.0f;
  p.dryWet = 100.0f;
  p.delayWet = 100.0f;
  p.activeHeads = 4.0f;
  p.bpm = 120.0f;
  p.headsMusical = 0.0f;
  p.guitarFocus = 0.0f;
  p.tone = 100.0f;
  p.pingPong = 0.0f;
  p.freeze = 0.0f;
  p.reverse = 0.0f;
  p.reverseSmear = 0.0f;
  p.spring = 0.0f;
  p.warpActive = 1.0f;
  p.warpSpeed = 1.0f;     // v_eff = 1 + 1.0 = 2.0x (+12 semitons / +1 oitava)
  p.warpGrainMs = 100.0f; // 100 ms de grão
  p.warpStereo = 0.0f;
  p.warpFeedback = 0.0f;
  p.warpMode = 0.0f;      // 2-Taps Musical

  CHECK(hydra_dsp_set_params(h, &p) == 0);
  CHECK(hydra_dsp_commit(h) == 0);

  // Estímulo de entrada: senoide pura de 440 Hz
  constexpr float kFreqIn = 440.0f;
  constexpr uint32_t kTotalFrames = 24000; // 0.5 s a 48 kHz
  std::vector<float> inL(kTotalFrames), inR(kTotalFrames);
  std::vector<float> outL(kTotalFrames), outR(kTotalFrames);

  for (uint32_t i = 0; i < kTotalFrames; ++i) {
    float s = std::sin(kTwoPi * kFreqIn * static_cast<float>(i) / kSampleRate);
    inL[i] = s * 0.5f;
    inR[i] = s * 0.5f;
  }

  // Processar em blocos de 64 amostras
  constexpr uint32_t kBlock = 64;
  for (uint32_t pos = 0; pos < kTotalFrames; pos += kBlock) {
    CHECK(hydra_dsp_process(h, inL.data() + pos, inR.data() + pos,
                            outL.data() + pos, outR.data() + pos, kBlock) == 0);
  }

  // Analisar contagem de cruzamentos por zero no trecho estabilizado (entre 0.25s e 0.45s)
  uint32_t startIdx = 12000;
  uint32_t endIdx = 21600;
  uint32_t zeroCrossings = 0;
  for (uint32_t i = startIdx + 1; i < endIdx; ++i) {
    if ((outL[i - 1] < 0.0f && outL[i] >= 0.0f) ||
        (outL[i - 1] >= 0.0f && outL[i] < 0.0f)) {
      zeroCrossings++;
    }
  }

  float durationSeconds = static_cast<float>(endIdx - startIdx) / kSampleRate;
  float measuredFreq = static_cast<float>(zeroCrossings) / (2.0f * durationSeconds);

  // A frequência de entrada era 440 Hz. A leitura com v_eff=2.0x deve produzir ~880 Hz (+1 oitava).
  // Se fosse descendente (pitch down), a frequência seria ~220 Hz.
  std::cout << "[Warp Pitch Test] Freq Entrada: " << kFreqIn
            << " Hz | Freq Medida Saida: " << measuredFreq << " Hz\n";
  CHECK(measuredFreq > 750.0f && measuredFreq < 980.0f);

  hydra_dsp_destroy(h);
}

// 3. Validar Stereo Warp (deslocamento de fase sem desafinação entre canais)
void test_warp_stereo_phase_offset() {
  hydra_dsp_handle* h = create_test_handle();

  hydra_dsp_params p{};
  p.drive = 20.0f;
  p.delayActive = 1.0f;
  p.delayTimeMs = 150.0f;
  p.dryWet = 100.0f;
  p.delayWet = 100.0f;
  p.warpActive = 1.0f;
  p.warpSpeed = 1.0f;
  p.warpGrainMs = 80.0f;
  p.warpStereo = 0.0f; // Mono centrado

  CHECK(hydra_dsp_set_params(h, &p) == 0);
  CHECK(hydra_dsp_commit(h) == 0);

  constexpr uint32_t kN = 2048;
  std::vector<float> in(kN), outL_mono(kN), outR_mono(kN);
  for (uint32_t i = 0; i < kN; ++i) {
    in[i] = std::sin(kTwoPi * 300.0f * static_cast<float>(i) / kSampleRate) * 0.5f;
  }
  CHECK(hydra_dsp_process(h, in.data(), in.data(), outL_mono.data(), outR_mono.data(), kN) == 0);

  // Com stereoWarp = 0, canais L e R devem ser idênticos
  for (size_t i = 500; i < kN; ++i) {
    CHECK(std::fabs(outL_mono[i] - outR_mono[i]) < 1e-5f);
  }

  // Agora criar novo handle ou resetar e habilitar stereoWarp = 1.0f
  hydra_dsp_handle* h2 = create_test_handle();
  p.warpStereo = 1.0f;
  CHECK(hydra_dsp_set_params(h2, &p) == 0);
  CHECK(hydra_dsp_commit(h2) == 0);

  std::vector<float> outL_stereo(kN), outR_stereo(kN);
  CHECK(hydra_dsp_process(h2, in.data(), in.data(), outL_stereo.data(), outR_stereo.data(), kN) == 0);

  // Com stereoWarp = 1.0, deve haver decorrelação temporal entre L e R
  float diffSum = 0.0f;
  for (size_t i = 500; i < kN; ++i) {
    diffSum += std::fabs(outL_stereo[i] - outR_stereo[i]);
  }
  std::cout << "[Warp Stereo Test] DiffSum L/R: " << diffSum << "\n";
  CHECK(diffSum > 1.0f);

  hydra_dsp_destroy(h);
  hydra_dsp_destroy(h2);
}

// 4. Validar estabilidade de realimentação e cascata ascendente (Feedback sem runaway nem NaN)
void test_warp_feedback_stability() {
  hydra_dsp_handle* h = create_test_handle();

  hydra_dsp_params p{};
  p.delayActive = 1.0f;
  p.delayTimeMs = 100.0f;
  p.dryWet = 50.0f;
  p.delayWet = 100.0f;
  p.feedback = 80.0f;
  p.warpActive = 1.0f;
  p.warpSpeed = 0.5f;     // +7 semitons por ciclo de repetição
  p.warpGrainMs = 80.0f;
  p.warpFeedback = 80.0f; // Realimentação ativa de 80%

  CHECK(hydra_dsp_set_params(h, &p) == 0);
  CHECK(hydra_dsp_commit(h) == 0);

  // Injetar impulso curto (Dirac)
  std::vector<float> inL(48000, 0.0f), inR(48000, 0.0f);
  inL[0] = 0.8f;
  inR[0] = 0.8f;

  std::vector<float> outL(48000), outR(48000);
  for (uint32_t pos = 0; pos < 48000; pos += 64) {
    CHECK(hydra_dsp_process(h, inL.data() + pos, inR.data() + pos,
                            outL.data() + pos, outR.data() + pos, 64) == 0);
  }

  // Verificar ausência estrita de NaN, Inf e estouro de amplitude
  for (uint32_t i = 0; i < 48000; ++i) {
    CHECK(!std::isnan(outL[i]) && !std::isinf(outL[i]));
    CHECK(!std::isnan(outR[i]) && !std::isinf(outR[i]));
    CHECK(std::fabs(outL[i]) <= 1.0f);
    CHECK(std::fabs(outR[i]) <= 1.0f);
  }

  hydra_dsp_destroy(h);
}

// 5. Validar acesso a parâmetros da API C para Warp
void test_warp_param_c_api() {
  hydra_dsp_handle* h = create_test_handle();

  float v = -1.0f;
  CHECK(hydra_dsp_get_parameter(h, HYDRA_DSP_PARAM_WARP_ACTIVE, &v) == 0);
  CHECK(v == 0.0f);

  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_WARP_ACTIVE, 1.0f) == 0);
  CHECK(hydra_dsp_get_parameter(h, HYDRA_DSP_PARAM_WARP_ACTIVE, &v) == 0);
  CHECK(v == 1.0f);

  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_WARP_SPEED, 1.5f) == 0);
  CHECK(hydra_dsp_get_parameter(h, HYDRA_DSP_PARAM_WARP_SPEED, &v) == 0);
  CHECK(std::fabs(v - 1.5f) < 1e-5f);

  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_WARP_GRAIN_MS, 120.0f) == 0);
  CHECK(hydra_dsp_get_parameter(h, HYDRA_DSP_PARAM_WARP_GRAIN_MS, &v) == 0);
  CHECK(std::fabs(v - 120.0f) < 1e-5f);

  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_WARP_MODE, 1.0f) == 0);
  CHECK(hydra_dsp_get_parameter(h, HYDRA_DSP_PARAM_WARP_MODE, &v) == 0);
  CHECK(v == 1.0f);

  hydra_dsp_destroy(h);
}

} // namespace

int main() {
  std::cout << "Running Accidental Warp V1 DSP Test Suite...\n";
  test_dual_tap_window_complementarity();
  test_warp_ascending_pitch_shift();
  test_warp_stereo_phase_offset();
  test_warp_feedback_stability();
  test_warp_param_c_api();
  std::cout << "All Accidental Warp V1 Tests PASSED successfully!\n";
  return 0;
}
