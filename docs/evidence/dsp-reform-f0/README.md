# Hydra DSP Reform — F0: Baseline Reproduzível e Preparação para F1

**Fase:** F0 (Auditoria de baseline reproduzível — somente leitura, testes e evidências)  
**Data:** 2026-11-23  
**Branch documental:** `docs/dsp-reform-f0-baseline`  
**HEAD de `main` analisada:** `6d94af5557f8ec23ef4627af3d0a584f7e5fd517`  
**Baseline histórica da auditoria:** `6849edf738c0d60731f51e96f7a16d1deac08cfe`  
**Veredito de prontidão:** **`READY_FOR_F1`** *(condicionado à execução de benchmark físico no ESP32-S3 antes de promover qualquer alteração de hot path na F1; medições físicas atuais registradas como `PENDING_PHYSICAL`)*.

---

## 1. Comparação HEAD vs Baseline Histórica (`6849edf`)

Entre o commit analisado na auditoria original (`6849edf738c0d60731f51e96f7a16d1deac08cfe`) e a HEAD atual de `main` (`6d94af5557f8ec23ef4627af3d0a584f7e5fd517`), ocorreram apenas **3 commits puramente documentais**:

- `1f58e4d` — `docs: record tape delay DSP and musical audit (2026-11-23)` (`docs/TAPE_DELAY_DSP_MUSICAL_AUDIT_2026-11-23.md`)
- `4caa334` — `docs: define agent-gated phased DSP reform implementation roadmap` (`docs/DSP_REFORM_IMPLEMENTATION_PHASES.md`)
- `6d94af5` — `docs: direct agents to mandatory DSP audit and phase gates` (`AGENTS.md`)

**Conclusão:** Zero linhas de código DSP, API C, presets, CMake ou PlatformIO foram modificadas após `6849edf`. Todas as conclusões técnicas e referências de código da auditoria (`docs/TAPE_DELAY_DSP_MUSICAL_AUDIT_2026-11-23.md`) permanecem válidas para a HEAD atual, tendo sido verificadas diretamente nos arquivos ativos nesta rodada.

> **Observação de histórico:** `6849edf` corresponde ao *Merge PR #56: Classic Tape Feedback C (Phase 7.2)*, imediatamente após `3b2bc34` (*Merge PR #57: fix/phase7-2e-realtime-optimization*). A Variante C já está no repositório em `main`, porém **estritamente opt-in** via macro de compilação (`HYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING=1`), nunca ativa no build padrão.

---

## 2. Inventário de Arquivos Examinados e Hashes de Integridade

### Arquivos DSP e Integração (SHA-256 na HEAD `6d94af5`)

| Arquivo | Papel / Status | SHA-256 |
|---|---|---|
| `dsp/core/tape_core.cpp` | **Núcleo canônico ativo** (Tape/Reverse/Warp/Spring) | `DE52B01F4305D958B769E12FC2F804D6493AF98C99B4614EBCAB2EE31B12E114` |
| `dsp/core/tape_core.hpp` | Interface C++ do `TapeCore` e `TapeParams` | *(lido integralmente)* |
| `dsp/core/fast_tanh_table.inc` | LUT canônica (1025 valores `float32`) | `FCC7A4D288F3207A2F09A2CCA3C708A713FABBE0289F9A49A5E5BCBC7FF8A7AD` |
| `dsp/core/hydra_dsp_c_api.cpp` | API C comum (Native / WASM / ESP32) | `E6726BB54D2F6CF984FC918FFFEB3A2809C30D4FD04ABA3BB3775EA6BFA713D9` |
| `dsp/core/feedback_routing_experiment.hpp` | Helpers opt-in das variantes B e C | `808EAF8529A68D626145C5AA7CCD9539E524B29F944E8C4475C8142647B33272` |
| `dsp/core/tape_integrity.hpp` | Guardas de biquad e `continuousFeedbackSoftKnee` | *(lido integralmente)* |
| `dsp/core/reverse_windows.hpp` | Reverse V2 (overlap, smoothstep, sync) | *(lido linhas 1–45)* |
| `include/hydra_dsp.h` | Contrato ABI C v1.7 (38 parâmetros) | `D74F370120F0DD4E09BCFDFE3CD65FAEBF10E71A42EC7122020DE331A7835179` |
| `include/hydra_mech_noise.hpp` | Gerador LCG e filtro de ruído mecânico L/R | *(lido integralmente)* |
| `host/native/esp32/hydra_dsp_esp32_adapter.cpp` | Adaptador RT-safe ESP32-S3 → API C | *(lido integralmente)* |
| `src/main.cpp` | Loop I2S / Audio Task / Pós-processamento | *(lido linhas 245–305, 575–620, 850–965)* |
| `lib/TapeDelay/library.json` | Manifesto de build PlatformIO (`Fripp`, `Bubbles`, `Freeverb`; exclui `TapeDelay.cpp`) | *(lido integralmente)* |

### Documentos, Build e Scripts Examinados

- `AGENTS.md`, `docs/TAPE_DELAY_DSP_MUSICAL_AUDIT_2026-11-23.md`, `docs/DSP_REFORM_IMPLEMENTATION_PHASES.md`
- `docs/ESP32_S3_PERFORMANCE_AUDIT.md`, `docs/PHASE7_2_FEEDBACK_AUDIT.md`, `docs/PHASE7_2_CLASSIC_ARCHITECTURE.md`, `docs/PHASE7_2E_DEADLINE_INVESTIGATION.md`, `docs/evidence/phase72e-realtime/decision.md`
- `docs/PHASE6_NUMERICAL_EQUIVALENCE.md`, `docs/PHASE6_FEEDBACK_STABILITY.md`
- `CMakeLists.txt`, `platformio.ini`, `.gitignore`
- `.github/workflows/core-equivalence.yml`, `.github/workflows/phase72-feedback.yml`
- `tests/core/compare_native_wasm.py`, `tests/core/tap_tempo_test.cpp`
- `scripts/phase64_feedback_stability.py`, `scripts/phase64_validate_engine.py`, `scripts/musical_sources.py`, `scripts/audio_analysis.py`, `tools/listening-lab/README.md`

---

## 3. Mapa da Arquitetura DSP Ativa em Produção

Cadeia ativa confirmada no código-fonte da HEAD `6d94af5`:

1. **Integração ESP32-S3 → API C → TapeCore:**
   - `src/main.cpp:258-297` (`makeTapeParams()` mapeia `p_mix` tanto para `params.dryWet` quanto para `params.delayWet`).
   - `src/main.cpp:860-920` (leitura I2S int32 ou `MelodyGen`, chamada a `tape->processStereoBlock(...)`).
   - `host/native/esp32/hydra_dsp_esp32_adapter.cpp:138-165` (`HydraDspEsp32Adapter::processStereoBlock` drena filas SPSC e chama `hydra_dsp_process`).
   - `dsp/core/hydra_dsp_c_api.cpp:330-337` (`hydra_dsp_commit` → `handle->core->updateParams(handle->params)`).
2. **Condicionamento e Gravação na Memória Circular Compartilhada:**
   - `dsp/core/tape_core.cpp:1110` (`inputHPF` → `inputLPF`: 20 Hz–20 kHz padrão, ou 150 Hz–5 kHz se `guitarFocus` em `486-496`).
   - `dsp/core/tape_core.cpp:1221-1223` (`freshInput = conditionedInput * driveGain`, soma com `feedbackInput`, `dcBlocker`, clamp `[-4, 4]`).
   - `dsp/core/tape_core.cpp:110-183, 1225-1229`: `TapeMagnetics::process(recSig, allowBias)` usa `FastTanhLUT` (`fast_tanh_table.inc`, 1025 entradas, interpolação linear, quadratura Gauss-Legendre de 2 pontos para anti-aliasing do bias) e estado de remanência `m`, escrevendo no buffer L/R em PSRAM (`allocateDelayBuffer` em `82-93`) quando `!impl_->freeze`.
3. **Três Cabeçotes de Leitura, Reverse V2 e Warp (Memória Compartilhada):**
   - Textura curta fixa em `200.f + mod` (`dsp/core/tape_core.cpp:1114`).
   - Cabeçotes convencionais 1/2/3 com pesos `1.0 / 0.75 / 0.55` normalizados por `invHeadGainSum` (`442-447, 1124-1139`).
   - Reverse V2 (`reverse_windows.hpp:19-39`, `tape_core.cpp:885-893, 1049-1097, 1143-1146`): `delaySig += reverseBlend * (reverseSig - delaySig)`. Reverse V1 persiste apenas para `reverseMode == 2` (`1049, 1120-1122`).
   - Accidental Warp (`tape_core.cpp:957-1045, 1118-1119`): lê os mesmos buffers L/R (modos 0–3, 1 ou 2 taps, Hermite ou int cast) e gera `warpSig` (saída direta) e `warpFb` (HPF 120 Hz → LPF 2200 Hz → `fastSatRational`).
4. **Wow, Flutter, Dropout, Azimute e Hiss:**
   - Modulação mecânica aperiódica (`include/hydra_mech_noise.hpp:25-60`, `tape_core.cpp:903-912`).
   - Dropout dinâmico (`OnePoleLP` + `ampGain`, `928-931, 1151-1156`).
   - Azimute (`AllpassFilter`, `914-926, 1157`).
   - Hiss estéreo com ducking por envelope de entrada (`932-950, 1158`).
5. **Filtros de Reprodução (Monitoração) e Roteamento de Feedback:**
   - `tapeSig = (textureSig * textureGain) + (delaySig * delayGain)` (`1149`), seguido de dropout, azimute e hiss (`1151-1158`).
   - **Variante A (padrão ativo, `1171-1175`):** `frame.feedbackSource = tapeSig + reverseBlend * (reverseSig - tapeSig);` — extraído **antes** da EQ final de reprodução (`tapeRolloff` → `reproHeadBump` → `gapLossLPF` em `1177`), porém **depois** dos ganhos de monitoração (`textureGain`, `delayGain`), dropout, azimute e hiss, e com um segundo blend de Reverse.
6. **Cadeia de Feedback, Saturadores e Compressor:**
   - `dsp/core/tape_core.cpp:1182-1216`: `feedbackHPF` (300 Hz, Q 0.5) → `feedbackDrive` → `feedbackHeadBump` (60–200 Hz, Q 0.8) → `feedbackGapLoss` (1-pole LPF) → `feedbackPhase` (allpass) → `feedbackPreEmph` (ganho escalar) → `fastSatRational(x*1.10)` → `*0.82` → `fastSatRational(x*1.08)` → `feedbackDeEmph` → `* feedbackGain` (`safeFeedback <= 0.88`, `422`) → `feedbackCompressor` (`continuousFeedbackSoftKnee`, `tape_integrity.hpp:27-46`) → `* delayEnableRamp`.
7. **Mixagem, Limitação Final e Auxiliares Internos/Externos:**
   - `writeCh` (`1232`): `outputLimiter((input * dryGain) + dryTapeOutput)`.
   - `reverseSmear` (4 allpass em série, `1273-1289`) e `spring` (6 estágios AP+LPF com `outputLimiter`, `1290-1300`) internos ao `TapeCore`.
   - Externos no firmware (`src/main.cpp:929-954`): `tanhf` externo pulado quando `appliedTapeCore == true`; `NoiseGate::processBlock`; `FreeverbEngine::processStereoBlock` opcional.

### Classificação dos Motores no Repositório

- **Produção ativa:** `TapeCore` (`dsp/core/tape_core.cpp`)
- **Experimentais (opt-in por macro):** Variantes B e C (`dsp/core/feedback_routing_experiment.hpp`)
- **Alternativos selecionáveis no host:** `FrippEngine` (`src/main.cpp:887-890`), `BubblesEngine` (`src/main.cpp:883-886`)
- **Pós-processamento externo ativo:** `FreeverbEngine` (`src/main.cpp:951-954`), `NoiseGate` (`src/main.cpp:942-944`)
- **Legado não compilado no firmware Tape:** `TapeModel` (`lib/TapeDelay/src/TapeDelay.cpp`, excluído por `lib/TapeDelay/library.json:4`)

---

## 4. Estado Real das Variantes de Feedback A / B / C

| Variante | Macro de Ativação | Compilada por padrão? | Comportamento no código (`tape_core.cpp`) | Status de Testes e Hardware |
|---|---|---|---|---|
| **A (Produção)** | *(nenhuma)* | **Sim** | `tapeSig + reverseBlend * (reverseSig - tapeSig)` (`:1174`) | Aprovada na Fase 7.1; todos os gates Native/WASM e validação física `c9bc17e` |
| **B (Experimental)** | `HYDRA_PHASE72_EXPERIMENTAL_FEEDBACK_ROUTING=1` | Não | `delaySig` puro sem ganhos de monitor, hiss, dropout ou EQ de reprodução (`:1170`) | Apenas build de escuta A/B (CI `phase72-feedback.yml`); **não** testada no ESP32-S3 |
| **C (Classic Opt-in)** | `HYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING=1` | Não | `delaySig` passa por cópia privada de dropout, azimute, shelf, bump, gap loss + `0.20 * hiss` (`:1165, 1239-1265`) | Selecionada por escuta na Fase 7.2C; aprovada fisicamente na Fase 7.2E (`a189487`, `esp32-s3-classic-c`) com **6 biquads, 2 one-poles e 2 allpasses extras**; **não** promovida a padrão |

- As macros B e C são mutuamente exclusivas por `#error` em `dsp/core/tape_core.cpp:17-20`.
- **Nota crítica:** Os dados físicos de `7.2E` referem-se à Variante C otimizada no commit `a189487` (`esp32-s3-classic-c`), não ao build padrão (Variante A) e nem a uma futura candidata F1.

---

## 5. Matriz de Testes Executados nesta Rodada (F0)

Ambiente local: Windows 11, CMake `4.1.1`, MinGW-W64 GCC `14.2.0` (`-O3 -DNDEBUG -std=c++17 -ffp-contract=off`), Emscripten `3.1.62`, Python `3.14.0`, Node `v24.11.1`.

| Categoria / Suite | Comando / Alvo | Resultado | Observações |
|---|---|---|---|
| **Build Native limpo** | `cmake -S . -B build-native -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON` + `cmake --build build-native -j` | **PASS** | `-ffp-contract=off` verificado em `build-native/compile_commands.json` |
| **CTest Native (geral)** | `ctest --test-dir build-native --output-on-failure` | **14/19 PASS, 5 FAIL** | Ver detalhamento abaixo |
| `fast_tanh_integrity` | CTest #1 | **PASS** | LUT 1025 entradas, hash e simetria íntegros |
| `phase7_dsp_integrity` | CTest #2 | **PASS** | Curva escalar e estabilidade de polos |
| `phase72_feedback_routing_contract` | CTest #3 | **PASS** | Contrato escalar B/C |
| `phase72_classic_core_smoke` | CTest #4 | **PASS** | Smoke build com macro C |
| `core_smoke` / `core_regression` / `core_audible_controls` | CTest #5, #6, #7 | **PASS** | Regressão e controles audíveis OK |
| `core_warp_test` / `core_reverse_test` | CTest #8, #9 | **PASS** | Accidental Warp e Reverse V2 OK |
| `core_transport_clock_test` / `core_midi_clock_test` | CTest #10, #12 | **PASS** | Transport e MIDI Clock OK |
| `core_tap_tempo_test` | CTest #11 | **FAIL (flaky em CTest) / PASS isolado** | Falhou sob carga do CTest em `tap_tempo_test.cpp:826` (`validSnapshots.load() > 0u`, corrida de agendamento entre `producer` e `consumer`); na reexecução direta: **20/20 PASS** (`6164` snapshots). **Registrado sem corrigir na F0.** |
| `web_param_ids` / `core_abi_symbols` / `core_abi_legacy_consumer` | CTest #13, #14, #15 | **PASS** | ABI v1.7 (38 IDs) e consumidor legado v1.2 íntegros |
| `core_transition_characterization` | CTest #16 (`listening_review.py`) | **FAIL (Windows lock)** | Erro de lock de arquivo temporário em `Temp` no Windows (mesma limitação conhecida em `decision.md:60`) |
| Goldens Native via CTest (`delay_modes`, `reset_state`, `param_automation`) | CTest #17, #18, #19 (`golden_test.py`) | **FAIL (Windows lock)** | `failed to open output path` em `Temp` no Windows |
| **Goldens Native (execução direta)** | `hydra_golden_regression_native.exe` + `golden_compare.py` | **3/3 PASS (BIT-EXACT)** | `max_abs=0.0`, `rmse=0.0`, `null_test_energy=0.0` nos 3 cenários |
| **Guards Python (`tests/core/`)** | `test_audio_task_invariants.py` (3/3), `test_diagnostic_setter_probe.py` (5/5) | **PASS** | 8/8 testes Python aprovados |
| **Build WASM limpo** | `emcmake cmake -S . -B build-wasm -DCMAKE_BUILD_TYPE=Release -DBUILD_WEB=ON -DBUILD_TESTING=ON` | **PASS** | `-ffp-contract=off` verificado em `build-wasm/compile_commands.json` |
| **CTest WASM (`-R "_wasm$"`)** | `ctest --test-dir build-wasm --output-on-failure -R "_wasm$"` | **3/3 PASS** | `delay_modes`, `reset_state`, `param_automation` em Node |
| **Equivalência Native ↔ WASM** | `verify_numerical_inputs.py` + `compare_native_wasm.py` | **PASS (BIT-EXACT)** | Input `4096/4096` (0 ULP), LUT `1025/1025` (0 ULP), 5 cenários `max_abs=0.0`, `rmse=0.0` |
| **Estabilidade de Feedback (60 s)** | `python scripts/phase64_feedback_stability.py` | **PASS** | `feedback95` & `multihead` @ 44.1/48 kHz: `limited sustain`, `clipped=0`, slope `+0.00001..+0.00005 dB/s`; `reverse`: `decayed to silence` |
| **Full-Engine Gates** | `python scripts/phase64_validate_engine.py` | **27/27 PASS** | Todos os gates a 44.1 kHz e 48 kHz aprovados |
| **Bias Anti-Aliasing** | `python scripts/bias_antialiasing.py` | **49/49 PASS** | Todos os gates de anti-aliasing aprovados |
| **Listening Lab (unit tests)** | `node --test tools/listening-lab/lab.test.mjs` | **7/7 PASS** | Transporte, RMS matching e proveniência OK |
| **Hardware físico ESP32-S3** | PlatformIO / placa física | **PENDING_PHYSICAL** | Sem CLI `pio` nem placa ESP32-S3 conectada neste ambiente |

---

## 6. Orçamento e Desempenho ESP32-S3 (Histórico vs Atual vs Pendente)

**Referência:** ESP32-S3 N16R8 @ 240 MHz, estéreo 44.100 Hz, blocos de 128 frames, deadline = **2.902,49 µs** (meta conservadora P99 < 2.032 µs = 70%).

### A. Resultados Históricos Documentados

1. **Produção padrão (Variante A / Warp, commit `c9bc17e`, `docs/ESP32_S3_PERFORMANCE_AUDIT.md:134-172`):**
   - `S04_TapeCore_Default_60s`: Mean `1.710,37 µs`, P50 `1.714 µs`, P95 `1.782 µs`, **P99 `1.790 µs` (61,67%)**, Peak `2.006 µs`, `0` overruns, `0` falhas I2S.
   - `S11_FullChain_Normal_5min` (103.360 blocos): Mean `1.772,08 µs`, P50 `1.776 µs`, P95 `1.849 µs`, **P99 `1.861 µs` (64,12%)**, Peak `2.108 µs`, `0` overruns, `0` falhas I2S, heap `128.092 B` estável, PSRAM `3.830.424 B` estável.
2. **Variante C otimizada (Fase 7.2E, commit `a189487`, `docs/evidence/phase72e-realtime/decision.md:26-39`):**
   - `T1` Delay convencional: P99 `2.081 µs`, Full Work Peak `2.384 µs` (margem `518,49 µs`), `0` overruns.
   - `T2` Multi-Head (pior caso): **P99 `2.333 µs` (80,38%)**, **Full Work Peak `2.604 µs`** (margem `298,49 µs`), `0` overruns.
   - `T4` Reverse FB95: P99 `2.159 µs`, Full Work Peak `2.464 µs`, `0` overruns.
   - Ensaios sustentados de 5 min (`Reverse FB85` / `Multi-Head`): `0` overruns, `0` falhas I2S, heap `116.912–122.648 B`, PSRAM `1.663.048 B`, stack `27.244–27.268 B`.

### B. Medições Físicas Executadas nesta Rodada (F0)

- **Nenhuma** medição em hardware físico foi executada nesta rodada (ambiente sem PlatformIO/ESP32-S3 acoplado).

### C. Medições Físicas Pendentes (`PENDING_PHYSICAL`)

- Captura de baseline física (`identity`, `perf`, `bench`, `stageprof`, `fullWorkUs`, heap/PSRAM/stack, I2S/overruns) na HEAD atual (`6d94af5`) para Variante A e comparação controlada na F1 antes de qualquer promoção.

---

## 7. Baseline Musical e Manifesto de Renderes

Foram renderizados 10 cenários determinísticos a **44.100 Hz** (seed `20261123`, `build-native/libhydra_abi_test_library.dll`, PCM24 estéreo) reutilizando `scripts/musical_sources.py`, `scripts/audio_analysis.py` e `scripts/render_phase6_review.py`. Os WAVs residem em `artifacts/dsp-reform-f0/baseline/` (ignorado pelo Git) e todos os metadados e hashes SHA-256 estão congelados em [`baseline_renders_manifest.json`](baseline_renders_manifest.json).

- **Cobertura completa:** guitarra limpa (`fb=45`), guitarra saturada (`fb=55`, `guitarFocus=1`), synth mono (`fb=65`), synth polifônico multi-head (`fb=60`, `activeHeads=7`), bateria ping-pong (`fb=55`, `pingPong=1`), percussão irregular (`fb=75`), impulso moderado (`fb=45`, 10 s), impulso alto (`fb=80`, 20 s), impulso próximo à auto-oscilação (`fb=92`, 60 s) e silêncio com hiss (`noise=30`, `fb=65`).
- **Integridade:** `10/10` finitos, `0` amostras no teto do limitador (`0.9899`), pico máximo observado `0.9359` (`02_driven_guitar_fb55`).

---

## 8. Diagnóstico de Entrada e Proposta Experimental para F1

### Respostas às 7 Perguntas de Diagnóstico

1. **A rota A realmente apresenta dependência entre mix e recirculação?**  
   **Sim, comprovado no código.** Em `dsp/core/tape_core.cpp:1149,1174`, `frame.feedbackSource` é derivado de `tapeSig = textureSig * textureGain + delaySig * delayGain`, onde `textureGain` (`dryWet * 0.01`) e `delayGain` (`delayWet * 0.01`) são controlados pelo mix (`src/main.cpp:274-276` mapeia `p_mix` para ambos). Além disso, `delaySig` já sofreu o blend de Reverse em `:1145` e `feedbackSource` aplica um segundo blend `tapeSig + reverseBlend * (reverseSig - tapeSig)` em `:1174`.
2. **Qual é o comportamento exato das variantes B e C?**  
   - **B** (`feedback_routing_experiment.hpp:7-9`, `tape_core.cpp:1170`): usa `delaySig` puro (já com 1 único blend Reverse), sem `textureGain`/`delayGain`, sem dropout, sem azimute, sem hiss e sem EQ de reprodução.
   - **C** (`feedback_routing_experiment.hpp:19-25`, `tape_core.cpp:1165,1239-1265`): parte de `delaySig`, passa por cópias privadas de dropout, azimute, `tapeRolloff`, `reproHeadBump`, `gapLossLPF` e soma `0.20 * hiss` antes da cadeia de feedback.
3. **É possível testar uma rota independente de menor custo sem duplicar processamento?**  
   **Sim, como hipótese experimental para F1.** Como `tapeRolloff` (high-shelf) e `gapLossLPF` (lowpass) na reprodução são lineares e o feedback já possui `feedbackGapLoss` (1-pole LPF) e `feedbackHeadBump` (peak), uma candidata enxuta pode desacoplar o mix (`delaySig` em vez de `tapeSig`) e reparametrizar/reaproveitar a filtragem já existente no loop de feedback (ou compartilhar apenas coeficientes sem duplicar 6 biquads/2 allpasses/2 one-poles extras), evitando o custo de ~470–550 µs/bloco da Variante C completa em T2.
4. **Quais filtros ou estados existentes podem ser reutilizados?**  
   Os estados já existentes na cadeia de feedback (`feedbackHPF`, `feedbackHeadBump`, `feedbackGapLoss`, `feedbackPhase`) e os coeficientes já calculados em `updatePlaybackFilters()`. **Regra inviolável:** nunca reutilizar o *estado* de um filtro de monitoração (`tapeRolloff`, `reproHeadBump`, `gapLossLPF`, `azimuthFilter`, `dropoutLPF`) avançando-o duas vezes por amostra.
5. **Quais cenários são mais importantes para comparação sonora?**  
   Variação de `delayWet`/`dryWet` com feedback fixo (cenários 03 e 04 do `package_phase72_feedback_ab.py`), silêncio pós-música com hiss (`noise=70`, `fb=65`), feedback alto (`fb=85..92`, cauda de 60 s), entrada/saída de Reverse, Multi-Head ping-pong e Warp como controle negativo bit-exact.
6. **Quais são os maiores riscos de regressão em CPU, PSRAM e Native/WASM?**  
   - **CPU ESP32-S3:** Variante C já consome P99 `2.333 µs` (80,38%) em T2; qualquer adição no hot path sem orçamento pode violar os `2.902,49 µs` sob automação.
   - **PSRAM:** Não adicionar leituras extras ou buffers longos; manter acessos por amostra idênticos.
   - **Native/WASM:** Qualquer `std::tanh`/`expf` ou contração FMA sem `-ffp-contract=off` quebraria a paridade bit-exact atual (`0.0 / 0.0`).
7. **Quais medições ainda faltam antes de implementar?**  
   Apenas a captura física da baseline atual (`6d94af5`) no ESP32-S3 (`PENDING_PHYSICAL`), caso se deseje comparar delta físico direto contra a mesma placa/ambiente na F1.

### Proposta Experimental para F1 (sem implementação nem escolha antecipada)

- **Hipótese F1:** Desacoplar `feedbackSource` dos ganhos de monitoração (`dryWet`/`delayWet`) e do duplo blend de Reverse, testando uma variante enxuta (**F1-Lean**) que preserva o caráter espectral/dinâmico aprovado de C usando a cadeia de feedback existente, comparada em A/B cego (RAW e MATCHED) contra A (produção), B (limpa) e C (clássica completa).
- **Critérios de aceitação para F1:**
  1. Independência estrutural comprovada (variação de `dryWet`/`delayWet` não altera energia/decay interno do loop).
  2. Warp permanece bit-exact (`max_abs = 0.0`) entre baseline e candidata.
  3. Native ↔ WASM `max_abs <= 1e-5` e `RMSE <= 1e-6` (idealmente `0.0`).
  4. Estabilidade 60 s (`phase64_feedback_stability.py` + `phase64_validate_engine.py` 27/27, zero teto `0.99`).
  5. Benchmark físico ESP32-S3 com `0` overruns, `0` falhas I2S e margem `fullWorkUs` superior à da Variante C (`> 298 µs` em T2).
  6. Aprovação humana explícita no Listening Lab (`tools/listening-lab/`).

---

## 9. Checklist de Prontidão para F1

- [x] `AGENTS.md`, auditoria e plano de fases lidos integralmente
- [x] HEAD `6d94af5` comparada com `6849edf` (somente 3 commits de docs)
- [x] Arquitetura ativa mapeada com `arquivo:linha`
- [x] Estado das variantes A/B/C documentado (A padrão; B/C opt-in)
- [x] Build limpo Native e WASM com `-ffp-contract=off` verificado
- [x] Equivalência Native ↔ WASM verificada (`0.0 / 0.0`, bit-exact)
- [x] Estabilidade 60 s e 27/27 gates completos verificados
- [x] Manifesto musical baseline gerado (`baseline_renders_manifest.json`)
- [x] Nenhum arquivo DSP alterado nesta fase
- [ ] Medição física da HEAD atual no ESP32-S3 (`PENDING_PHYSICAL` — obrigatória antes de promover F1)
