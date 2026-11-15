# Auditoria de Desempenho em Tempo Real — ESP32-S3 (PR #47)

**Repositório:** `ovelhaaa/hydra-tape-engine`  
**PR / Branch:** `#47` — Accidental Warp V1.3 (`codex/accidental-warp-v1`)  
**Baseline inicial analisado:** `f8d2851c663d62ae4850087dc5bb255bd8b818be`  
**Commits de diagnóstico e otimização:** `16bc1cc`, `f9e76d7`, `1575fe5`, `0298a4d`, `c9bc17e`  
**Alvo físico:** ESP32-S3 N16R8 (rev. v0.2), Xtensa LX7 dual-core @ 240 MHz, 16 MB Flash (QIO), 8 MB PSRAM (Octal/OPI)  
**Orçamento de áudio:** 44.100 Hz estéreo, blocos de 128 frames (`128 / 44100 × 1.000.000 = 2.902,49 µs`)  
**Meta de segurança (Fase E):** `0` overruns, `0` falhas I2S e `P99 < 2.032 µs` (< 70% do orçamento)

---

## Resumo Executivo

No baseline da PR #47, o firmware apresentava **100% de overruns** tanto com o modo Tape tradicional (`S04`, média `3.775,21 µs`) quanto com o Accidental Warp ativo (`S07`, média `3.613,53 µs`; cadeia completa `S11`, média `4.348,98 µs` e picos de `5.728 µs` — chegando a `8.957,12 µs/blk` sob profiling detalhado por estágio).

Após instrumentação em hardware (Fase A), diagnóstico por evidência (Fase B) e quatro commits incrementais sem reescrever o algoritmo do Accidental Warp nem alterar golden fixtures (Fases C e D), a execução em hardware no commit `c9bc17e` (`FW Nov 15 2026 20:18:28`) atingiu:

- **11/11 cenários (`S01..S11`, 1.024 blocos cada):** `0` overruns, `0` falhas I2S e `P99 <= 1.917 µs` (`<= 66,05%` do orçamento).
- **Validação sustentada (Fase E — ~9 minutos contínuos, 186.048 blocos medidos):** `0` overruns e `0` falhas I2S nos quatro ensaios de 60 s (`20.672` blocos cada) e no ensaio prolongado de 5 minutos em `S11_FullChain_Normal` (`103.360` blocos: média `1.772,08 µs` / `61,05%`, `P99 = 1.861 µs` / `64,12%`, pico máximo `2.108 µs`, heap interno `128.092 -> 128.092 (+0 B)` e PSRAM `3.830.424 -> 3.830.424 (+0 B)`).

---

## Fase A — Instrumentação e Baseline Medido

No commit `16bc1cc`, foram adicionados comandos seriais dedicados em [`src/main.cpp`](../src/main.cpp) (`bench`, `stageprof`, `membench` e `resetperf`) baseados em `esp_timer_get_time()` e no contador de ciclos de 240 MHz da CPU, medidos por bloco após aquecimento prévio (sem chamadas temporais por amostra).

### A.1 Varredura de Cenários no Baseline (`16bc1cc`, 1.024 blocos, 256 warmup)

| Cenário | Mean (µs) | P50 (µs) | P99 (µs) | Peak (µs) | Overruns | I2S Fail |
|---|---:|---:|---:|---:|---:|---:|
| `S01_GenOff_Silence` | 3.593,80 | — | 3.749 | 3.760 | 1.024 (100%) | 0 |
| `S02_GenOn_DspBypass` | 575,65 | — | 694 | — | 0 (0%) | 0 |
| `S03_TapeCore_Minimal` | 3.318,83 | — | 3.473 | — | 1.024 (100%) | 0 |
| `S04_TapeCore_Default` | 3.775,21 | — | 4.031 | 4.268 | 1.024 (100%) | 0 |
| `S05_TapeCore_HighFeedback` | 3.898,88 | — | 4.101 | 4.402 | 1.024 (100%) | 0 |
| `S06_Warp_2T_Hermite_Fbk0` | 3.697,04 | — | 3.984 | — | 1.024 (100%) | 0 |
| `S07_Warp_2T_Hermite_Fbk50` | 3.613,53 | — | 3.913 | — | 1.024 (100%) | 0 |
| `S08_Warp_2T_Hermite_Fbk95` | 3.727,48 | — | 3.930 | — | 1.024 (100%) | 0 |
| `S09_Warp_1T_RawInt_Fbk50` | 3.651,58 | — | 3.918 | — | 1.024 (100%) | 0 |
| `S10_TapeCore_Freeverb` | 4.372,03 | — | 4.619 | 4.861 | 1.024 (100%) | 0 |
| `S11_FullChain_Normal` | 4.348,98 | — | 5.421 | 5.728 | 1.024 (100%) | 0 |

Sob `stageprof` em `S11_FullChain_Normal` (que exercita todos os caminhos de código no mesmo loop), o thrashing da cache de instruções levou o total a **`8.957,12 µs/blk`**:

- `A2 TapeCore/DSP`: `5.643,11 µs/blk`
- `A3c Freeverb`: `1.445,83 µs/blk`
- `A3b NoiseGate`: `742,23 µs/blk`
- `A3a Ext tanhf()`: `573,90 µs/blk`
- `A1 Source/Gen/In`: `370,54 µs/blk`
- `A3d PCM Out`: `169,84 µs/blk`

### A.2 Sub-estágios Internos do `TapeCore` (Pass A2, Baseline)

| Sub-estágio do `TapeCore` | `S04_TapeCore_Default` (µs/blk) | `S11_FullChain_Normal` (µs/blk) |
|---|---:|---:|
| `Dropouts/Duck/His` | 1.039,81 | 1.485,74 |
| `MechMod/LFO/Az` | 929,09 | 934,24 |
| `WarpReader` | 16,93 | 1.551,30 |
| `RecAmp/Mag` | 457,44 | 599,44 |
| `PlaybackEQ` | 416,01 | 416,67 |
| `Feedback` | 504,39 | 104,25 |
| `HeadReads` | 404,88 | 227,76 |
| `OutMix` | 80,82 | 248,83 |
| `InCond` | 115,00 | 117,86 |
| `WriteBuf` | 56,51 | 57,09 |
| **Total interno medido** | **4.244,15** | **6.414,25** |

### A.3 Microbenchmark de PSRAM, SRAM e Funções Matemáticas (`membench`, 2.048 iters @ 240 MHz)

Medição registrada no firmware final (`c9bc17e`):

| Operação | Ciclos / chamada | Tempo por 128 chamadas (µs/128) |
|---|---:|---:|
| `SRAM  Hermite4 Seq` | 52,1 | 27,809 |
| `SRAM  Hermite4 Str` | 51,6 | 27,546 |
| `PSRAM Hermite4 Seq` | 52,0 | 27,725 |
| `PSRAM Hermite4 Str` | 51,5 | 27,481 |
| `PSRAM 192k HermSeq` | 65,7 | 35,028 |
| `PSRAM 192k HermStr` | 221,4 | 118,076 |
| `PSRAM 192k 4Tap+Wr` | 269,5 cyc/step | 143,736 |
| `tanhf()` | 372,7 | 198,766 |
| `expf()` | 261,2 | — |
| `powf()` | 239,1 | — |
| `fmodf()` | 203,2 | — |
| `sinf()` | 187,6 | — |
| `float div (__divsf3)` | 77,9 | — |
| `floorf()` | 71,3 | — |
| `int32 modulo (%)` | 16,2 | — |

---

## Fase B — Diagnóstico da Causa Raiz

1. **PSRAM não era o gargalo principal:** Mesmo no padrão mais agressivo de acesso não sequencial (`PSRAM 192k 4Tap+Wr`: 4 leituras Hermite espalhadas em 192k + 1 escrita), o custo total de barramento e memória é **`143,736 µs` por bloco de 128 frames** (`4,95%` do orçamento de `2.902,49 µs`). Portanto, mover a linha de delay para SRAM não era necessário nem justificável.
2. **Thrashing severo da I-Cache Flash de 16 KB (8 vias):** O framework padrão do PlatformIO/Arduino aplicava `-Os` e `-mfix-esp32-psram-cache-issue`, deixando `TapeCore`, `WarpReader`, `Freeverb` e utilitários matemáticos executando a partir da Flash com cache compartilhada de 16 KB. Quando o Warp e o Freeverb eram habilitados simultaneamente (`S10`/`S11`), as linhas da cache de instruções se expulsavam continuamente, criando saltos de ~`2,7 ms` para `4,3–8,9 ms`.
3. **Divisões `float` em software (`__divsf3`) e checagens de denormal por amostra no `TapeCore`:** Cada chamada a `__divsf3` em Flash/ROM consome ~`77,9` ciclos no ESP32-S3 (frente à aproximação por hardware `recip0.s`). O loop interno executava 9 divisões `float`, `floorf()` redundantes, divisão de ganho de cabeçotes, divisão pelo tamanho do grão do Warp, azimuth mesmo quando desligado e checagens de denormal `1e-20f` amostra por amostra sobre todas as variáveis de estado.
4. **Chamada externa redundante de `tanhf()` pós-`TapeCore` (Auditoria B4):** A integração chamava `tanhf()` em cada canal após `TapeCore::processStereo()`, custando `311–340 µs/blk` em `stageprof` (`573,90 µs/blk` no baseline sob contention de Flash I-Cache) — mesmo com `TapeCore` já aplicando saturação magnética e limitador interno (`outputLimiter` em `[-0,99, 0,99]`).
   - **Auditoria quantitativa em 26 WAVs de referência:** Em todos os 26 WAVs sem `tanhf()` externo, o pico máximo observado foi `0,92740` (`listening/high_feedback_harmonic/native.wav`), sem clipping. O `tanhf()` externo comprimia desnecessariamente o RMS em até `-1,146 dB` (`high_feedback_harmonic`) e introduzia distorção harmônica não solicitada em sinais limpos (em senoide a 440 Hz de amplitude `0,10..0,90`, a cadeia com apenas `outputLimiter` teve **`0,000%` de THD**, enquanto o `tanhf()` externo adicionou **`0,083%` a `5,637%` de THD**).
5. **Processamento amostra a amostra em `MelodyGen`, `NoiseGate` e `FreeverbEngine`:** Recálculo de ganho estéreo por amostra via divisão em `MelodyGen::nextStereo`, chamadas individuais de `Comb::process` / `Allpass::process` por amostra em `FreeverbEngine`, e leituras/escritas não promovidas a registradores no `NoiseGate`.

---

## Fases C & D — Otimizações Incrementais e Integração ESP32-S3

| Commit | Alterações principais | Arquivos-chave | Impacto em Hardware (`S11` / Observação) |
|---|---|---|---|
| `16bc1cc` | Telemetria de bloco, `stageprof`, `membench` | [`src/main.cpp`](../src/main.cpp) | Baseline medido: `S11` mean `4.348,98 µs` (`1.024` ovr) |
| `f9e76d7` | Pré-cálculo de `invHeadGainSum` e `invGrainSamples`, remoção de `floorf()` redundante e bypass de azimuth inativo | [`dsp/core/tape_core.cpp`](../dsp/core/tape_core.cpp) | `S03` caiu para `2.950,28 µs` (`539` ovr); `S11` em `5.104,68 µs` |
| `1575fe5` | `-unflags -Os` (e desabilitação de `-mfix-esp32-psram-cache-issue`), `-O2 -ffast-math -fno-finite-math-only`, `TapeCore` hot path em IRAM e inlining de kernels folha | [`platformio.ini`](../platformio.ini), [`include/hydra_mech_noise.hpp`](../include/hydra_mech_noise.hpp), [`dsp/core/tape_core.cpp`](../dsp/core/tape_core.cpp) | Eliminou thrashing da I-Cache; `S01..S09` atingiram `0` overruns; `S10` `850` ovr, `S11` `190` ovr (`2.753,36 µs`) |
| `0298a4d` | Refatoração do loop de áudio em blocos de 128 frames (`processStereoBlock`), remoção do `tanhf()` externo redundante pós-`TapeCore`, guarda de denormais no `MelodyGen` | [`src/main.cpp`](../src/main.cpp), [`lib/MelodyGen`](../lib/MelodyGen/src/MelodyGen.cpp) | **0/11 overruns** em `S01..S11`; `S11` mean `2.323,42 µs`, P99 `2.445 µs` (`84,24%`) |
| `c9bc17e` | `processStereo` delegando a `processStereoBlock`, `HYDRA_DSP_IRAM` em `hydra_dsp_process` e `TapeCore::Impl::flushDenormals()`, flushing de denormal `1e-20f` movido para fim de bloco, `fastRecip` Xtensa `recip0.s` (0 `__divsf3` em `TapeCore`), `panGainL`/`panGainR` pré-calculados em `MelodyGen::pickNextNote()`, `Comb::processBlockAccum` / `Allpass::processBlockInPlace` em chunks na stack em `FreeverbEngine`, `NoiseGate::processBlock` em registradores | [`dsp/core/tape_core.hpp`](../dsp/core/tape_core.hpp), [`dsp/core/tape_core.cpp`](../dsp/core/tape_core.cpp), [`dsp/core/hydra_dsp_c_api.cpp`](../dsp/core/hydra_dsp_c_api.cpp), [`lib/MelodyGen`](../lib/MelodyGen/include/MelodyGen.h), [`lib/TapeDelay`](../lib/TapeDelay/src/FreeverbEngine.cpp), [`src/main.cpp`](../src/main.cpp) | **Todos os 11 cenários abaixo de 70% do orçamento em P99** (`S11` mean `1.777,81 µs`, P99 `1.879 µs` / `64,74%`) |

### Nota sobre alocação em IRAM / Linker Xtensa

Métodos definidos inline dentro de uma `struct` anotada com `IRAM_ATTR` no Xtensa ESP32-S3 emitem seções COMDAT `.iram1.N[...]` sujeitas ao erro de linker `dangerous relocation: l32r: literal placed after use`. Ao mover o helper para declaração/definição out-of-line no `.cpp` (`HYDRA_DSP_IRAM void TapeCore::Impl::flushDenormals()`), o linker alocou todo o caminho crítico de áudio de forma limpa em IRAM.

Verificação de símbolos no ELF final (`c9bc17e`):
- DIRAM usada: `97.235 B` | DIRAM restante: `244.525 B`
- `hydra_dsp_process`: `0x40375640` (`0x5a` B, IRAM)
- `TapeCore::Impl::flushDenormals`: `0x40375a94` (`0x59c` B, IRAM)
- `TapeCore::processStereoBlock`: `0x40376030` (`0x2740` B, IRAM — `0` chamadas `__divsf3`, `9` instruções `recip0.s`)
- `FreeverbEngine::processStereoBlock`: `0x40378770` (`0x3a4` B, IRAM)
- `MelodyGen::nextStereo`: `0x4200e0d8` (`0x34c` B, IROM Flash)

> *Observação:* A instrumentação serial de benchmark (`bench`, `stageprof`, `membench`, `resetperf`) foi preservada no firmware atual de bancada em [`src/main.cpp`](../src/main.cpp) para permitir auditoria reproduzível via USB CDC.

---

## Comparativo Completo `S01..S11` (Baseline → Commit 4 `c9bc17e`)

Valores médios em `µs` por bloco de 128 frames (`Orçamento = 2.902,49 µs`):

| Cenário | Baseline Mean / Ovr | Commit 1 Mean / Ovr | Commit 2 Mean / Ovr | Commit 3 Mean / Ovr | Commit 4 Mean | Commit 4 P99 (% Bud) | Commit 4 Peak / Ovr |
|---|---:|---:|---:|---:|---:|---:|---:|
| `S01_GenOff_Silence` | 3.593,80 / 1024 | 3.285,38 / 1024 | 2.413,86 / 0 | 1.960,84 / 0 | **1.589,17** | **1.654** (56,99%) | 1.657 / **0** |
| `S02_GenOn_DspBypass` | 575,65 / 0 | 584,65 / 0 | 617,69 / 0 | 503,75 / 0 | **437,06** | **516** (17,78%) | 711 / **0** |
| `S03_TapeCore_Minimal` | 3.318,83 / 1024 | 2.950,28 / 539 | 2.159,76 / 0 | 1.706,63 / 0 | **1.461,31** | **1.500** (51,68%) | 1.727 / **0** |
| `S04_TapeCore_Default` | 3.775,21 / 1024 | 3.360,69 / 1024 | 2.468,06 / 0 | 1.987,27 / 0 | **1.722,40** | **1.793** (61,77%) | 2.026 / **0** |
| `S05_TapeCore_HighFeedback` | 3.898,88 / 1024 | 3.354,00 / 1024 | 2.536,53 / 0 | 2.047,58 / 0 | **1.677,45** | **1.732** (59,67%) | 1.921 / **0** |
| `S06_Warp_2T_Hermite_Fbk0` | 3.697,04 / 1024 | 3.339,79 / 1024 | 2.320,69 / 0 | 1.860,22 / 0 | **1.555,47** | **1.643** (56,61%) | 1.860 / **0** |
| `S07_Warp_2T_Hermite_Fbk50` | 3.613,53 / 1024 | 3.332,95 / 1024 | 2.413,55 / 0 | 1.930,77 / 0 | **1.562,60** | **1.651** (56,88%) | 1.831 / **0** |
| `S08_Warp_2T_Hermite_Fbk95` | 3.727,48 / 1024 | 3.369,15 / 1024 | 2.343,85 / 0 | 1.933,40 / 0 | **1.614,54** | **1.718** (59,19%) | 1.923 / **0** |
| `S09_Warp_1T_RawInt_Fbk50` | 3.651,58 / 1024 | 3.243,92 / 1024 | 2.250,11 / 0 | 1.763,83 / 0 | **1.529,03** | **1.604** (55,26%) | 1.787 / **0** |
| `S10_TapeCore_Freeverb` | 4.372,03 / 1024 | 3.928,96 / 1024 | 2.961,87 / 850 | 2.447,65 / 0 | **1.874,37** | **1.917** (66,05%) | 2.131 / **0** |
| `S11_FullChain_Normal` | 4.348,98 / 1024 | 5.104,68 / 1024 | 2.753,36 / 190 | 2.323,42 / 0 | **1.777,81** | **1.879** (64,74%) | 2.040 / **0** |

Perfil por estágio no cenário `S11_FullChain_Normal` no Commit 4 (`c9bc17e`, 512 blocos):

- `A1 Source/Gen/In`: `102,11 µs/blk` (`3,5%` do orçamento | `24.505 cyc/blk`)
- `A2 TapeCore/DSP`: `1.465,75 µs/blk` (`50,5%` do orçamento | `351.779 cyc/blk`)
- `A3a Ext tanhf()`: `0,04 µs/blk` (removido / apenas guarda de loop, `9 cyc/blk`)
- `A3b NoiseGate`: `24,33 µs/blk` (`0,8%` do orçamento | `5.838 cyc/blk`)
- `A3c Freeverb`: `188,14 µs/blk` (`6,5%` do orçamento | `45.154 cyc/blk`)
- `A3d PCM Out`: `7,15 µs/blk` (`0,2%` do orçamento | `1.715 cyc/blk`)
- **Total por bloco (`stageprof`)**: **`1.787,52 µs/blk`** (`61,6%` do orçamento | `429.005 cyc/blk`)

---

## Fase E — Validação Física Sustentada (`COM8`, Commit `c9bc17e`)

Ensaios executados continuamente em hardware físico (`FW Nov 15 2026 20:18:28`), com aquecimento de `256` blocos antes de cada janela:

| Ensaio | Blocos | Duração | Mean (µs) | P50 (µs) | P95 (µs) | P99 (µs) | Bud P99 | Peak (µs) | Ovr / I2S Fail | Heap Início→Fim / Min | PSRAM Início→Fim |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `S04_TapeCore_Default_60s` | 20.672 | 59,998 s | 1.710,37 | 1.714 | 1.782 | **1.790** | **61,67%** | 2.006 | **0 / 0** | `128140 -> 128140` (`+0`, min `128140`) | `3830424 -> 3830424` (`+0`) |
| `S07_Warp_2T_Hermite_Fbk50_60s` | 20.672 | 59,998 s | 1.580,45 | 1.586 | 1.660 | **1.673** | **57,64%** | 1.875 | **0 / 0** | `128140 -> 128140` (`+0`, min `128140`) | `3830424 -> 3830424` (`+0`) |
| `S08_Warp_2T_Hermite_Fbk95_60s` | 20.672 | 59,998 s | 1.574,84 | 1.582 | 1.659 | **1.671** | **57,57%** | 1.890 | **0 / 0** | `128140 -> 128140` (`+0`, min `128140`) | `3830424 -> 3830424` (`+0`) |
| `S11_FullChain_Normal_60s` | 20.672 | 60,000 s | 1.768,39 | 1.775 | 1.848 | **1.863** | **64,19%** | 2.097 | **0 / 0** | `128140 -> 128140` (`+0`, min `128140`) | `3830424 -> 3830424` (`+0`) |
| `S11_FullChain_Normal_5min` | 103.360 | 299,998 s | 1.772,08 | 1.776 | 1.849 | **1.861** | **64,12%** | 2.108 | **0 / 0** | `128092 -> 128092` (`+0`, min `128092`) | `3830424 -> 3830424` (`+0`) |

Todos os critérios mínimos e de margem de segurança (`P99 < 2.032 µs` / `< 70%`, `Peak < 2.902,49 µs`, zero overruns, zero erros I2S, zero vazamentos de heap/PSRAM e zero watchdog resets) foram integralmente cumpridos.

---

## Preservação Sonora e Regressão Offline

- **CTest Nativo (`Release`):** `11/11` testes aprovados (incluindo `core_warp_test`, `core_abi_legacy_consumer` e os três testes golden).
- **CTest WASM (`-R "_wasm$"`):** `3/3` testes golden aprovados.
- **Comparação A/B contra baseline `f8d2851` (commit `c9bc17e`):**
  - **Accidental Warp (`warp-ab/`, 12 cenários):** Correlação **`1,00000000`** em todos os cenários; RMSE `0,000e+00` em 8/12 cenários (`01_octave_up`, `02_fifth_up`, `03_dual_tap_hermite`, `06_dual_tap_raw`, `07_stereo`, `09_grain`, `10_speed`, `08_feedback/01`), `5,778e-06` em `04_single_tap_hermite`, `6,495e-06` em `05_single_tap_raw`, `3,324e-08..1,025e-07` em `08_feedback/02..04`, `8,214e-06` em `11_toggle` e `9,369e-07` em `12_cascade`.
  - **Modo Tape Tradicional (`listening/`):** `WORST_RMSE = 1,029e-04` (`ping_pong/native.wav`, correlação `0,99999983`, `specErr = 8,619e-06`, null `-64,8 dB`); `drive_ramp_excerpt/native.wav` com `rmse = 7,681e-05`, `max = 2,914e-02`, `corr = 0,99999986`. Nos demais cenários tradicionais (`delay_step`, `freeze`, `high_feedback_harmonic`, `multi_head_transients`, `reverse_stereo`, `single_head_impulse`, `wow_flutter_excerpt`), a correlação manteve-se `>= 0,99999925`.
- **Equivalência Native ↔ WASM (`tests/core/compare_native_wasm.py`):**
  - Baseline `main` (`e4cae43`): `max_abs = 7,436e-6 <= 1e-5` (PASS) e `RMSE = 2,49805e-6 > 1e-6` (FAIL pré-existente).
  - HEAD pós-otimização (`c9bc17e`): `max_abs = 8,479e-6 <= 1e-5` (PASS) e `RMSE = 2,74890e-6 > 1e-6` (FAIL pré-existente nos mesmos limites). Os thresholds originais foram mantidos inalterados.
