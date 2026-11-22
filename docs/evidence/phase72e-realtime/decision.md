# Decisão Técnica — Hydra Tape Engine Fase 7.2E (Classic Tape Feedback C)

**Classificação Física de Tempo Real:** `PHYSICAL_RUNTIME_PASS`  
**Classificação de Preservação Sonora:** `SONIC_EQUIVALENCE_BIT_EXACT`

> **Restrição mantida:** PR #56 e a branch `fix/phase7-2e-realtime-optimization` permanecem **sem merge** e a Variante C continua **opt-in** até autorização explícita.

---

## 1. Identidade dos Binários e Ambiente

- **Branch:** `fix/phase7-2e-realtime-optimization` (derivada de `experiment/phase7-2-feedback-routing` @ `ab7f483115d2c9875cc6e9e0fe16de64f96ce655`)
- **Commit de produção testado no ESP32-S3:** `a1894876ccbad094b51648eba59209032c79936f`
- **Source SHA-256:** `209fd4a180e4f1e1fb1b41a8cf7adfad4689df7d969379ee9c8857487535cb8a`
- **ELF SHA-256 (`esp32-s3-classic-c`):** `85a07e402878cbdb4529c6a8920cb8522af8b5709543bbd390defe0bdc79faa3`
- **Firmware `.bin` SHA-256 (`esp32-s3-classic-c`):** `CF6694AF6E946A8D6F34DD87C0525EAD79562FE90440F65630B12E836EDB93D2`
- **Compilador ESP32-S3:** `xtensa-esp-elf-g++.exe (crosstool-NG esp-14.2.0_20251107) 14.2.0`
- **Hardware:** ESP32-S3 (`COM15`), `240 MHz`, `44.100 Hz`, bloco `128 frames`, `8 MB PSRAM`, orçamento `2.902,49 µs`, marcador `FeedbackRouting=PHASE72_CLASSIC_C`.

---

## 2. Comparação Física: C Original (7.2D) × C Otimizada (7.2E)

*(Todos os valores abaixo são medições físicas reais no ESP32-S3 com `esp32-s3-classic-c` não instrumentado; ver [comparison.csv](file:///c:/Users/devx/Documents/PlatformIO/Projects/hydra/docs/evidence/phase72e-realtime/comparison.csv)).*

| Cenário | C original P99 | C otimizada P99 | C original Full Peak | C otimizada Full Peak | Margem | Status |
|---|---:|---:|---:|---:|---:|---|
| **T1** Delay convencional | 2.100 µs | 2.081 µs | 2.722 µs | 2.384 µs | 518,49 µs | **PASS** |
| **T2** Multi-Head | 2.372 µs | 2.333 µs | 2.891 µs | 2.604 µs | 298,49 µs | **PASS** |
| **T3** Reverse 1/4 | 2.217 µs | 2.195 µs | 2.717 µs | 2.430 µs | 472,49 µs | **PASS** |
| **T4** Reverse feedback 95 | 2.178 µs | 2.159 µs | **2.918 µs** *(1 ovr)* | 2.464 µs | 438,49 µs | **PASS** |
| **T5** Fita envelhecida | 2.111 µs | 2.086 µs | 2.788 µs | 2.373 µs | 529,49 µs | **PASS** |
| **T6** Fita rápida e brilhante | 2.103 µs | 2.074 µs | 2.653 µs | 2.299 µs | 603,49 µs | **PASS** |
| **T7** Warp (controle negativo) | 1.826 µs | 1.821 µs | 2.520 µs | 2.075 µs | 827,49 µs | **PASS** |

- **Overruns (DSP e Full Work):** `0` em todos os cenários T1–T7, no teste de automação (`32` passos / `3,86 s`, `Full Work Peak = 2.560 µs`, margem `342,49 µs`) e nos dois ensaios sustentados de 5 minutos (`Reverse FB85`: `Full Work Peak = 2.556 µs`; `Multi-Head`: `Full Work Peak = 2.644 µs`).
- **Falhas I2S TX:** `0` em toda a campanha.
- **Memória / Stack:** Heap interno (`116.912–122.648 bytes`), PSRAM (`1.663.048 bytes`) e Stack (`27.244–27.268 bytes`) estáveis e sem vazamento.
- **Observação de P99 em T2:** `P99 = 2.333 µs` (`80,38%` do orçamento, `11 µs` acima da meta desejável de `80%`), porém com `Full Work Peak = 2.604 µs` (`298,49 µs` de margem, bem acima da meta de `200 µs`).

---

## 3. Preservação Numérica e Musical (C Original × C Otimizada)

Relatório JSON: [numerical-equivalence.json](file:///c:/Users/devx/Documents/PlatformIO/Projects/hydra/docs/evidence/phase72e-realtime/numerical-equivalence.json)

- Todos os **10 cenários da Fase 7.2** (incluindo *High feedback*, *Aged and dark repeats*, *Hiss after music stops*, *Ping-pong Multi-Head*, *Reverse blend transition*, *Warp negative control* e *Long feedback residual* de 60 s) produziram saída **bit-exact** (`max_abs = 0.0`, `rmse = 0.0`, `nonfinite = 0`) entre a C original (`ab7f483`) e a C otimizada (`a189487`).

---

## 4. Validações Automatizadas Executadas Localmente

- **Equivalência C-original × C-otimizada (10 cenários Fase 7.2):** `10/10 BIT-EXACT` ([numerical-equivalence.json](file:///c:/Users/devx/Documents/PlatformIO/Projects/hydra/docs/evidence/phase72e-realtime/numerical-equivalence.json))
- **CTest Native Core:** `12/12 PASS` (`fast_tanh_integrity`, `phase7_dsp_integrity`, `phase72_feedback_routing_contract`, `phase72_classic_core_smoke`, `core_smoke`, `core_regression`, `core_audible_controls`, `core_warp_test`, `core_reverse_test`, `core_transport_clock_test`, `core_midi_clock_test`, `web_param_ids`) + `core_abi_legacy_consumer` `PASS`.
- **WASM Core (`build-wasm`):** `3/3 PASS` (`test_golden_delay_modes_wasm`, `test_golden_reset_state_wasm`, `test_golden_param_automation_wasm`).
- **Native vs WASM Equivalence:** `PASS` (`max_abs = 0.000000000000e+00`, `rmse = 0.000000000000e+00`).
- **Bias Anti-Aliasing (`scripts/bias_antialiasing.py`):** `49 / 49` gates `PASS`.
- **Listening Lab self-tests (`scripts/test_listening_lab.py`):** `6/6 PASS`.
- **Compilações PlatformIO ESP32-S3:** `esp32-s3-classic-c` (`PASS`), `esp32-s3-classic-c-stageprofile` (`PASS`), `esp32-s3-devkitc-1` (`PASS`).
- *Nota de plataforma Windows:* scripts Python que passam arquivos temporários abertos em `Temp` para `hydra_listen_native.exe` / `hydra_golden_regression_native.exe` falham por lock de arquivo do Windows (os mesmos testes passam no WASM e nos executáveis C++ diretos).
