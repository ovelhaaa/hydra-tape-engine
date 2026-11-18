# HYDRA TAPE ENGINE 2.0 — DEVELOPMENT ROADMAP

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

### FASE 5 — Sincronização MIDI Clock Externa V1 (Fase 5.1 Hardened)
- **Status:** CONCLUÍDO & REVISADO (PR #52 PRONTA PARA MERGE REVIEW).
- **Entregas Principais:**
  - Componente de sincronização em tempo real `MidiClockSync` operando na norma MIDI 1.0 (24 PPQN).
  - Estimativa de andamento em tempo real com buffer circular de histórico e filtragem estatística de jitter.
  - PLL digital proporcional de fase mantendo o Global Transport Clock (GTC) rigorosamente alinhado à posição do mestre externo com modulação limitada de slew rate (máx 3% de variação), eliminando artefatos audíveis de wow/flutter ou saltos de pitch.
  - **Fase 5.1 Hardening**: Separação estrita de erro de fase absoluto e erro de fase embrulhado (`absolutePhaseErrorBeats` e `wrappedPhaseErrorBeats`), impedindo que ambiguidades de batidas inteiras (1 e 2 beats) gerem falso travamento.
  - Rejeição antecipada de mensagens fora de ordem e duplicadas antes do incremento da contagem de pulsos.
  - Máquina de estados de sincronização determinística: `DISCONNECTED`, `ACQUIRING`, `LOCKED`, `HOLDOVER`, `LOST`.
  - Tratamento musical e determinístico de mensagens de transporte: Start (`0xFA`), Continue (`0xFB`) e Stop (`0xFC`), com preservação de caudas de áudio, feedback e buffers.
  - Parser de mensagens de tempo real com preservação de `runningStatus` em mensagens intercaladas.
  - **Arbitragem Estrita de Andamento**: Modelo explícito `manualBpm`, `tapEstimatedBpm`, `midiEstimatedBpm`, `effectiveBpm` e `tempoSource`. Atualizações manuais não sobrescrevem a referência MIDI nem restauram a fonte silenciosamente; toques durante MIDI External não sequestram o transporte. Todas as 6 transições de modo livres de estalos (*clickless*).
  - **Deadband de 0.1 BPM no Reverse V2**: Protege contra flutuações e recálculos desnecessários de janelas reversas causados por microajustes contínuos do PLL.
  - Fila dedicada Lock-Free SPSC de eventos MIDI (`SpscQueue<DspMidiEvent, 64>`) no ESP32-S3 com detecção de overflow (`midiOverflowCount`), purga de pulsos corrompidos e reset automático de aquisição.
  - Publicação de telemetria em Triple Buffer (`StatePublisher<hydra_dsp_midi_clock_state>`).
  - Elevação da C-API para versão 1.7.0 mantendo conformidade e estabilidade estrita de ABI (116, 140 e 148 bytes).
  - Comandos de terminal serial no firmware ESP32-S3 (`clock source <internal|tap|midi>`, `clock status`, `clock reset`, `clock event <hex>`).
  - **Preservação de Timestamps Web MIDI**: Módulo `host/web/webMidiTime.js` convertendo `event.timeStamp` diretamente para o domínio de frames de áudio, compensando latência e preservando espaçamento em lotes sem colapso.
  - Suporte completo no frontend Web (Web MIDI API, seletor de porta, seletor de fonte, badges e telemetria no AudioWorklet).
  - Suíte de 20 testes dedicados em C++ (`hydra_midi_clock_test`), 31 testes web em Node.js e 15/15 testes CTest passando em 100%.
  - Pacote de 12 cenários sonoros e temporais no Hydra Listening Lab (`artifacts/listening-lab/midi-clock-external-v1/`).
- **Critério de Saída:** Código completo, testes 100% aprovados, build ESP32-S3 e WASM validados, e artefatos de escuta gerados. Pronto para merge review.

---

### FASE 6 — Expansão Criativa do Accidental Warp & MVP Final
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
