# HYDRA TAPE ENGINE 2.0 — DEVELOPMENT ROADMAP

## Fase 6.4 / 6.4.1 — recuperação CI e decisão de release (2026-11-20)

A Fase 6.4 introduziu LUT tanh canônica, smoothing de filtros e suspensão antes de reset. A aprovação Windows foi generalizada incorretamente para Linux. A Fase 6.4.1 reproduziu a falha sem cache, demonstrou entrada/LUT idênticas e isolou a causa atual: FMA habilitado pelo target x86-64-v3 padrão do Ubuntu 26.04. Com `-ffp-contract=off`, o Core Equivalence oficial passa em Ubuntu 24.04 e 26.04 com cinco cenários e max_abs=RMSE=0. O código DSP e os flags ESP32 não foram alterados.

Bias recuperado, testes de feedback interno de 60 s, transições de cinco parâmetros e reset real A–E possuem workflows obrigatórios. Pacote final: 10 pares PCM24, incluindo feedback de 60 s, manifesto/hashes/player. Escuta humana `PENDING_HUMAN_AUDITION`; runtime físico `PHYSICAL_RUNTIME_PENDING`. Os cinco workflows obrigatórios passaram no HEAD `e2c9497`. O estado completo da CI e links reais constam no [Release Decision Record](PHASE6_RELEASE_DECISION.md). **Sem merge.** Os números e aprovações das fases anteriores abaixo são históricos.

## Fase 6.3 — otimização de tempo real do Reverse (2026-11-19)

**PASS humano 10/10 no pacote `7431a6d`; BLOCKED — REGRESSION; sem merge.** Perfil físico por estágio identificou o custo do Reverse no controle de fronteiras de subdivisão (duas avaliações em precisão dupla por amostra): 1039,8 → 78,3 µs/bloco com coleta por bloco exatamente reconciliada. O wipe de ~3 MB na ativação do delay foi substituído por época lógica O(1): `MaxParamUpdUs` 82237 → 549 µs. Benchmark físico com 10 000 blocos por cenário: zero overruns em nove cenários; sustentação de cinco minutos com zero overruns, heap/PSRAM estáveis (stack mínimo 27 284 bytes). Os três testes Native dependentes da portadora foram recuperados (Native 15/15); WASM funcional passa, restando a equivalência estrita da issue #48 com os mesmos números. Pacote A/B de 10 pares em `artifacts/listening-lab/phase6-reverse-optimization/`, bit-exact (max_abs = RMSE = 0), aprovado por escuta humana em 10/10 cenários no commit `7431a6d` com hashes verificados e sem critérios individuais fornecidos. P99 de Reverse 2095–2113 µs (72,2–72,8%), acima da meta preferencial de 70%, com zero overruns. Permanecem como bloqueios explícitos: issue #48, quatro critérios de cauda e a transição a 44,1 kHz (herdados da Fase 6.2). Ver [relatório](PHASE6_REVERSE_REALTIME_OPTIMIZATION.md) e [registro de escuta](PHASE6_REVERSE_OPTIMIZATION_HUMAN_AUDITION.md). Não realizar merge.

## Fase 6.2 — candidata de anti-aliasing do bias (2026-11-19)

**Regressões Native e overruns de Reverse resolvidos na Fase 6.3; restam issue #48 e os critérios de cauda/transição do motor completo.** Média de fase do bias selecionada após seis variantes isoladas; critérios espectrais/transferência 49/49, ensaio completo 22/27. Goldens preservados e aprovados; equivalência estrita Native/WASM continua falhando. O suplemento de 60 pares usa o Listening Lab existente, candidata aprovada por escuta humana em 12/12 cenários do pacote local f0c16cd (hashes verificados). Ver [relatório e pendências](PHASE6_BIAS_ANTIALIASING_REPORT.md) e [Fase 6.3](PHASE6_REVERSE_REALTIME_OPTIMIZATION.md). Não realizar merge.

## 1. Visão Geral das Fases de Desenvolvimento

Este documento estabelece o roteiro técnico detalhado para a evolução do Hydra Tape Engine da versão consolidada 1.2 até a versão completa 2.0. Cada fase é autocontida, com entregáveis específicos, dependências declaradas e critérios de aceitação objetivos.

```mermaid
flowchart LR
    F0["Fase 0<br/>Consolidação & Docs"] --> F1["Fase 1<br/>Accidental Warp V1"]
    F1 --> F2["Fase 2<br/>Reverse Correction"]
    F2 --> F3["Fase 3<br/>Global Transport Clock"]
    F3 --> F4["Fase 4<br/>Tap Tempo V1"]
    F4 --> F5["Fase 5<br/>Warp Creative & HW MVP"]
```

---

## 2. Detalhamento das Fases

### FASE 0 — Consolidação, Governança & Documentação Base
- **Status:** CONCLUÍDO.
- **Objetivos:**
  - Auditar a integração da PR #46 (`codex/consolidate-tape-core`) na branch `main`.
  - Proteger a integridade de `dsp/core/tape_core.cpp` como núcleo canônico.
  - Produzir a documentação definitiva do produto, arquitetura DSP, especificação matemática do Warp, roteiro e matriz de validação acústica.
  - Mapear a tabela de compatibilidade binária (ABI) e IDs de parâmetros da API C.
- **Entregáveis:**
  - `docs/HYDRA_2_PRODUCT_VISION.md`
  - `docs/HYDRA_2_DSP_ARCHITECTURE.md`
  - `docs/ACCIDENTAL_WARP_SPEC.md`
  - `docs/HYDRA_2_ROADMAP.md`
  - `docs/HYDRA_2_AUDIO_VALIDATION.md`
- **Critério de Saída:** Build nativo, suíte de 9 testes CTest passando em 100%, e build PlatformIO ESP32-S3 limpo.

---

### FASE 1 — Accidental Warp V1.3
- **Status:** ACCIDENTAL WARP V1.3 — MERGED na main em 16/11/2026 pela [PR #47](https://github.com/ovelhaaa/hydra-tape-engine/pull/47), [commit de merge](https://github.com/ovelhaaa/hydra-tape-engine/commit/20f65698643473d934f574eea3d602a8a58385bb).
- Leitor granular ascendente sobre o buffer de fita, com modos Hermite e Raw Integer, janelas dual-tap, fase estéreo e retorno Warp separado.
- Contrato legado de 116 bytes preservado e API expandida com setter versionado. Integração web exercitada com WASM real. O firmware expõe tempo DSP, overruns de orçamento e falhas de escrita I2S separadamente.
- A bancada offline renderiza Worklet, Python persistente e TapeCore em 11 cenários comuns, com toggle nativo adicional. Em hardware ESP32-S3 (rev. 0.2 @ 240 MHz, 44,1 kHz / 128 frames), o firmware otimizado (`c9bc17e`) eliminou os overruns nos 11 cenários e sustentou 5 min contínuos na cadeia completa com `P99 = 1.861 µs` (`64,12%` do orçamento) e zero falhas I2S. Consulte os limites e as evidências em [Validação de áudio](HYDRA_2_AUDIO_VALIDATION.md) e na [Auditoria de desempenho ESP32-S3](ESP32_S3_PERFORMANCE_AUDIT.md).
- **Encerramento:** [Escuta do de-click aprovada](MAGNETIC_BIAS_DECLICK_SIGNOFF.md) e validação física concluída. A exceção Native/WASM do [gate de release](ACCIDENTAL_WARP_RELEASE_GATE.md) vale exclusivamente para a PR #47. A [issue #48](https://github.com/ovelhaaa/hydra-tape-engine/issues/48) acompanha a dívida técnica.

---

### FASE 2 — Reverse Tape Echo Correction (Mode B)
- **Status:** CONCLUÍDO & MERGED na main em 16/11/2026 pela [PR #49](https://github.com/ovelhaaa/hydra-tape-engine/pull/49), commit de merge `4e39f60`.
- **Entregáveis:** Leitores reversos sobrepostos com janelamento smoothstep e modos Rhythmic, Free e Legacy em `dsp/core/reverse_windows.hpp`. Aprovado por unanimidade (10/10) na escuta cega humana do Hydra Listening Lab ([docs/REVERSE_V2_LISTENING_SIGNOFF.md](REVERSE_V2_LISTENING_SIGNOFF.md)).
- **Critério de Saída:** Validação de áudio, 100% de aprovação na escuta e integração completa na C-API 1.4, Web UI e firmware ESP32-S3.

---

### FASE 3 — Global Transport Clock & Sincronização Musical
- **Status:** PHASE 3 — GLOBAL TRANSPORT CLOCK: MERGED na main em 16/11/2026 pela [PR #50](https://github.com/ovelhaaa/hydra-tape-engine/pull/50), commit de merge `77a5225`.
- **Objetivos Cumpridos:**
  - Infraestrutura temporal centralizada com resolução sample-accurate e zero deriva acumulada (`dsp/core/global_transport_clock.hpp`).
  - Suporte a 10 subdivisões rítmicas racionais, métricas arbitrárias válidas ($1 \dots 32$ com denominadores em $\{2, 4, 8, 16\}$), e dinâmica completa de transporte (Play, Pause, Stop, Reset, Locate).
  - Preservação estrita de fase musical durante variações de BPM em tempo real sem saltos ou cliques.
  - Sincronização de fase do Reverse Tape Echo V2 em modo Rhythmic com alinhamento na grade global.
  - Expansão da C-API para versão 1.5.0 com funções dedicadas de transporte e diagnósticos de histórico limitado preservando estritamente a ABI (116, 140 e 148 bytes).
  - Hardening completo contra NaN, Infinity e valores inválidos/negativos no DSP e C-API com preservação de estado e retorno `-1`.
  - Resolução da falha de compilação Xtensa GCC 14.2 para ESP32-S3 (`std::clamp`/`std::max` type mismatch corrigido).
  - Telemetria integrada na Web UI (AudioWorklet + display visual de beats via `DataView`, badge de status e alerta visual de limitação de histórico) e na CLI do ESP32-S3 (`tempo`, `transport play|pause|stop|reset|status` com indicação de `Reverse Sync : LIMITED`).
  - Validação física em placa real ESP32-S3 N16R8 @ 240 MHz com zero overruns e 54,76% de uso de CPU sustentado em 5 minutos ininterruptos.
  - Pacote de 10 cenários objetivos gerado no Hydra Listening Lab (`artifacts/listening-lab/gtc-reverse-sync/`).
  - Precedência clara entre efeitos: Accidental Warp possui prioridade operacional sobre o Reverse V2 quando ambos estão ativos.
  - Aceite formal da exceção da divergência histórica Native/WASM (Issue #48) sem novas regressões.
- **Critério de Saída:** 13/13 testes CTest aprovados, 21/21 testes web aprovados, zero regressão e documentação definitiva em [docs/GLOBAL_TRANSPORT_CLOCK.md](GLOBAL_TRANSPORT_CLOCK.md).

---

### FASE 4 — Tap Tempo V1 — Performance-Ready Musical Tempo Input
- **Status:** PHASE 4 — TAP TEMPO V1: MERGED na main em 17/11/2026 pela [PR #51](https://github.com/ovelhaaa/hydra-tape-engine/pull/51).
- **Objetivos Cumpridos:**
  - Componente independente de estimação de andamento em tempo real sem alocações dinâmicas (`dsp/core/tap_tempo.hpp`).
  - Fonte única autoritativa de tempo musical preservada estritamente no Global Transport Clock (GTC), sem relógios paralelos nem reinicialização da posição musical.
  - Janela de debounce de 80 ms rejeitando disparos espúrios sem perda da referência temporal do último toque válido.
  - Estimativa robusta com histórico circular de até 5 intervalos, estimador por mediana e rejeição explícita de outliers relativos (> 20%).
  - Resposta ágil no 2º tap (estimativa responsiva imediata), confirmação de consistência no 3º tap (travamento) e refinamento progressivo da confiança nos taps subsequentes.
  - Reconhecimento dinâmico de mudanças intencionais de andamento (2 intervalos consecutivos confirmando a nova cadência e reancorando o histórico).
  - Timeout de sessão de 3.0 segundos para separação de sequências de taps preservando o último BPM confirmado no GTC.
  - Convenção explícita de 1 toque = 1 semínima (quarter-note) na faixa nominal estrita de 30 a 300 BPM (2000 ms a 200 ms).
  - Publicação de telemetria formalmente segura em C++17 via Triple Buffer lock-free (`StatePublisher<T>`), eliminando a data race do seqlock com payload não-atômico.
  - Fila SPSC de comandos (`SpscQueue<DspHandoffCommand, 32>`) com rejeição não-bloqueante e delimitação de latência máxima ao bloco de áudio (2,90 ms).
  - Expansão da C-API para versão 1.6.0 (`hydra_dsp_tap_tempo_event`, `hydra_dsp_tap_tempo_get_state`, `hydra_dsp_tap_tempo_reset`) preservando rigorosamente a ABI existente (116, 140 e 148 bytes).
  - Integração Web completa (AudioWorklet + botão TAP com suporte a toque, mouse e atalho de teclado 'T', feedback de status `TAP TO SET TEMPO`, `ACQUIRING`, `TEMPO LOCKED`, `TAP REJECTED` e telemetria em tempo real).
  - Interface CLI no ESP32-S3 (`tap`, `tap status`, `tap reset`) operando sem bloqueios no processamento de áudio.
  - Arquitetura desacoplada e preparada para footswitch físico de contato momentâneo via GPIO/ISR.
  - Suíte de 20 testes automatizados em C++ cobrindo todos os cenários funcionais, invariantes e de estresse de concorrência, 24 testes web e 14/14 suites CTest passando em 100%.
  - Pacote de 10 cenários objetivos gerado no Hydra Listening Lab (`artifacts/listening-lab/tap-tempo-v1/`).
  - Validação em placa real ESP32-S3 N16R8 @ 240 MHz com zero overruns e execução sustentada.
- **Critério de Saída:** Validação técnica completa e autorização formal de merge cumprida integralmente.

---

### FASE 5 — Sincronização MIDI Clock Externa V1 (Fase 5.2 Estabilização Final)
- **Status:** CONCLUÍDO & VALIDADO (PR #52 MERGED EM MAIN).
- **Entregas Principais:**
  - Componente de sincronização em tempo real `MidiClockSync` operando na norma MIDI 1.0 (24 PPQN).
  - Estimativa de andamento em tempo real com buffer circular de histórico e filtragem estatística de jitter.
  - PLL digital proporcional de fase mantendo o Global Transport Clock (GTC) rigorosamente alinhado à posição do mestre externo com modulação limitada de slew rate (máx 4-5% de variação), eliminando artefatos audíveis de wow/flutter ou saltos de pitch.
  - **Fase 5.2 Hardening — Autoridade Estrita de Fonte na C-API**: `hydra_dsp_transport_set_bpm()` roteia via `TapeCore::setManualBpm()`, preservando `manualBpm` armazenado sem contornar a fonte ativa (INTERNAL, TAP ou MIDI EXTERNAL). O cache de parâmetros e commit não corrompem o andamento externo durante a sincronização.
  - **Fase 5.2 Hardening — Recuperação Determinística de Grandes Desvios de Fase**: Separação clara entre *Fine Phase Tracking* (PLL proporcional sem locate para erros fracionários $\le 0.5$ beat) e *Large Phase Recovery* (realinhamento seguro via `GTC::locate()` acionado após persistência de $\ge 12$ pulsos com deslocamento inteiro $\ge 1.0$ beat e frequência estável, preservando buffers de delay e áudio).
  - **Fase 5.2 Hardening — Web MIDI End-to-End**: Validação do conversor `webMidiTime.js` e `processBatchMidiEvents` garantindo monotonicidade estrita, rejeição de duplicatas ($< 1$ ms) e eventos fora de ordem sem a mutação ingênua `prevFrame + 1`. Suporte a `BigInt` de 64-bit no `hydra-processor.js` (AudioWorklet) e validação ponta a ponta com WASM DSP atingindo status LOCKED e RMSE temporal $< 0.1$ frame.
  - **Fase 5.2 Hardening — Validação de Deadband do Reverse V2**: 0.1 BPM deadband testado deterministicamente sob jitter temporal ($\pm 0.5$ ms a $\pm 5$ ms) e degraus de tempo ($120 \to 140$ e $140 \to 90$ BPM), confirmando zero reconfigurações espúrias ou resets de áudio.
  - Rejeição antecipada de mensagens fora de ordem e duplicadas antes do incremento da contagem de pulsos.
  - Máquina de estados de sincronização determinística: `DISCONNECTED`, `ACQUIRING`, `LOCKED`, `HOLDOVER`, `LOST`.
  - Tratamento musical e determinístico de mensagens de transporte: Start (`0xFA`), Continue (`0xFB`) e Stop (`0xFC`), com preservação de caudas de áudio, feedback e buffers.
  - Parser de mensagens de tempo real com preservação de `runningStatus` em mensagens intercaladas.
  - Fila dedicada Lock-Free SPSC de eventos MIDI (`SpscQueue<DspMidiEvent, 64>`) no ESP32-S3 com detecção de overflow (`midiOverflowCount`), purga de pulsos corrompidos e reset automático de aquisição.
  - Publicação de telemetria em Triple Buffer (`StatePublisher<hydra_dsp_midi_clock_state>`).
  - Elevação da C-API para versão 1.7.0 mantendo conformidade e estabilidade estrita de ABI (116, 140 e 148 bytes).
  - Comandos de terminal serial no firmware ESP32-S3 (`clock source <internal|tap|midi>`, `clock status`, `clock reset`, `clock event <hex>`).
  - Suíte de 23 subtestes dedicados em C++ (`hydra_midi_clock_test`), 35 testes web em Node.js e 15/15 testes CTest passando em 100%.
  - Pacote de 12 cenários sonoros e temporais no Hydra Listening Lab (`artifacts/listening-lab/midi-clock-external-v1/`).
  - **Exceção Histórica Issue #48 Formalmente Documentada**: Divergência pré-existente Native/WASM mantida dentro do baseline aceito (`max_abs = 5.968e-06`, `rmse = 2.344e-06`), não introduzida pelo MIDI Clock e mantida aberta para investigação futura de ponto flutuante.
- **Critério de Saída:** Código completo, testes 100% aprovados, builds ESP32-S3, Native e WASM validados, e artefatos de escuta gerados. Pronto para merge.

---

### HOTFIX 5.2.1 — MIDI Clock V1.2.1 (WASM BigInt Interop & Web Release Recovery)
- **Status:** CONCLUÍDO & VALIDADO.
- **Diagnóstico:** Regressão pós-merge da PR #52 na suite Web (`webMidiClock.test.mjs`) gerando `TypeError: Cannot convert a BigInt value to a number`. O Emscripten legalizava parâmetros de 64 bits (`uint64_t`/`i64`) para pares `(i32, i32)` quando compilado sem `WASM_BIGINT=1`, colidindo com o binding `cwrap(..., ['number', 'number', 'bigint'])`.
- **Solução:**
  - Habilitação explícita de `-sWASM_BIGINT=1` global e no target `hydra_dsp_web` em `CMakeLists.txt`, gerando ABI WASM nativa com tipos `i64`.
  - Normalização segura de argumentos temporais de 64 bits via `toUint64BigInt` em `host/web/hydra-processor.js` (cobrindo `midiClockEvent`, `tapTempoEvent` e `transportLocate`).
  - Rastreamento contínuo de frames no AudioWorklet (`totalFramesProcessed`).
  - Validação abrangente de limites 64-bit (`0ULL`, `1000ULL`, `2^32-1`, `5e9`, `MAX_SAFE_INTEGER`, `UINT64_MAX`) nos testes de ABI C++ e na suíte de testes integrada Web (15/15 testes em `webMidiClock.test.mjs`, 40/40 testes web no total).
  - Preservação estrita dos algoritmos de DSP, PLL, GTC e Tap Tempo sem alterações de comportamento acústico ou temporal.
- **Critério de Saída:** Suíte Web 100% aprovada (40/40), CTest Native 100% aprovado (15/15), CTest WASM 100% aprovado (3/3), equivalência Native/WASM dentro do limite da issue #48, e firmware ESP32-S3 compilado com sucesso.

---

### FASE 6 / 6.1 — Validação Musical e Listening Lab
- **Status:** READY FOR HUMAN LISTENING; pacote Linux publicado e validado; escuta humana `PENDING_HUMAN_AUDITION`.
- **PR:** [#54](https://github.com/ovelhaaa/hydra-tape-engine/pull/54), branch `feature/musical-validation-phase6`; sem merge automático.
- Renderizador portátil, seeds fixas, 20 cenários, 40 WAVs PCM24, 20 CSVs, parâmetros/eventos e SHA-256 no manifesto. A CI Linux repete renderização e valida arquivos antes do upload do artifact `hydra-phase6-listening-lab`.
- Interface dedicada `tools/phase6-listening-lab/`: A/B no mesmo relógio, seek, loop, waveforms, eventos, avaliações e JSON importável/exportável. A interface histórica e a Web Demo foram preservadas.
- **Evidência:** Native offline e WASM/Node simulando AudioWorklet são categorias distintas de Browser Real e ESP32-S3 físico. Firmware: **FIRMWARE BUILT / FLASHED — PHYSICAL RUNTIME NOT VERIFIED**; medições antigas permanecem baseline histórico.
- **Issue #48 aberta:** CI Linux estrita no baseline: max_abs 3.6681e-05, RMSE 1.585565032192e-05, FAIL. Comparação local Windows: max_abs 5.968e-06, RMSE 2.344106e-06, FAIL. Thresholds e goldens inalterados. Não classificar a suíte estrita como aprovada.
- **Gates:** pacote Linux publicado e íntegro, interface validada e decisão humana explícita. Ver [relatório e links reais de evidência](PHASE6_MUSICAL_VALIDATION_REPORT.md) e [guia de escuta](PHASE6_LISTENING_GUIDE.md). A conclusão musical e a revisão de merge permanecem pendentes.

---

### FASE 7 — Expansão Criativa do Accidental Warp & MVP Final
- **Status:** PLANEJADO.
- **Sub-etapas:**
  1. **Warp Head:** Habilitar uma das 3 cabeças do tape delay clássico para operar simultaneamente como leitor Warp ascendente.
  2. **Warp Character:** Macrocontrole de textura interpolando entre *Smooth* (Hermite4), *Grainy* (triangular evidente) e *Broken* (leitura inteira crua).
  3. **Spiral Feedback:** Dosagem do sinal granular na realimentação magnética com compensação dinâmica de energia.
  4. **Rhythmic Warp:** Quantização temporal das fronteiras de grão com a grade rítmica do transporte.
  5. **Stereo Orbit:** Modulação contínua do ângulo de fase estéreo dos grãos.
  6. **Hold / Infinite Freeze:** Sustentação e isolamento instantâneo do segmento de fita em loop granular infinito.
- **Critério de Saída:** Demonstrações sonoras e fixtures auditivas aprovadas para os 10 presets de referência.

---

### FASE 5 — Integração de UX, Hardware ESP32 & MVP Final
- **Status:** PLANEJADO (meta de folga DSP em hardware físico já comprovada na PR #47 com `P99 < 65%` e `0` overruns; veja [Auditoria ESP32-S3](ESP32_S3_PERFORMANCE_AUDIT.md)).
- **Objetivos:**
  - Interface Web visual com controle completo dos três modos (Tape, Reverse, Warp).
  - Mapeamento de encoders físicos, botões e display OLED SSD1306 no firmware ESP32-S3.
  - Integração de importação/exportação de presets em formato JSON padronizado.
  - Medições físicas de uso de CPU (Core 0/1) e largura de banda da PSRAM sob estresse contínuo (baseline DSP e largura de banda PSRAM já auditados na Fase E: `PSRAM 192k 4Tap+Wr = 143,736 µs/128` e cadeia completa 5 min com `61,05%` médio / `64,12%` P99).
- **Critério de Saída:** Firmware estável em hardware físico executando a 44.1 kHz estéreo com folga de processamento (> 30% idle time).

---

## 3. Matriz de Riscos Técnicos e Mitigações

| Risco Técnico | Severidade | Probabilidade | Estratégia de Mitigação |
| :--- | :---: | :---: | :--- |
| **Gargalo de Banda na PSRAM do ESP32-S3** | Alta | Média | Utilizar barramento Octal SPI (OPI) a 80 MHz, aplicar alinhamento de 32 bits, restringir PSRAM estritamente à linha de retardo e alocar todos os filtros e tabelas LUT na SRAM interna rápida. |
| **Aliasing Excessivo na Leitura Acelerada do Warp** | Alta | Alta | Inserir filtro passa-baixas anti-aliasing calibrado antes do laço de retorno e utilizar interpolação Hermite4 de 4 pontos. |
| **Instabilidade / Runaway em Feedback Elevado** | Alta | Média | Incorporar filtro passa-altas de corte de offset subsônico (120 Hz/300 Hz), saturador racional soft-knee e limitador assintótico no laço de retorno. |
| **Divergência Numérica entre Nativo e WASM** | Média | Baixa | Utilizar tabela estática `FastTanhLUT` compartilhada, evitar funções matemáticas dependentes de implementação de libc (`tan`, `sin` não padronizados) e validar com tolerâncias RMSE $< 10^{-6}$. |
| **Cliques Transitórios no Reajuste de Tap Tempo** | Média | Alta | Implementar desacoplamento entre tempo alvo e tempo suavizado via filtro de 1 polo de 200 ms ou crossfade de buffers duplos. |


### FASE 6.4.1 — Recuperação CI e evidência de release (2026-11-20)
- **AUTOMATED_CI_PASS — escuta humana e runtime físico pendentes; sem merge.**
- Builds limpos reproduziram a falha Ubuntu 26.04 por FMA; o controle e a correção CMake passam em ambos os Ubuntus. Entrada e LUT idênticas bit a bit.
- Diagnósticos de bias recuperados (49/49 local), LUT histórica explícita, full-engine 27/27 e feedback interno de 60 s em workflow obrigatório.
- Transições incluem drive, velocidade, idade, tone e head bump (0.01797/0.01420 local, limite 0.03).
- Reset real A–E validado com WASM limpo; concorrência, erro e timeout tratados. Pacote final 10 pares/20 PCM24/10 CSVs com publicação e player real na CI.
- ESP32-S3 build PASS; console identificado mas sem resposta: `PHYSICAL_RUNTIME_PENDING`. Escuta final `PENDING_HUMAN_AUDITION`.
- Matriz e links atuais: [Release Decision Record](PHASE6_RELEASE_DECISION.md). Resultados Windows não substituem aprovação da CI oficial.

### FASE 6.5 — Recuperação física e aceitação de tempo real (2026-11-21)
- **PHYSICAL_RUNTIME_PASS — escuta humana final pendente; sem merge.** Escopo: gerador interno e I2S no nível do driver, sem escuta/captura analógica ou teste de RX externo.
- Console e boot recuperados por reset watchdog do esptool após silêncio reproduzido no caminho RTS; identidade de commit/fontes/ELF confirmada na placa.
- Defeito físico medido no host: varredura de stack a cada bloco custava 1143–1144us fora do antigo cronômetro. A consulta foi movida para a CLI, preservando DSP, parâmetros, prioridades e afinidade.
- Sete cenários com 10000 blocos/256 warmup; sustentado de 300.2168s/103399 blocos. Zero overruns DSP/trabalho completo e zero falhas TX I2S observadas; heap/PSRAM estáveis e stack mínima de 27268 bytes.
- P99 máximo2215us (76.31% do orçamento), trabalho completo máximo2820us; a meta preferencial70% não foi obtida fora de Delay. Divisões históricas 1/8 e 1/16 estavam rotuladas incorretamente e não são comparações diretas.
- `PHYSICAL_MIDI_CLOCK_NOT_TESTED`; LittleFS/OLED apresentam limitações documentadas; aprovação humana do pacote final continua `PENDING_HUMAN_AUDITION`.
- Evidências, reprodução e limites: [Aceitação física](PHASE65_PHYSICAL_HARDWARE_ACCEPTANCE.md). CI do HEAD atual: checks e links da PR#54, sem transferir resultados antigos.
