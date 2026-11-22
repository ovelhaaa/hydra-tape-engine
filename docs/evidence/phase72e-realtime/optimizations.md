# Fase 7.2E — Registro Incremental de Otimizações

Todas as otimizações foram aplicadas de forma incremental, uma hipótese por commit, com verificação **bit-exact** contra a Variante C original (`ab7f483115d2c9875cc6e9e0fe16de64f96ce655`) nos 10 cenários da Fase 7.2 e medição física em ESP32-S3 (`COM15`).

## 1. Otimizações Mantidas

| Commit | Hipótese | Escopo | Equivalência C-orig × C-opt | Ganho Físico Medido |
|---|---|---|---|---|
| `cdeb02c` | **H1:** Mover `memset(g_bench.hist, 0, 32768)` para fora da `audioTask` (Core 0 antes de `BENCH_WARMUP`) e corrigir `perf reset` | `src/main.cpp` | Não altera DSP (bit-exact) | `MaxPost` caiu de `650–759 µs` para `20 µs`. T4 `Full Work Peak`: `2.918 -> 2.407 µs` (`0` overruns). T2 `Full Work Peak`: `2.891 -> 2.649 µs`. |
| `54f57f5` | **H2:** `copyCoeffsFrom()` e cache de parâmetros invariantes em `updateFilters()` / `updatePlaybackFilters()` (reduz 12 biquads para 3 em `updatePlaybackFilters` e evita recálculo de `feedbackHPF`, `inputHPF/LPF`, `springLPF[0..5]` quando inalterados) | `dsp/core/tape_core.cpp` | **10/10 BIT-EXACT** (`max_abs=0.0`, `rmse=0.0`) | Reduz trabalho de coeficientes em `updateFilters()` / `pbTransition` em até 4×. |
| `77c8c9b` | **H4 + H5a:** Mover `memset(rxBuf, 0, 1024)` para dentro de `if (p_source == SOURCE_I2S_IN)` e pré-cachear `bufferSizeF` / `maxReadDelayF` + fast-path de epoch em `readHistory` e `dropoutBaseCutoff` / `targetDelay` fora do loop | `src/main.cpp`, `dsp/core/tape_core.cpp` | **10/10 BIT-EXACT** (`max_abs=0.0`, `rmse=0.0`) | T2 `MeanUs`: `2.213,45 -> 2.186,24 µs` (`-27,21 µs`); `P99`: `2.355 -> 2.332 µs`. |
| `a189487` | **H6:** Posicionar `TapeCore::updateParams`, `updateCachedParams`, `updateFilters`, `updatePlaybackFilters`, `BiquadFilter::set*` e `TapeMagnetics::updateParams` em DIRAM (`HYDRA_DSP_IRAM`) para eliminar cache misses de Flash durante automação | `dsp/core/tape_core.cpp` | **10/10 BIT-EXACT** (`max_abs=0.0`, `rmse=0.0`) | `MaxParamUpd` sob stress de automação caiu de `462 µs` para `186 µs` (`-276 µs`); `Full Work Peak` em automação caiu de `2.886 µs` para `2.634 µs` (`dltrace`) e `2.560 µs` (produção). |

## 2. Otimização Testada e Revertida

- **H3b (`BiquadState` separado de coeficientes em `colorTapeFeedback`):**  
  Testou-se separar `z1, z2` de `b0..a2` em `BiquadState` para compartilhar os objetos `tapeRolloff`, `reproHeadBump` e `gapLossLPF` sem cópia de coeficientes. Embora **10/10 bit-exact**, aumentou a pressão de ponteiros no loop Xtensa: T2 `MeanUs` subiu `+17,1 µs` (`2.213,45 -> 2.230,55 µs`) e T4 `MeanUs` subiu `+16,7 µs` (`2.016,05 -> 2.032,73 µs`). **Revertida** conforme política da Fase 7.2E.
