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

### FASE 1 — Accidental Warp V1 (MVP do Modo Experimental)
- **Status:** EM EXECUÇÃO (Rodada Atual).
- **Objetivos:**
  - Implementar o leitor Dual-Tap Warp em `dsp/core/tape_core.cpp` sobre o buffer de fita compartilhado.
  - Defasagem de $180^\circ$ entre leitores com envoltórias triangulares complementares ($\text{gain}_1 + \text{gain}_2 = 1.0$).
  - Transposição estritamente **ASCENDENTE** calibrada ($v_{eff} \approx 1 + \text{speedMultiplier}$).
  - Interpolação cúbica Hermite4 de alta precisão.
  - Laço de feedback com HPF (120 Hz), LPF anti-aliasing (8 kHz), soft saturation e controle de ganho.
  - Deslocamento de fase estéreo (`stereoWarp`).
  - Suporte ao modo laboratório 1-Tap (Bug Cru) para aferição A/B com o protótipo Web.
  - Extensão backward-compatible da API C (`HYDRA_DSP_PARAM_WARP_*`).
- **Entregáveis:**
  - `dsp/core/tape_core.hpp` e `dsp/core/tape_core.cpp` atualizados com estrutura `WarpReader`.
  - `include/hydra_dsp.h` e `dsp/core/hydra_dsp_c_api.cpp` versionados com novos parâmetros.
  - Teste de unidade dedicado para Warp (`tests/core/warp_core_test.cpp`).
  - Atualização do mapeamento Web em `host/web/main.js` e `host/web/hydra-processor.js`.
- **Critério de Saída:** Transposição ascendente comprovada numericamente (+12 semitons a $2.0\times$), zero regressão no Forward delay, testes CTest e build ESP32-S3 aprovados.

---

### FASE 2 — Reverse Tape Echo Correction (Mode B)
- **Status:** PLANEJADO.
- **Objetivos:**
  - Eliminar o contador de reversão compartilhado assimetricamente entre canais estéreo.
  - Implementar dois leitores reversos sobrepostos com janelamento suave (Hann ou senoidal) para mitigar descontinuidades na fronteira dos ciclos.
  - Garantir inversão temporal estrita (ataques crescentes / swell) com preservação de fase estéreo.
  - Sincronização da extensão da janela de reversão com o andamento musical.
- **Critério de Saída:** Teste acústico de impulso confirmando rampa temporal invertida e ausência de estalos transitórios.

---

### FASE 3 — Tap Tempo Compartilhado & Sistema Rítmico de 3 Cabeças
- **Status:** PLANEJADO.
- **Objetivos:**
  - Implementar o controlador de transporte e tap tempo centralizado (`hydra_tempo_controller`).
  - Debounce inteligente de cliques manuais e cálculo de média ponderada com rejeição de outliers.
  - Suporte a dois modos de transição de andamento:
    - **Tape Slew:** Variação física de pitch idêntica à alteração de rotação do motor da fita.
    - **Clean Sync:** Crossfade cruzado asséptico sem desvio acentuado de afinação.
  - Subdivisões musicais por cabeça (1/4, 1/8, 1/8D, 1/8T, 1/16).
  - Sincronização do período dos grãos do Warp e da janela do Reverse ao relógio mestre.
- **Critério de Saída:** Transições de tempo estáveis sem cliques ou travamento do buffer.

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
- **Status:** PLANEJADO.
- **Objetivos:**
  - Interface Web visual com controle completo dos três modos (Tape, Reverse, Warp).
  - Mapeamento de encoders físicos, botões e display OLED SSD1306 no firmware ESP32-S3.
  - Integração de importação/exportação de presets em formato JSON padronizado.
  - Medições físicas de uso de CPU (Core 0/1) e largura de banda da PSRAM sob estresse contínuo.
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
