# Fase 7.2E.1 — Investigação de Deadline (Classic Tape Feedback C)

**Data:** 2026-11-22  
**Alvo:** `fix/phase7-2e-realtime-optimization` (derivada de `experiment/phase7-2-feedback-routing` @ `ab7f483115d2c9875cc6e9e0fe16de64f96ce655`)  
**Hardware:** ESP32-S3 (QFN56 rev v0.2), 240 MHz, 16 MB Flash (QIO), 8 MB PSRAM (OPI), `COM15`, Core 1 (`audioTask`, prioridade 5), `44.100 Hz`, bloco `128 frames`, orçamento = `2.902,49 µs`.

---

## 1. Auditoria dos Cronômetros Existentes (`src/main.cpp`)

| Medição | Início | Fim | O que inclui | O que NÃO inclui |
|---|---|---|---|---|
| **DSP (`durationUs` / `g_dsp_perf` / `g_bench`)** | `block_start_us` (`src/main.cpp:790`) | `esp_timer_get_time()` (`src/main.cpp:943`) | A1 (geração/leitura de entrada), A2 (`TapeCore::processStereoBlock`), A3a (`tanhf` se não TapeCore), A3b (`NoiseGate`), A3c (`Freeverb`), A3d (`AudioOutputTapeInterceptor::writeStereoBlock` PCM32) | `updateParams()`, `memset(rxBuf)`, `i2s_read`, atualização do histograma `g_bench`, `heap_caps_get_free_size()`, `AudioHealth`, `i2s_write()` |
| **Param Update (`updUs` / `g_bench.maxParamUpdateUs`)** | `updStartUs` (`src/main.cpp:618`) | `esp_timer_get_time()` (`src/main.cpp:630`) | `tape->updateParams(localParams)`, `melody->setBPM/Mood/Rhythm`, `gate->setThreshold` | Cópia sob `paramMutex`; só executa quando `dspParamsDirty == true` |
| **Full Work (`fullWorkUs` / `g_dsp_perf.fullWorkPeakUs`)** | `iterationStartUs` (`src/main.cpp:608`) | `esp_timer_get_time()` (`src/main.cpp:1008`) | **Tudo acima** + `dspParamsDirty` (`updateParams`) + `memset(rxBuf, 0, 1024)` + `i2s_read` (se `SOURCE_I2S_IN`) + seção crítica `g_dsp_perf` + transição/histograma `g_bench` (inclui `memset` PSRAM e `heap_caps_get_free_size` a cada 64 blocos) + scan `AudioHealth` (se pendente) | `i2s_write()` (chamado na linha 1013, após `fullWorkUs`) |

### Bug de Telemetria Identificado na Auditoria

1. **`perf reset` não zera `g_bench.maxParamUpdateUs`** (`src/main.cpp:1822-1826`): apenas `resetperf` (`src/main.cpp:1624`) zera `maxParamUpdateUs`. Por isso, os logs de T1–T7 da Fase 7.2D exibiam `MaxParamUpdUs=426` (herdado da configuração inicial) e `516` (herdado de T6).
2. **`perf reset` dispara `dspParamsDirty = true`**: o comando `perf` está dentro do bloco `if (xSemaphoreTake(paramMutex, ...))` que cai no epílogo `globalParams = makeTapeParams(); dspParamsDirty = true;` (`src/main.cpp:2028-2029`).

---

## 2. Instrumentação Diagnóstica (`esp32-s3-classic-c-dltrace`)

Foi adicionado um ambiente opt-in `[env:esp32-s3-classic-c-dltrace]` (`-DHYDRA_PHASE72E_DEADLINE_TRACE=1`) com buffer circular estático em RAM interna (`64` eventos de `24` bytes, zero alocação dinâmica, zero `Serial.print` ou acesso a filesystem na task de áudio).

### Custo Medido da Instrumentação

Comparação física `bench 10000 256` entre `esp32-s3-classic-c` (Fase 7.2D) e `esp32-s3-classic-c-dltrace`:

| Cenário | Produção Mean / P99 / Peak (µs) | `dltrace` Mean / P99 / Peak (µs) | Sobrecarga |
|---|---:|---:|---|
| **T2 Multi-Head** | 2.229,31 / 2.372 / 2.666 | 2.227,94 / 2.380 / 2.640 | Desprezível (dentro da variância entre execuções) |
| **T4 Reverse FB95** | 2.013,41 / 2.178 / 2.471 | 2.029,76 / 2.226 / 2.432 | < 17 µs/bloco (~0,6% do orçamento) |

---

## 3. Evidências Coletadas na Placa Física (`COM15`)

### T2 Multi-Head (`dltrace`)
- `TotalBlocks=10546`, `MaxFullWork=2865 us (seq=325)`, `MaxDsp=2640 us (seq=2071)`
- `MaxPre=57 us (seq=10422)`, `MaxParamUpd=41 us`
- `MaxPost=650 us (seq=325)`, `MaxPostHeap=18 us`, `MaxPostNoHeap=650 us`
- Evento de pico:
  `seq=325, preUs=3, paramUpdUs=0, dspUs=2212, tapeUs=2032, postUs=650, fullWorkUs=2865, flags=0x24` (`benchRun=1, heapChk=0, paramUpd=0`)
- Demais blocos de pico de DSP:
  `seq=2071, preUs=3, paramUpdUs=0, dspUs=2640, tapeUs=2193, postUs=6, fullWorkUs=2649, flags=0x24`

### T4 Reverse FB95 (`dltrace`)
- `TotalBlocks=10543`, `MaxFullWork=2727 us (seq=326)`, `MaxDsp=2432 us (seq=5074)`
- `MaxPre=68 us (seq=10420)`, `MaxParamUpd=47 us`
- `MaxPost=759 us (seq=326)`, `MaxPostHeap=15 us`, `MaxPostNoHeap=759 us`
- Evento de pico:
  `seq=326, preUs=3, paramUpdUs=0, dspUs=1965, tapeUs=1835, postUs=759, fullWorkUs=2727, flags=0x0C` (`benchRun=1, heapChk=0, paramUpd=0`)

---

## 4. Causa Raiz Confirmada e Hipóteses Testadas

### Origem do Pico `Full Work` (650–759 µs em `postUs`)

Em ambos os cenários, **apenas um único bloco** (`seq=325` em T2, `seq=326` em T4) apresentou `postUs` de **650–759 µs**, enquanto todos os demais `10.540+` blocos tiveram `postUs` entre **4 e 18 µs**.

`seq=325/326` corresponde exatamente ao instante `~0,94 s` após `dlreset` (`~69` blocos de espera + `256` blocos de `BENCH_WARMUP`), ou seja, **o bloco de transição `BENCH_WARMUP -> BENCH_RUNNING`** (`src/main.cpp:957-958`):

```cpp
if (g_bench.warmupRemain == 0 && g_bench.hist) {
  memset(g_bench.hist, 0, kBenchHistBins * sizeof(uint32_t));
```

`g_bench.hist` tem **32.768 bytes (32 KB) alocados em PSRAM**. Executar `memset` de 32 KB em PSRAM síncrono dentro da `audioTask` consome **650–759 µs** em um único bloco. Na Fase 7.2D (T4), esse `memset` coincidiu com um bloco de DSP mais alto (~2.159 µs), totalizando **2.918 µs > 2.902,49 µs** (`Full Work Overruns = 1`). Isso também explica por que o ensaio sustentado de 5 minutos (sem `bench`) teve `Full Work Peak = 2.525 µs` (apenas `+10 µs` sobre `Peak DSP = 2.515 µs`).

### Hipóteses Avaliadas

| Hipótese | Veredito | Evidência |
|---|---|---|
| `memset(g_bench.hist, 0, 32768)` em PSRAM na `audioTask` | **CONFIRMADA** | `postUs = 650 µs` (T2 `seq=325`) e `759 µs` (T4 `seq=326`) exatamente na transição `BENCH_WARMUP -> BENCH_RUNNING` |
| `heap_caps_get_free_size` a cada 64 blocos em `BENCH_RUNNING` | **REJEITADA como causa do overrun** | `MaxPostHeap = 18 µs` (T2) e `15 µs` (T4) |
| `updateParams()` disparado durante `bench` estático | **REJEITADA para T4 estático, CONFIRMADA como risco sob automação** | `paramUpdUs = 0` no bloco de pico em T2/T4 estáticos; porém quando parâmetros mudam, `MaxParamUpdUs` chega a `426–516 µs` |
| Bloqueio de `i2s_write()` | **REJEITADA** | `fullWorkUs` é medido antes de `i2s_write()` (`src/main.cpp:1008-1013`) |
| Custo adicional de `colorTapeFeedback` + Multi-Head | **CONFIRMADA como gargalo de margem DSP** | T2 atinge `Peak DSP = 2.640–2.666 µs` e `P99 = 2.372–2.380 µs` (~82% do orçamento) |

---

## 5. Classificação dos Gargalos (Alvos para 7.2E.2)

1. **Gargalo P0 (Infraestrutura de Benchmark em `src/main.cpp`):** `memset(g_bench.hist, 0, 32768)` em PSRAM dentro da `audioTask` (+650–759 µs). Pode ser zerado na task CLI antes de armar `BENCH_WARMUP`.
2. **Gargalo P1 (Controle / Automação em `TapeCore::Impl::updateFilters` e `updatePlaybackFilters`):** recálculo redundante de 12+ biquads (incluindo L/R idênticos, cópias Classic C idênticas, `feedbackHPF` fixo em 300 Hz e 12 `springLPF` mesmo com `spring == false`), gerando picos de **375–516 µs** incompatíveis com automação em T2/T4.
3. **Gargalo P1 (DSP Hot-Path em `colorTapeFeedback` e cadeia Classic C):** `P99` de T2 em `81,7%` e `Peak DSP` em `2.640–2.666 µs` deixam pouca margem sob carga concorrente ou automação.
