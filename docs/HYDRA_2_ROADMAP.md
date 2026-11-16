# HYDRA TAPE ENGINE 2.0 — DEVELOPMENT ROADMAP

## 1. Visão Geral das Fases de Desenvolvimento

Este documento estabelece o roteiro técnico detalhado para a evolução do Hydra Tape Engine da versão consolidada 1.2 até a versão completa 2.0. Cada fase é autocontida, com entregáveis específicos, dependências declaradas e critérios de aceitação objetivos.

```mermaid
flowchart LR
    F0["Fase 0<br/>Consolidação & Docs"] --> F1["Fase 1<br/>Accidental Warp V1"]
    F1 --> F2["Fase 2<br/>Reverse Correction"]
    F2 --> F3["Fase 3<br/>Tap Tempo & 3-Head"]
    F3 --> F4["Fase 4<br/>Warp Creative"]
    F4 --> F5["Fase 5<br/>UX & Hardware MVP"]
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
- **Status:** IMPLEMENTADO & VALIDADO (branch `feature/global-transport-clock`).
- **Objetivos Cumpridos:**
  - Infraestrutura temporal centralizada com resolução sample-accurate e zero deriva acumulada (`dsp/core/global_transport_clock.hpp`).
  - Suporte a 10 subdivisões rítmicas racionais, métricas 4/4, 3/4 e 6/8, e dinâmica completa de transporte (Play, Pause, Stop, Reset, Locate).
  - Preservação estrita de fase musical durante variações de BPM em tempo real sem saltos ou cliques.
  - Sincronização de fase do Reverse Tape Echo V2 em modo Rhythmic com alinhamento na grade global.
  - Expansão da C-API para versão 1.5.0 com funções dedicadas de transporte preservando estritamente a ABI (116, 140 e 148 bytes).
  - Telemetria integrada na Web UI (AudioWorklet + display visual de beats) e na CLI do ESP32-S3 (`tempo`, `transport play|pause|stop|reset|status`).
  - Pacote de 10 cenários objetivos gerado no Hydra Listening Lab (`artifacts/listening-lab/gtc-reverse-sync/`).
- **Critério de Saída:** 13/13 testes CTest aprovados, 21/21 testes web aprovados, zero regressão e documentação definitiva em [docs/GLOBAL_TRANSPORT_CLOCK.md](GLOBAL_TRANSPORT_CLOCK.md).

---

### FASE 4 — Expansão Criativa do Accidental Warp
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
