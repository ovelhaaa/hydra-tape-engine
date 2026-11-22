# Relatório de Validação Física — Hydra Tape Engine Fase 7.2D (Variante C)

**Classificação Geral:** `PHYSICAL_RUNTIME_FAIL`  
**Data da Campanha:** 2026-11-22  
**Alvo:** PR #56 (`experiment/phase7-2-feedback-routing`, commit `ad08ca6705b5cf0eb301ec22b1ff08df3d1edc22`) vs Baseline A (`main`, commit `24977b67e1ab8441080a29d9e5d40114c966ac50`)  
**Status da PR #56:** Mantida aberta, **sem merge**.

---

## 1. Identidade e Integridade do Firmware

Todos os checks remotos da PR #56 no GitHub estavam `completed / success` (exceto `deploy` = `skipped`) no commit `ad08ca6705b5cf0eb301ec22b1ff08df3d1edc22` antes da campanha física.

A compilação PlatformIO utilizou `scripts/build_esp32_dsp.py`, incorporando diretamente `dsp/core/tape_core.cpp`, `dsp/core/hydra_dsp_c_api.cpp` e `host/native/esp32/hydra_dsp_esp32_adapter.cpp` sem mocks.

| Versão | Ambiente PlatformIO | Commit | Source SHA-256 | Firmware `.bin` SHA-256 | Marcador `identity` |
|---|---|---|---|---|---|
| **Variante C (Produção)** | `esp32-s3-classic-c` | `ad08ca6705b5cf0eb301ec22b1ff08df3d1edc22` | `7714d8a2b185832adfbdb3d65e657b062465bbb19a9dc380a24cedfd6cf80329` | `486D6C318882FC91C55E583858280CB81A9046AB7994330C27CB7561F6950410` | `FeedbackRouting=PHASE72_CLASSIC_C` |
| **Variante C (Profiler)** | `esp32-s3-classic-c-stageprofile` | `ad08ca6705b5cf0eb301ec22b1ff08df3d1edc22` | `7714d8a2b185832adfbdb3d65e657b062465bbb19a9dc380a24cedfd6cf80329` | — (ELF `d401a5c1...`) | `FeedbackRouting=PHASE72_CLASSIC_C` |
| **Baseline A (Produção)** | `esp32-s3-devkitc-1` | `24977b67e1ab8441080a29d9e5d40114c966ac50` | `7f9d6f5759aedf7e1f2629921d4724498190c6b2123e962197d9ec6e4279a94a` | `DB3EC04BEA23142585951443F24B76E4DB4259D650E502FE4B82AC26BA07A6F9` | Sem marcador experimental |

Antes de qualquer gravação, a tabela de partições foi lida (`nvs`, `otadata`, `app0`, `spiffs`) e uma imagem de recuperação não destrutiva da partição `app0` (`0x10000`) foi preservada localmente fora do repositório. Todas as gravações foram restritas ao offset `0x10000` (`app0`), preservando NVS e LittleFS.

---

## 2. Condições Efetivas do Teste Físico

Hardware identificado via `esptool v5.1.0` e comando serial `identity`:

- **Porta serial:** `COM15` (`USB\VID_303A&PID_1001`, USB-Serial/JTAG)
- **Chip:** `ESP32-S3 (QFN56) (revision v0.2)`, MAC `b8:f8:62:e3:e4:a4`
- **CPU:** `240 MHz`
- **Flash:** `16 MB` (QIO)
- **PSRAM:** `8 MB` OPI (`8388608 bytes`)
- **Áudio:** `44.100 Hz`, bloco de `128 frames`, I2S inicializado (`I2SInitialized=1`), orçamento de tempo real = **2.902,49 µs**
- **Fonte de sinal:** Gerador interno (`src 1`, `gen 1`, `vol 10`), sem dependência de MP3/LittleFS ou ADC externo. `audiohealth` valida o caminho PCM digital e transmissão I2S (`TxError=0`, `TxBytes=1024`), não captura analógica no DAC.

---

## 3. Matriz Completa dos Benchmarks (`bench 10000 256` + `perf` + `audiohealth`)

### Variante C (`esp32-s3-classic-c`)

| Cenário | Mean (µs) | P50 (µs) | P95 (µs) | P99 (µs) | Peak DSP (µs) | Full Work Peak (µs) | DSP Ovr | FW Ovr | I2S Fail | Status |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| **T1** Delay convencional | 1943,26 | 1908 | 2088 | 2100 | 2367 | 2722 | 0 | 0 | 0 | **PASS** |
| **T2** Multi-Head (`hds 7`, `ppg 1`) | 2229,31 | 2215 | 2356 | 2372 | 2666 | 2891 | 0 | 0 | 0 | **PASS** *(margem 11,49 µs)* |
| **T3** Reverse 1/4 (`fbk 70`) | 2021,95 | 2010 | 2159 | 2217 | 2489 | 2717 | 0 | 0 | 0 | **PASS** |
| **T4** Reverse feedback 95 (`fbk 95`) | 2013,41 | 2007 | 2154 | 2178 | 2471 | **2918** | 0 | **1** | 0 | **FAIL** *(excedeu 2902,49 µs)* |
| **T5** Fita envelhecida (`tpa 90`, `dps 35`) | 2012,96 | 2033 | 2097 | 2111 | 2396 | 2788 | 0 | 0 | 0 | **PASS** |
| **T6** Fita rápida/brilhante | 1942,78 | 1941 | 2090 | 2103 | 2390 | 2653 | 0 | 0 | 0 | **PASS** |
| **T7** Warp (controle negativo) | 1721,20 | 1730 | 1796 | 1826 | 2074 | 2520 | 0 | 0 | 0 | **PASS** |

### Baseline A (`esp32-s3-devkitc-1`)

| Cenário | Mean (µs) | P50 (µs) | P95 (µs) | P99 (µs) | Peak DSP (µs) | Full Work Peak (µs) | DSP Ovr | FW Ovr | I2S Fail |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| **T1** Delay convencional | 1865,93 | 1871 | 1949 | 1965 | 2244 | 2595 | 0 | 0 | 0 |
| **T2** Multi-Head (`hds 7`, `ppg 1`) | 2149,46 | 2153 | 2228 | 2246 | 2492 | 2760 | 0 | 0 | 0 |
| **T3** Reverse 1/4 (`fbk 70`) | 1929,39 | 1918 | 2015 | 2091 | 2302 | 2660 | 0 | 0 | 0 |
| **T4** Reverse feedback 95 (`fbk 95`) | 1934,86 | 1938 | 2022 | 2093 | 2335 | 2648 | 0 | 0 | 0 |
| **T5** Fita envelhecida (`tpa 90`, `dps 35`) | 1879,87 | 1884 | 1952 | 1967 | 2228 | 2651 | 0 | 0 | 0 |
| **T6** Fita rápida/brilhante | 1864,92 | 1871 | 1947 | 1964 | 2213 | 2590 | 0 | 0 | 0 |
| **T7** Warp (controle negativo) | 1731,92 | 1738 | 1814 | 1831 | 2090 | 2500 | 0 | 0 | 0 |

---

## 4. Comparação de Desempenho A × C

| Cenário | A Mean | C Mean | Δ Mean | A P99 | C P99 | Δ P99 | A Peak | C Peak | A FW Peak | C FW Peak | Margem FW C | Status |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| **T1** | 1865,93 | 1943,26 | +77,33 (+4,14%) | 1965 | 2100 | +135 (+6,87%) | 2244 | 2367 | 2595 | 2722 | 180,49 µs | PASS |
| **T2** | 2149,46 | 2229,31 | +79,85 (+3,72%) | 2246 | 2372 | +126 (+5,61%) | 2492 | 2666 | 2760 | 2891 | **11,49 µs** | PASS |
| **T3** | 1929,39 | 2021,95 | +92,56 (+4,80%) | 2091 | 2217 | +126 (+6,03%) | 2302 | 2489 | 2660 | 2717 | 185,49 µs | PASS |
| **T4** | 1934,86 | 2013,41 | +78,55 (+4,06%) | 2093 | 2178 | +85 (+4,06%) | 2335 | 2471 | 2648 | **2918** | **-15,51 µs** | **FAIL** |
| **T5** | 1879,87 | 2012,96 | +133,09 (+7,08%) | 1967 | 2111 | +144 (+7,32%) | 2228 | 2396 | 2651 | 2788 | 114,49 µs | PASS |
| **T6** | 1864,92 | 1942,78 | +77,86 (+4,17%) | 1964 | 2103 | +139 (+7,08%) | 2213 | 2390 | 2590 | 2653 | 249,49 µs | PASS |
| **T7** | 1731,92 | 1721,20 | -10,72 (-0,62%) | 1831 | 1826 | -5 (-0,27%) | 2090 | 2074 | 2500 | 2520 | 382,49 µs | PASS |

### Análise de Custo da Cadeia C e Interação com Multi-Head / Reverse

1. **Custo específico de C:** Em condições normais (T1, T2, T3, T4, T6), a cadeia `colorTapeFeedback` adiciona **~77 a 93 µs/bloco** na média e **+85 a +144 µs** no P99. Em **T5** (`dps 35`, `tpa 90`), o custo sobe para **+133,09 µs/bloco** na média porque o branch de dropout (`if (drop.hfLoss < 0.9995f || drop.ampGain < 0.9995f)`) executa `dropoutLP.setCutoffFastInv(...)` e `dropoutLP.process(...)` em mais amostras.
2. **Controle negativo Warp (T7):** ΔMean de `-10,72 µs` (-0,62%) e ΔP99 de `-5 µs` (-0,27%) confirmam que Warp permanece inalterado.
3. **Combinação crítica com Multi-Head (T2):** Multi-Head (`hds 7`, `ppg 1`) já é o cenário mais pesado na Baseline A (`Mean=2149,46 µs`, `P99=2246 µs`). Com o acréscimo de C, **T2** atinge `P99=2372 µs` (**81,72%** do orçamento, acima da meta de 80%) e `Full Work Peak=2891 µs`, deixando apenas **11,49 µs (0,40%)** de folga até o deadline.
4. **Diagnóstico instrumentado (`esp32-s3-classic-c-stageprofile`):** Em `stageprof`, a diferença entre `A2 TapeCore/DSP` e `TOTAL STAGES` (onde `colorTapeFeedback` não possui macro de profiling própria) confirma o custo extra fora dos estágios originais: **126,01 µs/bloco** em T1 e **184,81 µs/bloco** em T4, contra **55,87 µs/bloco** de overhead basal em T7 (Warp).

---

## 5. Memória, Stack, I2S e Estabilidade

- **Heap interno:** Estável em `124.496 bytes` em todos os cenários T1–T7 de C (`HeapStart == HeapEnd == HeapMin`), sem vazamento (Baseline A: `125.456 bytes`; diferença de ~960 bytes compatível com os estados adicionais de filtro da Variante C).
- **PSRAM:** Estável em `1.663.048 bytes` livres em todos os cenários de A e C.
- **Stack watermark (`StackMinWords`, reportado em bytes no ESP-IDF):** `27.268 bytes` livres constantes em todos os cenários de A e C (margem ampla).
- **I2S:** `I2S TX Write Failures = 0` e `I2SFailDelta = 0` em todos os cenários; `audiohealth` confirmou amostras PCM não nulas (`NonZeroWords=256`, `SaturatedWords=0`, `TxBytes=1024`, `TxError=0`).
- **Reinicializações:** Nenhuma reinicialização inesperada ou watchdog durante os testes.

---

## 6. Avaliação do Ensaio Sustentado (5 Minutos)

Configuração: Variante C de produção, Reverse quarter (`rev 1`, `revmode 0`, `revdiv 0`), `fbk 85`, `tpa 80`, `ftd 25`, `wwd 25`, gerador interno (`vol 10`).

- **Duração:** `300,0 s` (21 amostras de `perf` a cada 15 s).
- **Blocos processados:** `134` -> `103.493` (`103.359` blocos adicionais, ~344,5 blocos/s ≈ 44.100 Hz / 128).
- **DSP Peak:** `2.515 µs` (`DSP Budget Overruns = 0`).
- **Full Work Peak:** `2.525 µs` (`Full Work Overruns = 0`).
- **I2S TX Write Failures:** `0`.
- **Áudio digital (`audiohealth`):** `TxError=0`, `NonZeroWords=256`, `SaturatedWords=0`.

Embora o ensaio sustentado estático de 5 minutos tenha concluído sem overruns (`Full Work Peak = 2525 µs`), ele não anula a falha observada em **T4** (`Full Work Peak = 2918 µs`, `Full Work Overruns = 1`) nem a margem crítica de **11,49 µs** em **T2**.

---

## 7. Problemas Encontrados e Análise de Causa

1. **Falha de deadline em T4 (`Full Work Peak = 2918 µs > 2902,49 µs`, `Full Work Overruns = 1`):**  
   O tempo puro de DSP em T4 foi `2471 µs`, mas `Full Work` inclui também `updateParameters()` (`MaxParamUpdUs = 426 µs` medido em T1–T5, `516 µs` em T6–T7) e o despacho I2S/FreeRTOS na task de áudio. Quando uma atualização de parâmetros ou variação de cache/PSRAM coincide com um bloco de pico da Variante C, a soma ultrapassa os `2902,49 µs`.
2. **Margem crítica em T2 Multi-Head (`Full Work Peak = 2891 µs`, `P99 = 81,72%`):**  
   Com apenas `11,49 µs` de folga, qualquer evento concorrente (atualização de parâmetros ou automação) coloca T2 em risco iminente de overrun.
3. **Causa arquitetural em `dsp/core/tape_core.cpp` (linhas 1175–1202):**  
   `colorTapeFeedback` executa por amostra em L e R: `dropoutLP.setCutoffFastInv(...)` + `dropoutLP.process(...)`, `azimuth.process(...)`, `shelf.process(...)`, `bump.process(...)`, `gap.process(...)` — totalizando 6 biquads, 2 one-poles e 2 allpasses extras por frame estéreo.

### Propostas de Otimização (Sem Alterar o DSP Sem Nova Validação Auditiva)

- **Otimização 1 (sem mudança de sonoridade):** Remover `updateParameters()` pesado de dentro do caminho crítico do bloco de áudio (ou dividir/adiar o recálculo de coeficientes dos biquads para fora do bloco com maior carga, ou evitar recalcular coeficientes quando os parâmetros suavizados já convergiram). Isso reduziria `MaxParamUpdUs` (~426–516 µs) e daria centenas de microssegundos de folga em `Full Work`.
- **Otimização 2 (otimização estrutural de `colorTapeFeedback`):** Consolidar ou simplificar filtros na cadeia de realimentação C (ex.: avaliar se `dropoutLP` pode usar coeficiente pré-calculado ou se os filtros de playback C podem compartilhar estrutura SIMD/unrolled no ESP32-S3). *Nota: qualquer alteração na resposta do DSP invalida a aprovação auditiva anterior da Variante C.*

---

## 8. Riscos Residuais

- **Automação / mudança contínua de parâmetros:** Em T1–T7 os parâmetros eram estáticos durante `bench`; com automação contínua (MIDI/UI), `MaxParamUpdUs` (426–516 µs) ocorrerá com frequência muito maior, elevando a probabilidade de `Full Work Overruns` em T2 e T4.
- **Periféricos não validados nesta campanha:** Entrada analógica I2S (`src 2`), reprodução MP3 via LittleFS, display OLED/I2C e saída analógica física do DAC PCM5102 não fazem parte do escopo desta validação digital.

---

## 9. Recomendação Técnica para a PR #56

**NÃO FAZER MERGE DA PR #56 no estado atual.**

Apesar da aprovação musical e da estabilidade de memória/stack/I2S, a **Variante C** falhou no critério objetivo de tempo real em **T4** (`Full Work Overruns = 1`, `Full Work Peak = 2918 µs > 2902,49 µs`) e opera com margem de apenas **11,49 µs** em **T2** (`P99 = 81,72%`). Recomenda-se:
1. Manter a PR #56 aberta sem merge;
2. Otimizar o custo de `updateParameters()` / `Full Work` na task de áudio (preferencialmente sem alterar o som de C) e/ou otimizar `colorTapeFeedback`;
3. Reexecutar a campanha física completa no ESP32-S3 após a otimização antes de aprovar o merge.
