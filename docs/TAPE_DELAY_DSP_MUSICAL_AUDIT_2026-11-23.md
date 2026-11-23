# Hydra Tape Engine — Auditoria técnica e musical do Tape Delay

**Data:** 2026-11-23  
**Repositório:** [ovelhaaa/hydra-tape-engine](https://github.com/ovelhaaa/hydra-tape-engine)  
**Baseline de leitura:** `main`, commit [`6849edf738c0d60731f51e96f7a16d1deac08cfe`](https://github.com/ovelhaaa/hydra-tape-engine/commit/6849edf738c0d60731f51e96f7a16d1deac08cfe)  
**Natureza da evidência:** leitura do código e dos relatórios de experimentos/medições existentes. Nenhum novo benchmark, render ou teste de placa foi executado nesta auditoria.  
**Escopo da rodada:** apenas análise e documentação; nenhum código foi modificado.  
**Plano de execução vinculado:** [DSP_REFORM_IMPLEMENTATION_PHASES.md](DSP_REFORM_IMPLEMENTATION_PHASES.md).

> **Regra de interpretação:** cada risco sonoro indicado abaixo é uma hipótese fundamentada no código, não prova de defeito audível. É obrigatório distinguir problema demonstrado, risco técnico e preferência musical. Não substituir o som previamente aprovado sem A/B, gate objetivo e decisão explícita.

## 1. Mapa real da cadeia DSP atual

A implementação em produção do modo Tape usa a cadeia `src/main.cpp` → `HydraDspEsp32Adapter` → API C `hydra_dsp_*` → `TapeCore`. A entrada estéreo I2S ou do sintetizador passa por HPF/LPF e ganho de gravação, é somada ao feedback, tratada por DC blocker e gravada através de `TapeMagnetics` (LUT de saturação, bias médio e estado de remanência) em buffers L/R circulares na PSRAM. Os cabeçotes Tape convencionais, Reverse V2 e Accidental Warp leem a *mesma* memória. A leitura convencional é somada opcionalmente à textura curta; a reprodução recebe dropout, azimute e hiss. A monitoração, adicionalmente, usa shelf, head bump e gap loss, é combinada com o seco e limitada. **O roteamento padrão A extrai o feedback ANTES da EQ final de reprodução**, depois de ganhos de monitoração/hiss/dropout; passa por HPF, head bump, gap loss, allpass, dupla saturação racional, ganho e compressor de joelho suave antes de retornar ao gravador magnético. Reverse Smear e spring internos podem ser inseridos depois da saída limitada; o firmware pode aplicar ainda noise gate e Freeverb externos. Evidência: `src/main.cpp:860–954`; `dsp/core/tape_core.cpp:1106–1315`; `dsp/core/hydra_dsp_c_api.cpp:330–336`.

~~~text
Fonte I2S / sintetizador → entrada L/R
              ├──────────────────────────────► dry ──────────────┐
              └► HPF → LPF → record drive                        │
                                   │                             │
                         soma com feedback ◄────────────────┐    │
                                   │                        │    │
                            DC blocker                      │    │
                                   │                        │    │
                          TapeMagnetics                      │    │
                                   │                        │    │
                          PSRAM tape L/R                     │    │
                       ┌───────────┼──────────────┐         │    │
                  cabeçotes      Reverse V2      Warp       │    │
                       └───────────┼──────────────┘         │    │
                           leitura + textura                │    │
                                   │                        │    │
                           dropout → azimute → hiss          │    │
                                   ├─ feedback A ────────────┤    │
                                   │  HPF → bump → gap loss  │    │
                                   │  → phase → sat ×2       │    │
                                   │  → gain → soft knee ────┘    │
                                   │                             │
                                   └─ shelf → bump → gap loss     │
                                                │                 │
                                            wet mix ◄─────────────┘
                                                │
                                        output limiter
                                                │
                                   smear / spring opcionais
                                                │
                             gate / Freeverb opcionais no host
                                                │
                                             I2S out
~~~

**Classificação de componentes:**

| Arquivo | Papel real e status |
|---|---|
| `dsp/core/tape_core.cpp`, `tape_core.hpp` | **Core canônico ativo** de Tape/Reverse/Warp/spring. Fonte de verdade para DSP. (`tape_core.hpp:10–86`; `tape_core.cpp:851–1315`) |
| `dsp/core/fast_tanh_table.inc` | Tabela canônica de saturação magnética, utilizada no modelo de escrita. Não regenerar sem teste de integridade numérica. (`tape_core.cpp:156–171`; `docs/PHASE6_NUMERICAL_EQUIVALENCE.md:17–28`) |
| `dsp/core/reverse_windows.hpp` | Reverse V2 ativo (overlap, smoothstep, sincronização e limites); Reverse V1 persiste como modo legado de compatibilidade. (`reverse_windows.hpp:19–39,70–100`; `tape_core.cpp:1049–1062`) |
| `dsp/core/feedback_routing_experiment.hpp` | Rotas B e C opt-in apenas por macro; fora da compilação padrão. (`feedback_routing_experiment.hpp:5–25`; `tape_core.cpp:1163–1175`) |
| `dsp/core/hydra_dsp_c_api.cpp`, `include/hydra_dsp.h` | API C comum aos ambientes Native, WASM e ESP32, versão 1.7, 38 IDs. (`hydra_dsp.h:14–26,159–199`) |
| `include/hydra_mech_noise.hpp` | Modulação mecânica estocástica determinística; já faz filtragem de ruído. (`hydra_mech_noise.hpp:16–60`) |
| `lib/TapeDelay/src/TapeDelay.cpp` | `TapeModel` **legado**, não usado como motor tape de produção; o header histórico ainda contém tipos para outros engines. (`docs/DSP_ARCHITECTURE_AUDIT.md:16–26,65–70`) |
| `lib/TapeDelay/src/FrippEngine.cpp`, `BubblesEngine.cpp` | Motores **alternativos selecionáveis** no firmware, com buffers próprios; substituem o TapeCore durante seu modo. (`src/main.cpp:580–597,880–900`) |
| `lib/TapeDelay/src/FreeverbEngine.cpp` | Pós-processamento opcional **ativo** no firmware, após engine/gate. (`src/main.cpp:598–603,951–954`) |

O manifesto `lib/TapeDelay/library.json:1–7` compila Fripp, Bubbles e Freeverb, não o `TapeModel` legado. A especificação `docs/HYDRA_2_DSP_ARCHITECTURE.md:1–13` exige memória Tape/Reverse/Warp compartilhada: não introduzir motores paralelos desnecessários.

## 2. Diagnóstico ranqueado por impacto musical

### CRÍTICO — A1. Feedback dependente de mix e duplo blend Reverse

**Evidência:** `dsp/core/tape_core.cpp:1149–1177,1266–1271`; `src/main.cpp:274–276`.  
**Fato técnico:** o feedback A nasce de `tapeSig + reverseBlend * (reverseSig - tapeSig)`, sendo `tapeSig` influenciado por ganho de textura/delay, hiss, dropout e azimute. O firmware mapeia `p_mix` em `dryWet` *e* `delayWet`. `delaySig` já incorpora uma transição forward/reverse (`tape_core.cpp:1143–1146`).  
**Impacto provável:** alterar mix muda também decay, saturação e limiar de auto-oscilação; Reverse pode alterar energia no loop de maneira não intuitiva. Referências clássicas (RE-201/Echoplex) inspiram separação entre nível ouvido e recirculação, mas não bastam para alegar fidelidade física.  
**Trabalho já realizado:** A/B/C experimentados, com C selecionada por audição; otimização física de C passou no ESP32-S3 mas continua **sem merge/opt-in** (`docs/PHASE7_2_FEEDBACK_AUDIT.md:29–74`; `docs/PHASE7_2_CLASSIC_ARCHITECTURE.md:13–78`; `docs/evidence/phase72e-realtime/decision.md:1–6,26–59`).  
**Direção:** comparar A/B/C com uma alternativa de feedback independente e barata; não promover C automaticamente.

### CRÍTICO — A2. Mudanças de tempo têm duas semânticas diferentes

**Evidência:** `dsp/core/tape_core.cpp:345,386–399,434–437,894–906,1124–1136`.  
**Fato técnico:** a leitura convencional usa `smoothedDelaySamples` com smoothing de aproximadamente 200 ms; quando `headsMusical` está ativo, os delays dos cabeçotes vêm diretamente dos valores derivados do BPM.  
**Impacto provável:** varispeed suave no controle livre versus saltos de endereço/afinação em tap/MIDI clock, sobretudo em acordes.  
**Direção:** política explícita e testada de slew mecânico para gestos graduais e transição controlada para grandes alterações sincronizadas; sem duplicar sempre todos os leitores.

### ALTO — A3. Pré-ênfase no loop não é espectral

**Evidência:** `dsp/core/tape_core.cpp:483–485,1191–1207`.  
**Fato técnico:** `feedbackPreEmph` e `feedbackDeEmph` são ganhos escalares antes/depois de duas funções `fastSatRational`. Não são filtros que enfatizam/deseatenuam bandas de frequência.  
**Impacto provável:** excitação harmônica diferente, mas sem equivalência à pré/pós-ênfase analógica.  
**Direção:** calibrar primeiro a saturação existente; testar filtro espectral barato só após A/B.

### ALTO — A4. Filtro de graves no feedback possivelmente excessivo

**Evidência:** `dsp/core/tape_core.cpp:498–505,520–530,558–565,1191–1197`.  
**Fato técnico:** HPF de feedback em 300 Hz (Q 0,5) antes de head bump aproximadamente 60–200 Hz; filtros são úteis contra acúmulo, mas seus efeitos podem se contradizer.  
**Impacto provável:** ecos limpos e pouco embolados, porém sem corpo em notas graves e bateria.  
**Direção:** medir evolução de 80–250 Hz por repetição, recalibrando HPF/bump/LPF em conjunto, não removendo o HPF.

### ALTO — A5. Gain staging e comportamento entre 65–95% de feedback

**Evidência:** `dsp/core/tape_core.cpp:417–423,1182–1230`; `dsp/core/tape_integrity.hpp:23–46`.  
**Fato técnico:** drive de gravação até 5×, ganho adicional de feedback, duas saturações racionais, compressor soft-knee e `safeFeedback` limitado a 0,88.  
**Impacto provável:** a região musical entre decay limpo, compressão e sustain pode ter progressividade insuficiente.  
**Correção de diagnóstico:** `docs/PHASE6_FEEDBACK_STABILITY.md:74–108` demonstrou *limited sustain* estável por 60 s, sem crescimento interno ocultado pelo limitador: não classificar o loop como instável por padrão.  
**Direção:** progressividade e compensação perceptiva preservando o plateau estável.

### ALTO — A6. HF compressão dependente do nível não é modelada separadamente

**Evidência:** `dsp/core/tape_core.cpp:557–565,1150–1158,1191–1207`.  
**Fato técnico:** já existem playback shelf, head bump, gap loss, dropout, magnetização e filtragem de feedback. Os coeficientes de playback seguem sobretudo parâmetros estáticos ou suavizados, não o nível instantâneo de gravação.  
**Impacto provável:** transientes fortes não recebem a degradação adicional de HF que poderia sugerir *self-erasure*; a saturação global, contudo, já é dependente da amplitude.  
**Direção:** em Onda 2, filtro/env barato somente se trouxer melhora clara a guitarras, piano e pratos.

### MÉDIO — A7. Wow/flutter já são aperiódicos; calibrar correlação e profundidade

**Evidência:** `include/hydra_mech_noise.hpp:16–60`; `dsp/core/tape_core.cpp:470–482,902–912,1124–1136`.  
**Fato técnico:** ruído mecânico filtrado, estados independentes L/R e pequeno residual periódico; não é apenas seno. A modulação dos cabeçotes acompanha proporções do delay.  
**Impacto provável:** movimento musical em doses leves, mas possível desafinação excessiva em acordes com parâmetros extremos.  
**Direção:** preservar geradores atuais; ajustar correlação e mapeamentos.

### MÉDIO — A8. Estéreo multicebeçote pouco deliberado

**Evidência:** `dsp/core/tape_core.cpp:442–447,1124–1139,1266–1269`.  
**Fato técnico:** três cabeçotes convencionais compartilham pesos entre canais; largura vem de diferença mecânica L/R ou ping-pong, não de pan individual.  
**Impacto provável:** espacialidade menos definida; soma de leituras correlatas pode gerar comb filtering, **sem evidência atual de cancelamento mono severo**.  
**Direção:** distribuição estéreo sutil com testagem Mid/Side e soma mono, sem criar uma matriz de parâmetros.

### MÉDIO — A9. Hiss/dropout têm interação, porém exigem auditoria perceptiva

**Evidência:** `dsp/core/tape_core.cpp:928–950,1150–1158`; `include/hydra_mech_noise.hpp:25–60`.  
**Fato técnico:** hiss tem ducking por entrada, variação com dropout/scrape e seeds independentes L/R; em C experimental, somente fração 0,20 é regravada (`feedback_routing_experiment.hpp:19–24`).  
**Impacto provável:** níveis extremos podem sobrepor música/caudas; em doses menores, integram a textura.  
**Direção:** escuta de pausas, cauda longa e sinal denso antes de mudar níveis.

### MÉDIO — A10. Transições de mix, cabeçote e Freeze

**Evidência:** `dsp/core/tape_core.cpp:442–449,1083–1097,1124–1139,1224–1230,1301`.  
**Fato técnico:** Reverse e playback EQ já usam suavização. Seleção de cabeçotes e ganho normal podem mudar na fronteira do bloco; `freezeFade` é atualizado mas não condiciona a escrita que depende diretamente de `freeze`.  
**Impacto provável:** clicks ou degraus de energia sob automação; ainda a medir.  
**Direção:** rampas de ganho seletivas, sem redesenhar Freeze.

### REFINO — A11. Hermite 4 pontos não é gargalo demonstrado

**Evidência:** `dsp/core/tape_core.cpp:305–317,1114–1139`.  
**Fato técnico:** interpolação fracionária Hermite cúbica de 4 pontos, adequada a delay modulado. A resposta pode variar com fração e alta frequência; não foi provado dano musical relevante. Warp de leitura inteira (`tape_core.cpp:998–1032`) é deliberadamente lo-fi.  
**Direção:** comparar offline resposta magnitude/fase e custo, somente trocar após escuta e teste físico.

### REFINO — A12. Não desfazer o trabalho resolvido em bias e Reverse

**Evidência:** `dsp/core/tape_core.cpp:156–171`; `dsp/core/reverse_windows.hpp:19–39,70–100`; `docs/PHASE6_BIAS_ANTIALIASING_REPORT.md:104–143,195–205`; `docs/REVERSE_V2.md:60–122`.  
**Fato técnico:** bias por média de duas LUTs suprimiu produtos da portadora triangular; Reverse V2 tem janelas overlap/smoothstep e modo V1 de compatibilidade.  
**Direção:** preservar comportamento até evidência de problema. Corrigir futuramente a discrepância entre diagrama arquitetural idealizado de feedback (`docs/HYDRA_2_DSP_ARCHITECTURE.md:87–122`) e rota padrão A do código.

## 3. Plano priorizado em três ondas

### Orçamento, ABI e paridade como gates invariantes

- ESP32-S3 240 MHz, estéreo **44.100 Hz**, **128 frames**, deadline **2.902,49 µs**; meta histórica P99 < **2.032 µs** (70%). `docs/ESP32_S3_PERFORMANCE_AUDIT.md:7–20,160–172`.
- Caso C otimizado multi-head: P99 **2.333 µs** (80,38%), full-work peak **2.604 µs**, sem overruns/I2S nos testes registrados. **Não extrapolar a folga** da versão A para C ou novos candidatos. `docs/evidence/phase72e-realtime/decision.md:26–39`.
- O diagnóstico de deadline encontrou `memset` de 32 KB em PSRAM dentro da audio task custando 650–759 µs e risco de atualizações pesadas de filtros: `docs/PHASE7_2E_DEADLINE_INVESTIGATION.md:59–90`.
- Em Native/WASM, o comparador usa máximo absoluto `1e-5` e RMSE `1e-6` por cenário, verifica igualdade bit a bit de input/LUT; `-ffp-contract=off` corrige contração FMA host. `tests/core/compare_native_wasm.py:53–129`; `docs/PHASE6_NUMERICAL_EQUIVALENCE.md:93–106`; `CMakeLists.txt:10–17`.
- Preservar API 1.7/38 parâmetros, memória compartilhada e testes Native, WASM, Reverse, Warp, feedback, ABI e goldens; não autorizar atualização de golden para ocultar alteração sonora.

### Onda 1 — Quick Wins (ordem de execução)

| ID | Intervenção | Custo CPU adicional previsto | Musicalidade buscada |
|---|---|---|---|
| **1A** | Decidir rota de feedback independente da monitoração via A/B/C e versão enxuta que reutilize filtros existentes. | Aproximadamente zero para B; baixo/variável para híbrido; C integral já comprovou custo relevante. | Caudas independentes do mix sem perder o caráter aprovado de C. |
| **1B** | Recalibrar HPF/bump/gap loss do loop de forma conjunta. | Quase zero (coeficientes atuais). | Corpo de graves com definição por repetição. |
| **1C** | Equalizar ganho de gravação, ganho entre saturadores e progressividade 65–95% feedback. | Quase zero. | Aumento de sustain e saturação gradual/expressivo. |
| **1D** | Mapear mix/drive/tone de modo perceptivo; compensação de volume/rampas apenas onde medido. | Muito baixo. | Sweet spots maiores e automação sem saltos. |
| **1E** | Uniformizar slew de delay e cabeçotes musicais, mantendo varispeed intencional. | Baixo, se sem múltiplos leitores extras. | Transições rítmicas previsíveis e glide mecânico musical. |
| **1F** | Recalibrar playback EQ e ganho pré-saturação atual; nenhuma nova etapa DSP. | Quase zero. | Brilho preservado sem agressividade desnecessária. |

**Primeiro marco recomendado:** 1A + 1B; depois 1C, antes de avançar para novos algoritmos.

### Onda 2 — Melhorias estruturais de custo contido

| ID | Intervenção | Custo estimado relativo | Risco |
|---|---|---|---|
| **2A** | Política híbrida de tempo (slew mecânico + transição curta para saltos), sem duplicação permanente de todas as leituras. | Baixo, possivelmente moderado durante transições. | Pico de acesso PSRAM / fase. |
| **2B** | Pré/pós-ênfase espectral verdadeira por filtro(s) de 1ª ordem onde comprovadamente útil. | Baixo a moderado. | Regressão de timbre/aliasing e custo de recálculo. |
| **2C** | Compressão de HF dependente de nível com envelope barato e coeficientes controlados. | Baixo. | Abafamento artificial / modulação audível. |
| **2D** | Correlação refinada wow/flutter L/R e profundidade por cabeçote usando estados atuais. | Muito baixo. | Desafinação excessiva. |
| **2E** | Estéreo deliberado de cabeçotes com ganhos/pan discretos e soma mono monitorada. | Muito baixo. | Cancelamento mono / comb. |
| **2F** | Benchmark de interpoladores fracionários; manter Hermite até comprovação. | Predominantemente pesquisa offline; implementação variável. | CPU/PSRAM e alteração de timbre. |

**Diretriz de hot path:** nenhuma sugestão presume oversampling amplo, `tanhf`/`expf`/`powf` em amostra, divisões em software, novos buffers longos ou recálculo de 12+ biquads por atualização. CPU estimado não equivale a validação física.

### Onda 3 — Ideias de alto risco ou custo (não iniciar)

1. Oversampling 2× localizado e ADAA de primeira ordem apenas para estágio de aliasing medido; não aplicar genericamente.
2. Oversampling 4×/8× da cadeia completa.
3. Histerese física com solver iterativo.
4. Matriz de feedback/cabeçotes com canais e buffers extras.
5. Novos motores de degradação paralelos e controles públicos extensos.

**Reabertura:** requer problema audível demonstrado, variante isolada com custo físico medido e justificativa de que Ondas 1–2 não oferecem solução mais barata.

## 4. Protocolo de validação objetiva e perceptiva

Cada candidato deve conservar baseline/HEAD, seed, amostra de entrada, sample rate, bloco, parâmetros e toolchain; registrar SHA dos WAVs e diferenças numéricas. Usar escuta **RAW** e **MATCHED** no `tools/listening-lab/` (`tools/listening-lab/README.md:24–43`). A/B cego deve registrar preferência, equivalência ou rejeição e confiança; melhoria objetiva não dispensa aprovação sonora.

### Testes por intervenção — Onda 1

| ID | Reuso/extensão indicada | Critério objetivo e critério audível |
|---|---|---|
| **1A** | `scripts/package_phase72_feedback_ab.py`; `scripts/phase64_feedback_stability.py`; `tests/core/regression_core.cpp`; Listening Lab. | Variar mix com feedback fixo, medir decay/energia, picos internos e cauda 60 s; ouvir se cauda mantém timbre enquanto mix muda. |
| **1B** | `scripts/musical_sources.py`; estender `scripts/audio_analysis.py` com bandas por repetição. | Evolução 80–250 Hz, médios, >3 kHz; ouvir grave com corpo sem embolo ou ringing. |
| **1C** | `scripts/phase64_validate_engine.py`; `scripts/phase64_feedback_stability.py`; `tests/core/audible_controls.cpp`. | RMS, crest factor, THD/IMD, envelopes e plateau estável para feedback 65–100%; ouvir sustain gradual sem colapso áspero. |
| **1D** | `tests/core/regression_core.cpp:333–363`; `scripts/audio_analysis.py:18–59,99–116`; Listening Lab. | Medir derivadas/steps e loudness em automação de mix, drive, tone; ouvir ausência de click e de salto indevido de volume. |
| **1E** | `tests/core/tap_tempo_test.cpp`; `tests/core/midi_clock_test.cpp`; `tests/core/regression_core.cpp`; análise de tempo em `scripts/audio_analysis.py:131–175`. | Steps BPM, taps irregulares, MIDI clock; medir glitch, timing e pitch transitório; ouvir acordes/percussão. |
| **1F** | `scripts/bias_antialiasing.py`; `scripts/diagnose_bias_whine.py`; `scripts/audio_analysis.py`. | Resposta de frequência, distorção e alias residual; ouvir brilho e definição dos ecos sem perder a correção de bias já aprovada. |

### Testes por intervenção — Onda 2

| ID | Reuso/extensão indicada | Critério objetivo e critério audível |
|---|---|---|
| **2A** | `tests/core/regression_core.cpp`; `tests/core/reverse_core_test.cpp`; `scripts/audio_analysis.py:99–116,131–175`. | Tempo e inclinação de mudanças de pitch, descontinuidade por evento, P99 de transição; ouvir acordes sustentados e transientes. |
| **2B** | `scripts/bias_antialiasing.py`; `scripts/musical_sources.py`; análise espectral estendida. | Evolução harmônica e alias por repetição em loudness pareado; ouvir se o brilho se integra melhor e se a saturação não endurece. |
| **2C** | `scripts/musical_sources.py:15–112,160–266`; `scripts/audio_analysis.py` estendido. | HF de notas fortes/fracas com espectro comum, transientes e IMD; ouvir compressão de brilho sem abafamento global. |
| **2D** | `tests/core/audible_controls.cpp:92–155`; `scripts/audio_analysis.py:118–129`. | Pitch drift e correlação L/R; ouvir amplitude orgânica com acordes afinados. |
| **2E** | `tests/core/regression_core.cpp:152–176,207–234`; `scripts/audio_analysis.py:118–129` e extensão M/S. | Energia Mid/Side, correlação e soma mono; ouvir largura real sem sumiço de elementos no centro. |
| **2F** | Teste isolado novo sob `tests/core/` e `scripts/audio_analysis.py` com swept sine e leitura fracionária modulada. | Magnitude/fase/erro e custo de leitura; aceitar somente com ganho auditivo repetível e deadline aprovado. |

### Estímulos e percepção

Usar fontes já disponíveis em `scripts/musical_sources.py`: guitarra limpa, guitarra saturada, synth mono e polifônico, bateria e percussão irregular (`:15–335`). Acrescentar janelas de silêncio e caudas de pelo menos 60 s para os regimes de feedback alto. Comparar tanto em solo quanto em uma mix fixa. Não compensar loudness silenciosamente: conservar RAW e MATCHED como avaliações distintas.

### Gates de release por candidato

1. **Numerical:** `tests/core/compare_native_wasm.py:53–129`; inputs e LUT bit idênticos, máximos existentes por cenário sem relaxamento; `CMakeLists.txt:10–17` com `-ffp-contract=off`. Resultados históricos de `docs/PHASE6_NUMERICAL_EQUIVALENCE.md:93–106` mostram paridade estrita possível.
2. **Core:** CTest Native/WASM, goldens, ABI legado, Warp, Reverse, Tap Tempo, MIDI Clock, regressões, controles audíveis e integridade de LUT. Nunca atualizar goldens para tornar falha verde antes da decisão musical.
3. **Feedback:** `scripts/phase64_validate_engine.py` e `scripts/phase64_feedback_stability.py`; `docs/PHASE6_FEEDBACK_STABILITY.md:114–148` estabelece estado finito, estabilidade da cauda, pico e ausência de teto do limiter. Auto-oscilação *limitada* é aceita quando musicalmente aprovada.
4. **Físico:** PlatformIO ESP32-S3 real, 44,1 kHz/128 frames, worst cases multi-head, Reverse FB95, Warp, Freeverb e automação. Medir DSP, param update, full-work, I2S, heap, PSRAM e stack. Exigir zero overruns/falhas. Diferenciar meta de P99 tradicional (70%) da validação específica de C (margens físicas documentadas).
5. **Audível:** Listening Lab com par cego, níveis RAW/MATCHED, 60 s de cauda, ficha por cenário, decisão explícita do responsável e proveniência SHA/compilador/firmware.
6. **Governança:** uma fase por PR, mudanças pequenas, rollback A imediato, resultados anexados. Não realizar merge sem green gates + aprovação de escuta + teste físico quando o hot path mudou.

**Conclusão:** maior retorno musical provável vem do **roteamento independente de feedback**, da **calibração de graves/degradação por repetição** e do **gain staging expressivo**, antes de qualquer novo recurso ou oversampling. O objetivo é tornar o som central excepcional com DSP previsível, não multiplicar features. A sequência executável e as obrigações dos agentes estão detalhadas em [DSP_REFORM_IMPLEMENTATION_PHASES.md](DSP_REFORM_IMPLEMENTATION_PHASES.md).
