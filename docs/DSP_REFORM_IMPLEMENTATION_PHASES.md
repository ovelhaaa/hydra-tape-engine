# Hydra Tape Engine — Plano de reforma do DSP por fases (instruções para agentes)

**Versão:** 1.2 — 2026-11-23 (baseline independente F0.2)
**Estado:** **PLANEJADO; nenhuma fase de implementação autorizada por este documento isoladamente.**  
**Origem técnica obrigatória:** [TAPE_DELAY_DSP_MUSICAL_AUDIT_2026-11-23.md](TAPE_DELAY_DSP_MUSICAL_AUDIT_2026-11-23.md).  
**Baseline analisada:** `main` @ `6849edf738c0d60731f51e96f7a16d1deac08cfe`. Antes de executar, revalidar a HEAD atual e quaisquer mudanças posteriores à auditoria.  
**Finalidade:** fornecer uma sequência pequena, reprodutível, reversível e musicalmente auditável para agentes que implementarão melhorias no Tape Delay. Cada fase exige nova autorização/ordem de execução, evidência de testes e aprovação humana.

> **INSTRUÇÃO OBRIGATÓRIA A QUALQUER AGENTE:** antes de tocar no DSP, leia este documento inteiro, a auditoria vinculada, as restrições de hardware e os contratos de paridade. Antes de CADA fase, releia o contrato e a checklist específicos daquela fase, compare a HEAD com a baseline e faça um relato escrito do que já existe. Não iniciar a próxima fase automaticamente. Não alterar o som aprovado para "corrigir" métricas sem escuta.

## 0. Contratos globais imutáveis

### G0.1 — Fonte de verdade, arquivos e roteamento

A produção Tape usa `src/main.cpp` → `host/native/esp32/hydra_dsp_esp32_adapter.cpp` → `dsp/core/hydra_dsp_c_api.cpp` → `dsp/core/tape_core.cpp`. Reverse V2 e Warp leem os **mesmos buffers circulares** do TapeCore. `TapeModel` em `lib/TapeDelay/src/TapeDelay.cpp` é legado; `FrippEngine` e `BubblesEngine` são modos **alternativos** do firmware, e `FreeverbEngine` é efeito pós-motor opcional: `src/main.cpp:880–954`, `docs/DSP_ARCHITECTURE_AUDIT.md:10–26`.

### G0.2 — Orçamento e tempo real

| Invariante | Limite/obrigação |
|---|---|
| Placa e configuração de referência | ESP32-S3 N16R8 @ 240 MHz, 44.100 Hz, estéreo, blocos de 128 frames, 8 MB de PSRAM. |
| Deadline por bloco | 2.902,49 µs; **zero** overrun e **zero** falha I2S. Medir também `fullWorkUs`, param update, memória e stack. |
| Meta histórica conservadora | P99 < 2.032 µs (<70%) para cenários tradicionais quando viável. Não declarar 70% como gate histórico de C: C otimizada registrou T2 multi-head P99 2.333 µs (80,38%), *physical pass*, mas pouca margem. |
| Não regressão | Comparar **mesmo firmware/flags/cenário/hardware** com baseline em P50/P95/P99/peak, full work, memória, reboots/I2S. Não aceitar regressão significativa sem análise e aprovação explícita. |
| Hot path | Proibido adicionar `malloc/new`, bloqueios, log/Serial, filesystem, memset grande em PSRAM, trigonometria/transcendentes pesadas, recálculo dispendioso de biquads, solver iterativo ou loop não limitado por amostra. |
| Memória | Não duplicar buffers longos nem criar motores paralelos Tape/Reverse/Warp; estimar bytes de estados/buffers e medir heap, PSRAM e stack físicos. |

**Fontes obrigatórias:** `docs/ESP32_S3_PERFORMANCE_AUDIT.md:7–20,93–126,160–172`; `docs/PHASE7_2E_DEADLINE_INVESTIGATION.md:59–90`; `docs/evidence/phase72e-realtime/decision.md:26–39`. A otimização histórica de C evita o pior pico de `memset` em PSRAM na audio task; não reintroduzir esse caminho.

### G0.3 — ABI, equivalência e regressão

- **API C:** manter versão, IDs, estruturas e semânticas existentes de `include/hydra_dsp.h:14–26,159–199,222–319` e `dsp/core/hydra_dsp_c_api.cpp`. Novos parâmetros públicos não são autorizados por este plano. Defaults/presets/legacy ABI devem continuar íntegros.
- **Native ↔ WASM:** usar o mesmo core canônico; `tests/core/compare_native_wasm.py:53–129` verifica input/LUT bit idênticos, 5 cenários e limiares **max_abs ≤ 1e-5, RMSE ≤ 1e-6 por cenário**. O histórico comprova `max_abs=0, RMSE=0` com `-ffp-contract=off` para CMake (`CMakeLists.txt:10–17`; `docs/PHASE6_NUMERICAL_EQUIVALENCE.md:93–106`). Não relaxar tolerância nem mudar flags globais só para passar.
- **Preservação sonora:** um candidato pode — e geralmente deve — diferir do baseline, mas o mesmo candidato deve ser consistente entre Native/WASM. Golden diferente requer **análise explícita da diferença, aprovação auditiva e autorização para regeneração**; jamais atualizar por conveniência.
- **Feedback:** testes de `scripts/phase64_feedback_stability.py` e `scripts/phase64_validate_engine.py`: aceitar auto-oscilação limitada, não confundi-la com instabilidade; não permitir NaN/Inf, teto do limiter e cauda crescendo sem controle (`docs/PHASE6_FEEDBACK_STABILITY.md:114–148`).
- **Compatibilidade musical:** não degradar ou alterar incidentalmente Reverse V2, Warp, ping-pong, freeze, tap tempo, MIDI clock, modos legacy, entradas dry, noise gate e Freeverb. Para variantes de feedback, o Warp continua sendo controle negativo bit-exact quando essa exigência já faz parte do experimento.

### G0.4 — Regras de entrega por fase

1. **Preflight só de leitura:** declarar SHA e branch, listar os arquivos lidos, identificar contratos pertinentes e registrar o que já foi resolvido. Se a HEAD mudou, reauditar pontos afetados; não trabalhar com referências desatualizadas.
2. **Hipótese verificável:** causa no código com `arquivo:linha`, efeito audível esperado, métrica mensurável, orçamento incremental de CPU/estados.
3. **Baseline congelada:** capturar build flags/toolchain, seeds, entradas WAV/float32, parâmetros, taxa, tamanho de bloco, render hashes e medições disponíveis. Não sobrescrever os WAVs/goldens aprovados.
4. **Experimentação isolada:** branch e PR **por fase**, sem misturar melhorias não relacionadas; preferir flags ou variantes reversíveis. Nenhum merge automático.
5. **Gates:** compilar Native/WASM e ESP32, rodar testes centrais, comparador estrito, estabilidade e cenários específicos; se alterar hot path ou controle físico, exigir benchmark na placa antes de promover.
6. **Escuta:** produzir pacote A/B no Listening Lab; comparar RAW e MATCHED, fontes musicais, cauda de 60 s; registrar preferência/equivalência/rejeição e confiança, sem inferir aprovação humana.
7. **Relatório de saída:** na PR registrar diff, medições, custos P99/full work, comportamento espectral/transições, compatibilidade, link dos artefatos e riscos restantes. Resultado: `APPROVED`, `REJECTED` ou `PENDING`, nunca assumir aprovação pelo teste automático.
8. **Handoff:** atualizar o estado da fase *somente após* decisão explícita; na rodada seguinte outro agente relê este plano, a auditoria, os PRs/relatórios das fases concluídas e a HEAD atual.

### G0.5 — Checklist de leitura mínima a repetir antes de CADA fase

**Núcleo:** `dsp/core/tape_core.cpp`, `tape_core.hpp`, `fast_tanh_table.inc`, `reverse_windows.hpp`, `feedback_routing_experiment.hpp`, `hydra_dsp_c_api.cpp`, `include/hydra_dsp.h`, `include/hydra_mech_noise.hpp`.  
**Integração e legado:** `src/main.cpp` (roteamento e hot path), `host/native/esp32/hydra_dsp_esp32_adapter.cpp`, `lib/TapeDelay/include/TapeDelay.h`, `lib/TapeDelay/src/TapeDelay.cpp`, `lib/TapeDelay/src/FrippEngine.cpp`, `BubblesEngine.cpp`, `FreeverbEngine.cpp`.  
**Relatórios:** `docs/DSP_ARCHITECTURE_AUDIT.md`, `HYDRA_2_DSP_ARCHITECTURE.md`, `ESP32_S3_PERFORMANCE_AUDIT.md`, `PHASE6_BIAS_ANTIALIASING_REPORT.md`, `PHASE6_FEEDBACK_STABILITY.md`, `PHASE7_2_FEEDBACK_AUDIT.md`, `PHASE7_2E_DEADLINE_INVESTIGATION.md`, `PHASE6_NUMERICAL_EQUIVALENCE.md`, `REVERSE_V2.md`, `ACCIDENTAL_WARP_SPEC.md`, e **o relatório vinculado a este plano**.  
**Validação:** `tests/core/`, `scripts/audio_analysis.py`, `scripts/musical_sources.py`, `tools/listening-lab/README.md`. Ler também a evidência física mais recente referente à variante efetivamente testada.

Essa leitura pode ser incremental em fases subsequentes **somente se** o agente indicar explicitamente os arquivos já conferidos e comprovar que não mudaram desde o último relatório; não copiar conclusões antigas sem verificação.

## 1. Mapa de ondas, dependências e estados

### Atualização F0.2 — gate offline e referência dos próximos experimentos

**`READY_FOR_DSP_EXPERIMENT`**, pela nova baseline independente
`NEW_BASELINE_REPRODUCIBLE`, autorizada expressamente na F0.2. Fonte congelada
`3cc6c1b5f4ce8b93d7665744d266a82cb42e80e2`, rota **A de produção**, sem promover
C ou branches experimentais. Dez inputs/outputs float32/PCM/WAV idênticos em
duas execuções; Native 19/19, goldens bit-exact e Native/WASM zero.
[Relatório](evidence/dsp-reform-f0-2/README.md),
[manifesto](evidence/dsp-reform-f0-2/baseline_manifest.json),
[procedimento executável](evidence/dsp-reform-f0-2/REPRODUCTION_PROTOCOL.md).

O bloqueio F0.1 por receita incompleta, descrito historicamente abaixo, foi
resolvido pela solução 3; a receita histórica não foi recuperada nem seus hashes
substituídos. O pacote externo ainda não tem URL pública; receita versionada
permite recriação, com ambiente e identidade binária explicitados.
Nenhuma F1–F11 foi implementada/aprovada/incorporada nesta fase.
Próxima fase autorizável: investigação e calibração conjunta HPF/head bump/gap
loss, após decisão explícita de rota F1; a referência disponível é A. Se a
decisão for C/C0, congelar sua receita/flags e comparação própria, sem reusar
esta baseline como se fosse C. Não iniciar sem nova ordem.
Escuta A/B e físico permanecem obrigatórios antes de promover hot path.

### Reconciliação F0.1 — leitura anterior aos contratos F1–F11

Este quadro qualifica os objetivos originais abaixo, sem apagar fases ou
autorizar implementação. **Main de produção:** `6d94af5`, rota A, API 1.7.0 / 38
parâmetros. PRs #58–#62 abertas/draft, nenhuma integrada. **Experimental #61:**
API 1.8.0 / 47, IDs 38–46 Motion; **experimental #62:** API 1.9.0 / 49, IDs 47–48
Studio. Não reutilizar esses IDs em propostas futuras nem tratá-los como produção.
G0.3 conserva a ABI da branch/target efetivamente escolhido e as estruturas
legadas; não autoriza acrescentar novos parâmetros. Versão preset continua v2.

Leitura adicional obrigatória: [reconciliação das PRs](DSP_REFORM_PR_RECONCILIATION.md),
[consolidação F0](evidence/dsp-reform-f0/CONSOLIDATION.md) e
[snapshot de SHAs/checks](evidence/dsp-reform-f0/branch-snapshot.json).
Estados abaixo descrevem implementação existente **nas branches indicadas**, não
aprovação, merge ou superioridade musical. Nenhuma F1–F11 incorporada foi comprovada.

| Fase | Objetivos reconciliados / classificação | Evidência existente e trabalho restante |
|---|---|---|
| F0 | Histórico F0.1: regeneração BLOCKED; gate offline resolvido por F0.2 acima | F0 original preservada e receita antiga desconhecida. Nova referência A F0.2: 10/10 em duas execuções, Native 19/19 e Native/WASM zero. Físico de candidatos e decisão musical continuam pendentes; não usar READY_FOR_F1 histórico como aprovação humana. |
| F1 | Independência ALREADY_IMPLEMENTED em C; recriar C SUPERSEDED; economia CONDITIONAL; escolha/promoção STILL_REQUIRED | C e seleção #58 já atendem separação do monitor. Nova rota só após demonstrar benefício de custo ainda não atendido; preservar caráter C por A/B. Encerrar F1 por decisão de rota sem código é válido. |
| F2 | Calibração espectral STILL_REQUIRED | #59 envelhecimento dinâmico não calibra HPF/bump/gap existentes. Medir por repetição; recomendação de primeiro experimento, dependente da liberação F0 e decisão F1. |
| F3 | Saturação/progressividade PARTIALLY_IMPLEMENTED; calibração STILL_REQUIRED | Dupla saturação, soft-knee e sustain limitado já existem; C/C1–C3 não provam nova curva musical 65–95%. Preservar bias/LUT. |
| F4 | Separação de monitor e transições PARTIALLY_IMPLEMENTED; ergonomia STILL_REQUIRED | #62 implementa dry unidade, wet externo e morph 30 ms; #61 slew Amount/Stereo. Não recriar Studio. Freeze/head selection/drive/tone e caudas compartilhadas continuam a medir. |
| F5 | Movimento PARTIALLY_IMPLEMENTED; tempo musical STILL_REQUIRED | #61 modula leitura e muda Motion sem reset; não unifica mudança de BPM, tap/MIDI ou delays dos cabeçotes. Preservar engine Motion ao testar alvos de tempo. |
| F6 | EQ existente ALREADY_IMPLEMENTED; calibração STILL_REQUIRED | Playback C/Studio já usam filtros; pre-emphasis da eletrônica continua escalar. Calibrar sem criar cadeias paralelas. |
| F7 | Transição entre modos ALREADY_IMPLEMENTED na #61; política temporal CONDITIONAL | Fade 150 ms é de deslocamento Motion, não dois leitores de mudança de tempo. Não descartar F7 por confusão com Motion; só abrir se F5 demonstrar necessidade. |
| F8 | HF dinâmica PARTIALLY_IMPLEMENTED em C1/C2/C3; reimplementação desses modelos SUPERSEDED; adoção CONDITIONAL | #59 já faz spectral wear/transient softening, não está na #61. Escuta achou similares: preservar C0. Pré/pós-ênfase espectral real e mérito perceptivo/custo continuam condicionais. |
| F9 | Movimento ALREADY_IMPLEMENTED experimental; adoção/estéreo PARTIALLY_IMPLEMENTED | #60/#61 M0–M3, correlação/common-diff e StereoMotion. Não recriar motor. Pan por cabeçote, matriz mono/escuta e qualificação física continuam CONDITIONAL. |
| F10 | Comparativo de interpolação CONDITIONAL | Motion mantém Hermite, sem demonstração de deficiência que justifique troca. Nenhuma PR supre estudo de F10; conservar gatilho. |
| F11 | Release STILL_REQUIRED; promoção BLOCKED | CI verde não substitui baseline regenerável, escuta das integrações, físico/headroom/rollback e decisão explícita. |

Dependências existentes: #58 → #59 e #61; #60 é referência experimental da #61;
#61 → #62. **#59 não entrou na #61.** #61/#62 partiram antes dos documentos
atuais de governança: ao eventual retarget integrar contratos/documentos sem
sobrescrever arquitetura experimental nem promover seus defaults à main.

O próximo marco proposto passa a ser **liberar F0 e decidir referência F1;
depois medir/calibrar F2**. A proposta antiga de F1-Lean é CONDITIONAL à evidência
de economia necessária, não tarefa automática. Reabrir A/B/C apenas para uma
hipótese nova mensurável; os testes existentes e a preferência C são patrimônio.
Cada preflight abaixo deve usar linhas do SHA escolhido, pois números originais
referem-se à main histórica. Nenhum objetivo marcado SUPERSEDED elimina a fase:
a justificativa e os testes de evidência permanecem acima e na reconciliação.

**Nenhum estágio está automaticamente autorizado.** "Planejada" significa somente que existe um contrato de implementação.

| Fase | Onda | Foco | Depende de | Estado inicial |
|---|---|---|---|---|
| **F0** | Preparação | Baseline, métricas, renderes e gates de referência | nenhuma | PLANEJADA |
| **F1** | 1 | Roteamento de feedback e separação do monitor (auditoria 1A) | F0 | PLANEJADA |
| **F2** | 1 | Resposta espectral do feedback / graves (1B) | F1 | PLANEJADA |
| **F3** | 1 | Gain staging, saturação e curva feedback (1C) | F2 | PLANEJADA |
| **F4** | 1 | Mix, drive, tone, transições e ergonomia (1D) | F3 | PLANEJADA |
| **F5** | 1 | Tempo, tap/MIDI, cabeçotes musicais e varispeed básico (1E) | F4 | PLANEJADA |
| **F6** | 1 | Calibração EQ reprodução / pre-emphasis escalar (1F) | F3–F5 | PLANEJADA |
| **F7** | 2 | Upgrades de tempo/varispeed somente se F5 insuficiente (2A) | F5–F6 | PLANEJADA CONDICIONAL |
| **F8** | 2 | Pré/pós-ênfase espectral e self-erasure leve (2B–2C) | F6 | PLANEJADA CONDICIONAL |
| **F9** | 2 | Wow/flutter, panorama e mono compatibilidade (2D–2E) | F6 | PLANEJADA CONDICIONAL |
| **F10** | 2 | Comparativo de interpolação, sem troca presumida (2F) | F7–F9 | ESTUDO CONDICIONAL |
| **F11** | Fechamento | Revisão musical final, hardening, release candidate | F1–F6 e decisões de F7–F10 | PLANEJADA |
| **Onda 3** | Suspensa | Oversampling, histerese completa, ADAA ampla, buffers novos | demonstração de necessidade | **NÃO AUTORIZADA** |

A sequência poderá ser reordenada *somente por decisão documentada* se os resultados de uma fase mostrarem dependência diferente. Uma fase condicional pode ser **descartada por mérito** sem bloquear F11.

## 2. Contratos de implementação de cada fase

### F0 — Auditoria de baseline reproduzível (somente leitura/teste)

**Objetivo:** provar o estado atual sem mudança do som.

**Preflight adicional:** `docs/PHASE6_NUMERICAL_EQUIVALENCE.md`, `docs/ESP32_S3_PERFORMANCE_AUDIT.md`, `docs/evidence/phase72e-realtime/decision.md`, `CMakeLists.txt`, `platformio.ini`, workflows de `.github/workflows/`, `scripts/phase64_validate_engine.py`, `tests/core/compare_native_wasm.py` e `tools/listening-lab/README.md`.

**Tarefas:** comparar HEAD com `6849edf`; enumerar flags A/B/C e build ativo; inventariar testes/gates, hash do LUT e fixture, recursos físicos e suites de escuta existentes; preservar outputs/fixtures de referência; declarar quais medições são históricas versus executadas agora.

**Aceitação:** relatório de baseline com versionamento, evidências rastreáveis e nenhuma modificação de DSP; identificação explícita do estado atual de PR/branch C. Se a baseline não estiver reproduzível, suspender F1.

### F1 — Topologia musical de feedback

**Objetivo:** tornar feedback independente da monitoração **sem perder a sonoridade escolhida em C**.

**Preflight adicional:** `dsp/core/tape_core.cpp:1106–1271`, `feedback_routing_experiment.hpp`, `docs/PHASE7_2_FEEDBACK_AUDIT.md`, `docs/PHASE7_2_CLASSIC_ARCHITECTURE.md`, `docs/evidence/phase72e-realtime/decision.md` e `scripts/package_phase72_feedback_ab.py`.

**Tarefas e não tarefas:**
- Congelar A aprovada, B limpa, C degradada e resultados físicos C existentes.
- Somente se o gate CONDITIONAL da reconciliação F0.1 demonstrar benefício novo: projetar **uma hipótese por PR** de feedback independente com custo menor, preferindo reutilizar estados/filtragem disponíveis; preservar integralmente o caminho de monitoração e as rotas Warp/Reverse conforme contrato. Caso contrário, decidir entre rotas existentes sem recriar C.
- Provar que o mix do monitor não altera a cauda por dependência estrutural, sem exigir igual loudness entre topologias distintas.
- **Não** promover C padrão só por ser mais próxima de um esquema clássico, e **não** duplicar filtros do monitor avançando seus estados duas vezes.

**Testes:** `scripts/package_phase72_feedback_ab.py`, `scripts/phase64_feedback_stability.py`, `tests/core/phase72_feedback_routing_test.cpp`, `tests/core/regression_core.cpp`, Listening Lab (high feedback, hiss after stop, reverse entry/exit, warp negative control), placa T1–T7.

**Saída para F2:** uma rota escolhida explicitamente, medições A/B/C/candidata e custo físico; ou decisão de preservar A/C atual e encerrar F1 sem mudança.

### F2 — Graves, head bump e perda espectral a cada repetição

**Objetivo:** repetir com corpo, preservar clareza e evitar ringing/embolamento.

**Preflight adicional:** `tape_core.cpp:498–565,1148–1207`, `docs/PHASE6_FEEDBACK_STABILITY.md` e F1 concluída.

**Tarefas:** medir impulso, baixo/guitarra grave/bateria e espectro por repetição; experimentar coeficientes de **filtros já existentes** (HPF 300 Hz, bump 60–200 Hz, gap loss/LPF), sempre com controle de ganho. Ajustar como sistema; evitar aumento de graves acima do que o feedback permite.

**Testes:** `scripts/musical_sources.py`, `scripts/audio_analysis.py` (extensão de bandas por repetição), estabilidade 60 s e escuta RAW/MATCHED.

**Saída:** curva de degradação identificada, comparação com F1 e decisão de manter/reverter.

### F3 — Saturação, ganho e progressividade de auto-oscilação

**Objetivo:** transição musical de eco discreto para saturado e sustain limitado.

**Preflight adicional:** `tape_core.cpp:126–171,417–423,1182–1230`, `tape_integrity.hpp:23–46` e `docs/PHASE6_BIAS_ANTIALIASING_REPORT.md`.

**Tarefas:** caracterizar `driveGain`, `feedbackDrive`, trims dos dois saturadores, `safeFeedback` e compressor; comparar curvas em 0/25/50/65/75/85/95/100% e entradas com diferentes níveis. Preferir mapeamentos/coeficientes (custo ~zero). **Preservar bias médio e a LUT atual.**

**Testes:** `scripts/phase64_feedback_stability.py`, `scripts/phase64_validate_engine.py`, `scripts/bias_antialiasing.py` e `tests/core/audible_controls.cpp`; medir RMS/crest/THD/IMD e caudas internas quando instrumentação permitir.

**Saída:** sweet spots ampliados e cauda comprovadamente limitada; caso a nova curva não seja preferida na escuta, manter a curva anterior.

### F4 — Ergonomia dos controles e transições de performance

**Objetivo:** mix/drive/tone/cabeçotes/Freeze com gestos previsíveis e sem artefatos óbvios.

**Preflight adicional:** `tape_core.cpp:442–449,757–813,1083–1097,1124–1139,1218–1235,1301`; `hydra_dsp_c_api.cpp:23–61`; `src/main.cpp:248–299`; `tests/core/regression_core.cpp:333–398`.

**Tarefas:** medir antes de adicionar smoothing; propor curvas de controle ou pequenas rampas internas de ganho sem mudar ABI/presets; investigar `freezeFade` hoje não aplicado à escrita/saída. A separação dos dois parâmetros de mix no host deve respeitar o contrato do ESP32 atual, que usa `p_mix` para ambos.

**Testes:** `scripts/audio_analysis.py:99–116`, `tests/core/regression_core.cpp`, cenários de parameter automation e Listening Lab.

**Saída:** transições avaliadas, sem instabilidade e sem mudança acidental do nível dos presets existentes.

### F5 — Tempo musical, tap tempo e MIDI clock

**Objetivo:** unificar política temporal mantendo caráter de capstan, previsibilidade rítmica e transport.

**Preflight adicional:** `tape_core.cpp:345,386–399,434–437,681–713,789–808,894–906,1124–1139`; `dsp/core/global_transport_clock.hpp`, `tap_tempo.hpp`, `midi_clock_sync.hpp`; `docs/REVERSE_V2.md`.

**Tarefas:** classificar mudanças lentas, grandes passos, divisão rítmica e recovery MIDI; estabelecer limites de pitch/slew antes de tocar no algoritmo; primeiro tentar controle de alvo e transição barata sem duplicar três cabeçotes por canal. Não quebrar sincronismo Reverse V2.

**Testes:** `tests/core/tap_tempo_test.cpp`, `tests/core/midi_clock_test.cpp`, `tests/core/transport_clock_test.cpp`, `tests/core/reverse_core_test.cpp`; `scripts/audio_analysis.py:99–116,131–175`; escuta com synth polifônico e bateria.

**Saída:** especificação objetiva do comportamento em tap/MIDI e A/B de glide mecânico versus clock-synced; novos leitores de transição só serão propostos na F7 se necessário.

### F6 — EQ final e calibração do estágio chamado pré-ênfase

**Objetivo:** consolidar resposta de gravação/reprodução com filtros existentes.

**Preflight adicional:** `tape_core.cpp:483–565,1148–1207`; `docs/PHASE6_BIAS_ANTIALIASING_REPORT.md`; resultados F2/F3.

**Tarefas:** esclarecer nos comentários/documentação que a "pre-emphasis" atual é ganho escalar antes da saturação; calibrar shelf/bump/gap loss/ganhos sem novo DSP. **Não** implementar pré-ênfase espectral real nesta fase — reservar para F8 se necessária.

**Testes:** `scripts/audio_analysis.py`, `scripts/bias_antialiasing.py`, `scripts/diagnose_bias_whine.py`, harmônicos de guitarra/piano/pratos e RAW/MATCHED.

**Saída:** nova referência sonora da Onda 1 aprovada ou manutenção do baseline, com todo o comportamento F1–F6 documentado.

### F7 — Varispeed estrutural (condicional)

**Gatilho:** F5 demonstrar cliques/pitch transitório inaceitáveis que não podem ser corrigidos por smoothing/alvo de baixo custo.

**Preflight adicional:** `tape_core.cpp:305–317,894–906,1124–1139`; `reverse_windows.hpp`; medições de memória/CPU de F5.

**Tarefas:** avaliar transições curtas com leitores alternativos **somente quando estritamente necessário**, estimar número de leituras PSRAM e custo por bloco antes de implementar; não duplicar todos os taps permanentemente.

**Testes:** timing/pitch/discontinuity e worst-case físico em multi-head com automação; regressões Reverse/Warp/transport.

**Saída:** implementar apenas se ganho auditivo compensa custo e risco; senão marcar `SKIPPED_BY_EVIDENCE`.

### F8 — Caráter espectral dinâmico (condicional)

**Gatilho:** F6 ficar audivelmente aquém em transientes/nível alto, com evidência espectral clara.

**Preflight adicional:** `tape_core.cpp:126–171,483–565,1191–1230`; `fast_tanh_table.inc`; `docs/PHASE6_BIAS_ANTIALIASING_REPORT.md`.

**Tarefas:** A/B separado de (a) pré/pós-ênfase *espectral* barata e (b) self-erasure por envelope. Usar 1 polo/biquad já existente quando possível; controlar estabilidade dos coeficientes e aliasing sem oversampling amplo. Não juntar os dois numa única hipótese irreversível.

**Testes:** `scripts/bias_antialiasing.py`, `scripts/musical_sources.py`, `scripts/audio_analysis.py` e gate de feedback interno.

**Saída:** cada técnica aprovada/rejeitada individualmente; sem novas opções públicas.

### F9 — Movimento orgânico e imagem estéreo (condicional)

**Gatilho:** após F6, escuta indicar falta de coerência de afinação ou pouca separação na mix.

**Preflight adicional:** `include/hydra_mech_noise.hpp`, `tape_core.cpp:470–482,902–912,1124–1139,1266–1269`; `tests/core/audible_controls.cpp`.

**Tarefas:** calibrar combinação de componente comum, componente L/R e profundidade de wow/flutter; teste separado de pan discreto por cabeçote. Preservar determinismo e não expor novos parâmetros sem autorização.

**Testes:** áudio estéreo, soma mono, medidas Mid/Side, correlação intercanal e drift de pitch; fontes polifônicas e percussivas.

**Saída:** largura sutil sem cancelamento perceptível; se apenas "mais estéreo", rejeitar.

### F10 — Interpolação e precisão da leitura (estudo condicional)

**Gatilho:** análise isolada demonstrar artefato significativo de Hermite sob modulação e uma alternativa de custo comparável.

**Preflight adicional:** `tape_core.cpp:305–317,1114–1139`; `docs/ESP32_S3_PERFORMANCE_AUDIT.md:69–97`.

**Tarefas:** benchmark *offline* Hermite versus alternativas sob swept sine, fractional delay e modulação; comparar magnitude/fase, overshoot e acessos PSRAM. **Nenhuma mudança de produção por preferência teórica.**

**Testes:** novo microteste isolado em `tests/core/`, `scripts/audio_analysis.py` e benchmark físico `membench/stageprof` quando aplicável.

**Saída:** rejeitar troca se não superar Hermite com audição e custo satisfatórios.

### F11 — Release candidate e aprovação humana

**Preflight adicional:** auditoria inicial, este plano, relatórios individuais das PRs F0–F10, HEAD final, evidence física, política C e contratos Native/WASM.

**Tarefas:** limpar experimentos temporários apenas com revisão; confirmar documentação da cadeia real; gerar novos renders de longa duração e pacote cego comparando baseline aprovada com candidato final; executar suites completas em 44,1/48 kHz, Reverse/Warp e firmware físico. Preservar o firmware anterior para rollback.

**Critérios de assinatura:** (1) todos os gates verdes sem relaxamento; (2) escuta documentada e aprovada; (3) zero falhas I2S/overruns e margem de full-work medida; (4) heap/PSRAM/stack estáveis; (5) ABI/presets/reverse legacy preservados; (6) diferenças de golden justificadas e explicitamente autorizadas; (7) estratégia de rollback; (8) merge separado e autorizado.

## 3. Validação operacional: comandos, instrumentos e comparações

**Preflight Native genérico** (adaptar ao ambiente de build existente, sem gerar arquivos na árvore rastreada):

~~~sh
cmake -S . -B build-native -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-native -j
ctest --test-dir build-native --output-on-failure
python scripts/phase64_feedback_stability.py
python scripts/phase64_validate_engine.py
~~~

Os testes de WASM/ABI e `compare_native_wasm.py` usam os executáveis e targets configurados no CMake/workflows do repo; o agente **deve ler** os workflows antes de montar os comandos (não supor nomes de artefato). Exemplo de argumentos do comparador: `--native <runner> --wasm-js <runner.js>`; os paths reais devem ser obtidos dos builds e comprovados.

**Físico ESP32-S3:** `platformio.ini`, `docs/PHASE7_2C_PHYSICAL_TEST_GUIDE.md:8–22,109–166`, `docs/PHASE7_2E_DEADLINE_INVESTIGATION.md`. Rodar baseline e candidato no mesmo hardware, cenário e configuração. Registrar `identity`, `perf`, `audiohealth`, `bench`, `fullWorkUs`, atualização de parâmetros, memória/stack e I2S. Dados históricos **não** substituem nova medição após qualquer alteração do hot path.

**Escuta:** `tools/listening-lab/README.md:11–43`; `scripts/musical_sources.py:15–335`; `scripts/audio_analysis.py:18–302`. Priorizar guitarra limpa/saturada, synth mono/poli, bateria, percussão irregular, silêncio e tails 60 s, com amostras/seed/ganho controlados. `RAW` avalia ganho real; `MATCHED` reduz viés de loudness. Exportar decisões e proveniência SHA.

## 4. Template obrigatório de handoff ao próximo agente

Ao concluir uma fase, publicar na PR/relatório um quadro equivalente a:

~~~text
FASE: F__
STATUS: PENDING | APPROVED | REJECTED | SKIPPED_BY_EVIDENCE
BASE HEAD / CANDIDATE HEAD: <SHA> / <SHA>
ARQUIVOS LIDOS: <lista + linha quando relevante>
CAUSA / HIPÓTESE AUDÍVEL: <explicação>
DIFERENÇAS IMPLEMENTADAS: <arquivos e linhas>
SEM MUDANÇAS NÃO RELACIONADAS: sim/não + justificativa
BACKEND NATIVE/WASM: <max_abs, RMSE, CI>
TESTES CORE/ABI/REVERSE/WARP: <resultados>
SONIC A/B RAW/MATCHED: <manifesto, SHA, decisão humana ou pending>
CPU FÍSICA: <P50/P95/P99/peak DSP e full-work, baseline/candidate>
I2S / OVERRUNS / MEMÓRIA / STACK: <medidas>
GOLDENS / API / PRESETS: <preservados ou mudança autorizada>
RISCO RESIDUAL / ROLLBACK: <informação>
AUTORIZAÇÃO DA PRÓXIMA FASE: <explícita ou ausente>
~~~

**Regra final para os agentes:** este documento é um **contrato de implementação e revisão**, não autorização para executar todas as fases em sequência. Antes de cada nova PR, consultar o relatório de auditoria, o histórico de PRs já incorporadas e o contrato da fase. Quando um avanço não soar melhor, **rejeitar/reverter** e preservar a baseline musical. O critério de excelência é *resultado audível por ciclo de CPU*, com robustez na placa real e equivalência numérica Native/WASM.
