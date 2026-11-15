#include "check.h"
#include "hydra_dsp.h"
#include "hydra_dsp_esp32_adapter.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
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

  CHECK(hydra_dsp_set_params_v1_3(h, &p) == 0);
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

  CHECK(hydra_dsp_set_params_v1_3(h, &p) == 0);
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

  // Com stereoWarp = 1.0f, canais L e R devem se diferenciar
  hydra_dsp_handle* h2 = create_test_handle();
  p.warpStereo = 1.0f;
  CHECK(hydra_dsp_set_params_v1_3(h2, &p) == 0);
  CHECK(hydra_dsp_commit(h2) == 0);

  std::vector<float> outL_stereo(kN), outR_stereo(kN);
  CHECK(hydra_dsp_process(h2, in.data(), in.data(), outL_stereo.data(), outR_stereo.data(), kN) == 0);

  float diffSum = 0.0f;
  for (size_t i = 500; i < kN; ++i) {
    diffSum += std::fabs(outL_stereo[i] - outR_stereo[i]);
  }
  std::cout << "[Warp Stereo Test] DiffSum L/R: " << diffSum << "\n";
  CHECK(diffSum > 1.0f);

  hydra_dsp_destroy(h);
  hydra_dsp_destroy(h2);
}

// 4. Validar Feedback em múltiplos níveis: 0%, 50%, 80%, 95%
void test_warp_feedback_levels() {
  // Teste 4a: warpFeedback = 0% -> sem realimentação fantasma (não cai no feedback tradicional)
  {
    hydra_dsp_handle* h = create_test_handle();
    hydra_dsp_params p{};
    p.drive = 20.0f;
    p.delayActive = 1.0f;
    p.delayTimeMs = 100.0f;
    p.dryWet = 100.0f;
    p.delayWet = 100.0f;
    p.feedback = 80.0f;      // Feedback tradicional ALTO no parâmetro de fita
    p.warpActive = 1.0f;
    p.warpSpeed = 1.0f;
    p.warpGrainMs = 60.0f;   // ~2880 amostras
    p.warpFeedback = 0.0f;   // WARP FEEDBACK É ZERO!

    CHECK(hydra_dsp_set_params_v1_3(h, &p) == 0);
    CHECK(hydra_dsp_commit(h) == 0);

    // Injetar Dirac no início
    constexpr uint32_t kFrames = 24000;
    std::vector<float> inL(kFrames, 0.0f), inR(kFrames, 0.0f);
    inL[0] = 0.9f; inR[0] = 0.9f;
    std::vector<float> outL(kFrames), outR(kFrames);

    for (uint32_t pos = 0; pos < kFrames; pos += 64) {
      CHECK(hydra_dsp_process(h, inL.data() + pos, inR.data() + pos,
                              outL.data() + pos, outR.data() + pos, 64) == 0);
    }

    // Após o término do grão primário (~60ms = 2880 amostras + margem de segurança),
    // a cauda deve decair para zero absoluto (sem recirculações adicionais).
    float lateEnergy = 0.0f;
    for (uint32_t i = 10000; i < kFrames; ++i) {
      lateEnergy += std::fabs(outL[i]) + std::fabs(outR[i]);
    }
    std::cout << "[Warp Feedback 0%] Late Energy: " << lateEnergy << "\n";
    CHECK(lateEnergy < 1e-3f);

    hydra_dsp_destroy(h);
  }

  // Teste 4b: warpFeedback = 50% e 80% -> realimentação sustentada
  for (float fbVal : {50.0f, 80.0f}) {
    hydra_dsp_handle* h = create_test_handle();
    hydra_dsp_params p{};
    p.drive = 20.0f;
    p.delayActive = 1.0f;
    p.delayTimeMs = 100.0f;
    p.dryWet = 100.0f;
    p.delayWet = 100.0f;
    p.warpActive = 1.0f;
    p.warpSpeed = 0.5f;
    p.warpGrainMs = 60.0f;
    p.warpFeedback = fbVal;

    CHECK(hydra_dsp_set_params_v1_3(h, &p) == 0);
    CHECK(hydra_dsp_commit(h) == 0);

    constexpr uint32_t kFrames = 24000;
    std::vector<float> inL(kFrames, 0.0f), inR(kFrames, 0.0f);
    inL[0] = 0.9f; inR[0] = 0.9f;
    std::vector<float> outL(kFrames), outR(kFrames);

    for (uint32_t pos = 0; pos < kFrames; pos += 64) {
      CHECK(hydra_dsp_process(h, inL.data() + pos, inR.data() + pos,
                              outL.data() + pos, outR.data() + pos, 64) == 0);
    }

    float lateEnergy = 0.0f;
    for (uint32_t i = 10000; i < kFrames; ++i) {
      lateEnergy += std::fabs(outL[i]) + std::fabs(outR[i]);
      CHECK(!std::isnan(outL[i]) && !std::isinf(outL[i]));
      CHECK(std::fabs(outL[i]) <= 1.0f);
    }
    CHECK(lateEnergy > 0.01f); // Comprova recirculação ativa

    hydra_dsp_destroy(h);
  }

  // Teste 4c: warpFeedback = 95% -> estabilidade extrema sem estouro durante 5 segundos
  {
    hydra_dsp_handle* h = create_test_handle();
    hydra_dsp_params p{};
    p.drive = 30.0f;
    p.delayActive = 1.0f;
    p.delayTimeMs = 100.0f;
    p.dryWet = 80.0f;
    p.delayWet = 100.0f;
    p.warpActive = 1.0f;
    p.warpSpeed = 0.5f;
    p.warpGrainMs = 70.0f;
    p.warpFeedback = 95.0f; // 95% de realimentação

    CHECK(hydra_dsp_set_params_v1_3(h, &p) == 0);
    CHECK(hydra_dsp_commit(h) == 0);

    // 5 segundos = 240.000 amostras
    constexpr uint32_t kLongFrames = 240000;
    std::vector<float> inL(64, 0.0f), inR(64, 0.0f);
    std::vector<float> outL(64), outR(64);
    inL[0] = 0.95f; inR[0] = 0.95f;

    for (uint32_t pos = 0; pos < kLongFrames; pos += 64) {
      CHECK(hydra_dsp_process(h, inL.data(), inR.data(), outL.data(), outR.data(), 64) == 0);
      inL[0] = 0.0f; inR[0] = 0.0f; // Apenas impulso inicial

      for (int i = 0; i < 64; ++i) {
        CHECK(!std::isnan(outL[i]) && !std::isinf(outL[i]));
        CHECK(!std::isnan(outR[i]) && !std::isinf(outR[i]));
        CHECK(std::fabs(outL[i]) <= 1.0f);
        CHECK(std::fabs(outR[i]) <= 1.0f);
      }
    }

    hydra_dsp_destroy(h);
  }
}

// 5. Validar todos os 4 modos do Warp (Hermite vs Raw Int, 2-Taps vs 1-Tap)
void test_warp_all_modes() {
  for (int m = 0; m <= 3; ++m) {
    hydra_dsp_handle* h = create_test_handle();
    hydra_dsp_params p{};
    p.drive = 25.0f;
    p.delayActive = 1.0f;
    p.delayTimeMs = 120.0f;
    p.dryWet = 100.0f;
    p.delayWet = 100.0f;
    p.warpActive = 1.0f;
    p.warpSpeed = 1.0f;
    p.warpGrainMs = 80.0f;
    p.warpStereo = 0.25f;
    p.warpFeedback = 40.0f;
    p.warpMode = static_cast<float>(m);

    CHECK(hydra_dsp_set_params_v1_3(h, &p) == 0);
    CHECK(hydra_dsp_commit(h) == 0);

    constexpr uint32_t kN = 4096;
    std::vector<float> in(kN), outL(kN), outR(kN);
    for (uint32_t i = 0; i < kN; ++i) {
      in[i] = std::sin(kTwoPi * 440.0f * static_cast<float>(i) / kSampleRate) * 0.5f;
    }

    CHECK(hydra_dsp_process(h, in.data(), in.data(), outL.data(), outR.data(), kN) == 0);

    float energyL = 0.0f, energyR = 0.0f;
    for (uint32_t i = 500; i < kN; ++i) {
      CHECK(!std::isnan(outL[i]) && !std::isinf(outL[i]));
      CHECK(!std::isnan(outR[i]) && !std::isinf(outR[i]));
      energyL += std::fabs(outL[i]);
      energyR += std::fabs(outR[i]);
    }

    std::cout << "[Warp Mode " << m << "] Energy L: " << energyL << " | R: " << energyR << "\n";
    CHECK(energyL > 1.0f);
    CHECK(energyR > 1.0f);

    hydra_dsp_destroy(h);
  }

  // Testar transição dinâmica e suave entre modos (0 -> 1 -> 2 -> 3 -> 0) com sinal contínuo
  {
    hydra_dsp_handle* h = create_test_handle();
    hydra_dsp_params p{};
    p.drive = 20.0f;
    p.delayActive = 1.0f;
    p.delayTimeMs = 200.0f;
    p.dryWet = 100.0f;
    p.delayWet = 100.0f;
    p.warpActive = 1.0f;
    p.warpSpeed = 0.5f;
    p.warpGrainMs = 80.0f;
    p.warpMode = 0.0f;
    CHECK(hydra_dsp_set_params_v1_3(h, &p) == 0);
    CHECK(hydra_dsp_commit(h) == 0);

    constexpr uint32_t kBlock = 64;
    std::vector<float> in(kBlock, 0.4f);
    std::vector<float> outL(kBlock), outR(kBlock);

    for (float modeTarget : {1.0f, 2.0f, 3.0f, 0.0f}) {
      CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_WARP_MODE, modeTarget) == 0);
      CHECK(hydra_dsp_commit(h) == 0);
      for (int b = 0; b < 20; ++b) {
        CHECK(hydra_dsp_process(h, in.data(), in.data(), outL.data(), outR.data(), kBlock) == 0);
        for (uint32_t i = 0; i < kBlock; ++i) {
          CHECK(!std::isnan(outL[i]) && !std::isinf(outL[i]));
          CHECK(!std::isnan(outR[i]) && !std::isinf(outR[i]));
        }
      }
    }
    hydra_dsp_destroy(h);
  }
}

// 6. Validar continuidade de fase na modulação do tamanho do grão (warpGrainMs)
void test_warp_grain_phase_continuity() {
  hydra_dsp_handle* h = create_test_handle();
  hydra_dsp_params p{};
  p.drive = 20.0f;
  p.delayActive = 1.0f;
  p.delayTimeMs = 150.0f;
  p.dryWet = 100.0f;
  p.delayWet = 100.0f;
  p.warpActive = 1.0f;
  p.warpSpeed = 1.0f;
  p.warpGrainMs = 40.0f;

  CHECK(hydra_dsp_set_params_v1_3(h, &p) == 0);
  CHECK(hydra_dsp_commit(h) == 0);

  constexpr uint32_t kBlock = 64;
  std::vector<float> in(kBlock, 0.5f);
  std::vector<float> outL(kBlock), outR(kBlock);

  // Inicializar buffer com áudio contínuo
  for (int b = 0; b < 20; ++b) {
    CHECK(hydra_dsp_process(h, in.data(), in.data(), outL.data(), outR.data(), kBlock) == 0);
  }

  // Variação dinâmica contínua de warpGrainMs
  float prevSample = outL[kBlock - 1];
  for (int step = 0; step < 40; ++step) {
    float newGrain = 40.0f + static_cast<float>(step) * 5.0f; // 40ms -> 240ms
    CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_WARP_GRAIN_MS, newGrain) == 0);
    CHECK(hydra_dsp_commit(h) == 0);

    CHECK(hydra_dsp_process(h, in.data(), in.data(), outL.data(), outR.data(), kBlock) == 0);

    // Assert ausência de saltos descontínuos bruscos entre blocos
    float deltaFirst = std::fabs(outL[0] - prevSample);
    CHECK(deltaFirst < 1.0f);

    for (uint32_t i = 1; i < kBlock; ++i) {
      float delta = std::fabs(outL[i] - outL[i - 1]);
      CHECK(!std::isnan(outL[i]));
      CHECK(delta < 0.8f);
    }
    prevSample = outL[kBlock - 1];
  }

  hydra_dsp_destroy(h);
}

// 7. Validar alternância Liga/Desliga do Warp com áudio presente no buffer
void test_warp_toggle_with_audio_in_buffer() {
  hydra_dsp_handle* h = create_test_handle();
  hydra_dsp_params p{};
  p.drive = 20.0f;
  p.delayActive = 1.0f;
  p.delayTimeMs = 150.0f;
  p.dryWet = 50.0f;
  p.delayWet = 100.0f;
  p.warpActive = 0.0f; // Inicia em Delay tradicional

  CHECK(hydra_dsp_set_params_v1_3(h, &p) == 0);
  CHECK(hydra_dsp_commit(h) == 0);

  constexpr uint32_t kN = 2048;
  std::vector<float> in(kN), outL(kN), outR(kN);
  for (uint32_t i = 0; i < kN; ++i) {
    in[i] = std::sin(kTwoPi * 350.0f * static_cast<float>(i) / kSampleRate) * 0.4f;
  }

  // Preencher buffer com fita tradicional
  CHECK(hydra_dsp_process(h, in.data(), in.data(), outL.data(), outR.data(), kN) == 0);

  // Alternar dinamicamente para Warp
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_WARP_ACTIVE, 1.0f) == 0);
  CHECK(hydra_dsp_commit(h) == 0);
  CHECK(hydra_dsp_process(h, in.data(), in.data(), outL.data(), outR.data(), kN) == 0);
  for (uint32_t i = 0; i < kN; ++i) {
    CHECK(!std::isnan(outL[i]) && !std::isinf(outL[i]));
    CHECK(std::fabs(outL[i]) <= 1.0f);
  }

  // Desativar Warp de volta para delay de fita
  CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_WARP_ACTIVE, 0.0f) == 0);
  CHECK(hydra_dsp_commit(h) == 0);
  CHECK(hydra_dsp_process(h, in.data(), in.data(), outL.data(), outR.data(), kN) == 0);
  for (uint32_t i = 0; i < kN; ++i) {
    CHECK(!std::isnan(outL[i]) && !std::isinf(outL[i]));
    CHECK(std::fabs(outL[i]) <= 1.0f);
  }

  hydra_dsp_destroy(h);
}

// 8. Validar saltos abruptos de Speed e Stereo durante áudio contínuo
void test_warp_abrupt_speed_and_stereo_changes() {
  hydra_dsp_handle* h = create_test_handle();
  hydra_dsp_params p{};
  p.drive = 20.0f;
  p.delayActive = 1.0f;
  p.delayTimeMs = 120.0f;
  p.dryWet = 100.0f;
  p.delayWet = 100.0f;
  p.warpActive = 1.0f;
  p.warpSpeed = 1.0f;
  p.warpStereo = 0.0f;

  CHECK(hydra_dsp_set_params_v1_3(h, &p) == 0);
  CHECK(hydra_dsp_commit(h) == 0);

  constexpr uint32_t kBlock = 64;
  std::vector<float> in(kBlock, 0.4f);
  std::vector<float> outL(kBlock), outR(kBlock);

  const float speeds[] = {0.1f, 1.8f, 0.5f, 2.0f, 0.0f, 1.5f};
  const float stereos[] = {1.0f, 0.0f, 0.8f, 0.2f, 0.5f, 0.0f};

  for (size_t i = 0; i < sizeof(speeds) / sizeof(speeds[0]); ++i) {
    CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_WARP_SPEED, speeds[i]) == 0);
    CHECK(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_WARP_STEREO, stereos[i]) == 0);
    CHECK(hydra_dsp_commit(h) == 0);

    for (int b = 0; b < 16; ++b) {
      CHECK(hydra_dsp_process(h, in.data(), in.data(), outL.data(), outR.data(), kBlock) == 0);
      for (int k = 0; k < 64; ++k) {
        CHECK(!std::isnan(outL[k]) && !std::isinf(outL[k]));
        CHECK(std::fabs(outL[k]) <= 1.0f);
      }
    }
  }

  hydra_dsp_destroy(h);
}

void test_esp32_adapter_applies_warp_only_changes() {
  constexpr float sampleRate = 44100.0f;
  hydra_dsp_params params{};
  params.drive = 20.0f;
  params.tapeSpeed = 50.0f;
  params.delayActive = 1.0f;
  params.delayTimeMs = 100.0f;
  params.dryWet = 100.0f;
  params.delayWet = 100.0f;
  params.activeHeads = 4.0f;
  params.bpm = 120.0f;
  params.tone = 50.0f;
  params.warpActive = 1.0f;
  params.warpSpeed = 1.0f;
  params.warpGrainMs = 80.0f;

  struct WarpChange {
    float hydra_dsp_params::* member;
    float value;
  };
  const WarpChange changes[] = {
    {&hydra_dsp_params::warpActive, 0.0f},
    {&hydra_dsp_params::warpSpeed, 0.5f},
    {&hydra_dsp_params::warpGrainMs, 120.0f},
    {&hydra_dsp_params::warpStereo, 0.75f},
    {&hydra_dsp_params::warpFeedback, 80.0f},
    {&hydra_dsp_params::warpMode, 2.0f},
  };
  constexpr uint32_t frames = 44100 / 2;
  std::vector<float> input(frames);
  for (uint32_t frame = 0; frame < frames; ++frame) {
    input[frame] = 0.5f * std::sin(kTwoPi * 440.0f * frame / sampleRate);
  }
  for (const auto& change : changes) {
    HydraDspEsp32Adapter control(sampleRate), automated(sampleRate);
    CHECK(control.isValid() && automated.isValid());
    control.updateParams(params);
    automated.updateParams(params);
    float controlL, controlR, actualL, actualR;
    float peakDifference = 0.0f;
    for (uint32_t frame = 0; frame < frames; ++frame) {
      if (frame == 8192) {
        auto changed = params;
        changed.*(change.member) = change.value;
        automated.updateParams(changed);
      }
      const float sample = input[frame];
      control.processStereo(sample, sample, &controlL, &controlR);
      automated.processStereo(sample, sample, &actualL, &actualR);
      const float difference = std::max(std::fabs(controlL - actualL), std::fabs(controlR - actualR));
      if (frame < 8192) CHECK(difference == 0.0f);
      peakDifference = std::max(peakDifference, difference);
    }
    CHECK(peakDifference > 0.001f);
  }
}

void test_api_backward_compatibility() {
  hydra_dsp_handle* h = create_test_handle();
  CHECK(hydra_dsp_get_param_count() == 36);

  for (int i = 0; i < HYDRA_DSP_PARAM_COUNT; ++i) {
    const auto id = static_cast<hydra_dsp_param_id>(i);
    hydra_dsp_param_spec spec{};
    CHECK(hydra_dsp_get_param_spec_for_handle(h, id, &spec) == 0);
    CHECK(spec.id == id);
    CHECK(spec.name && spec.name[0]);
    float value = -1.0f;
    CHECK(hydra_dsp_get_parameter(h, id, &value) == 0);
    CHECK(value == spec.default_value);
    CHECK(hydra_dsp_set_parameter(h, id, spec.max_value) == 0);
    CHECK(hydra_dsp_get_parameter(h, id, &value) == 0);
    CHECK(value == (id == HYDRA_DSP_PARAM_RESET ? 0.0f : spec.max_value));
    CHECK(hydra_dsp_set_parameter(h, id, spec.default_value) == 0);
  }

  hydra_dsp_param_spec modeSpec{};
  CHECK(hydra_dsp_get_param_spec(HYDRA_DSP_PARAM_WARP_MODE, &modeSpec) == 0);
  CHECK(modeSpec.min_value == 0.0f && modeSpec.max_value == 3.0f);
  CHECK(modeSpec.default_value == 0.0f);

  hydra_dsp_params_v1_2 legacy{};
  legacy.drive = 45.0f;
  legacy.springMix = 31.0f;
  hydra_dsp_params full{};
  full.drive = 55.0f;
  full.warpActive = 1.0f;
  full.warpSpeed = 1.5f;
  full.warpGrainMs = 120.0f;
  full.warpStereo = 0.8f;
  full.warpFeedback = 75.0f;
  full.warpMode = 3.0f;

  auto expect = [&](hydra_dsp_param_id id, float expected) {
    float value = -1.0f;
    CHECK(hydra_dsp_get_parameter(h, id, &value) == 0);
    CHECK(value == expected);
  };
  auto expectWarpDefaults = [&] {
    for (int i = HYDRA_DSP_PARAM_WARP_ACTIVE; i < HYDRA_DSP_PARAM_COUNT; ++i) {
      const auto id = static_cast<hydra_dsp_param_id>(i);
      hydra_dsp_param_spec spec{};
      CHECK(hydra_dsp_get_param_spec(id, &spec) == 0);
      expect(id, spec.default_value);
    }
  };

  CHECK(hydra_dsp_set_params_v1_3(h, &full) == 0);
  expect(HYDRA_DSP_PARAM_DRIVE, 55.0f);
  expect(HYDRA_DSP_PARAM_WARP_ACTIVE, 1.0f);
  expect(HYDRA_DSP_PARAM_WARP_MODE, 3.0f);
  CHECK(hydra_dsp_set_params(h, &legacy) == 0);
  expect(HYDRA_DSP_PARAM_DRIVE, 45.0f);
  expect(HYDRA_DSP_PARAM_SPRING_MIX, 31.0f);
  expectWarpDefaults();

  CHECK(hydra_dsp_set_params_sized(h, &full, sizeof(full)) == 0);
  expect(HYDRA_DSP_PARAM_WARP_SPEED, 1.5f);
  expect(HYDRA_DSP_PARAM_WARP_GRAIN_MS, 120.0f);
  expect(HYDRA_DSP_PARAM_WARP_STEREO, 0.8f);
  expect(HYDRA_DSP_PARAM_WARP_FEEDBACK, 75.0f);
  CHECK(hydra_dsp_set_params_v1_2(h, &legacy) == 0);
  expectWarpDefaults();

  CHECK(hydra_dsp_set_params_sized(h, &full, sizeof(full) - 1) == 0);
  expectWarpDefaults();
  CHECK(hydra_dsp_set_params_sized(h, &legacy, sizeof(legacy)) == 0);
  expect(HYDRA_DSP_PARAM_DRIVE, 45.0f);
  CHECK(hydra_dsp_set_params_sized(h, &legacy, sizeof(legacy) - 1) == -2);
  CHECK(hydra_dsp_set_params_sized(nullptr, &legacy, sizeof(legacy)) == -1);
  CHECK(hydra_dsp_set_params_sized(h, nullptr, sizeof(full)) == -1);
  CHECK(hydra_dsp_set_params(h, nullptr) == -1);
  CHECK(hydra_dsp_set_params_v1_2(nullptr, &legacy) == -1);
  CHECK(hydra_dsp_set_params_v1_3(h, nullptr) == -1);
  expect(HYDRA_DSP_PARAM_DRIVE, 45.0f);
  CHECK(hydra_dsp_commit(h) == 0);
  hydra_dsp_destroy(h);
}

} // namespace

int main() {
  std::cout << "Running Accidental Warp V1.3 Comprehensive DSP Test Suite...\n";
  test_dual_tap_window_complementarity();
  test_warp_ascending_pitch_shift();
  test_warp_stereo_phase_offset();
  test_warp_feedback_levels();
  test_warp_all_modes();
  test_warp_grain_phase_continuity();
  test_warp_toggle_with_audio_in_buffer();
  test_warp_abrupt_speed_and_stereo_changes();
  test_esp32_adapter_applies_warp_only_changes();
  test_api_backward_compatibility();
  std::cout << "All Accidental Warp V1.3 Tests PASSED successfully!\n";
  return 0;
}
