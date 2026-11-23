# Hydra DSP Reform — F0.2

**Decisão offline: `READY_FOR_DSP_EXPERIMENT`.**
**Baseline: `NEW_BASELINE_REPRODUCIBLE`, rota A de produção.**
Nenhum algoritmo, ABI, preset, golden ou tolerância mudou. Nenhum merge ou flash.
A autorização recebida para F0.2 inclui expressamente criar referência nova se
a histórica não puder ser recuperada. Isso resolve o gate de receita incompleta;
não autoriza automaticamente F1/F2 nem promove experimentos #58–#62.

## Base e preflight

Partida: checkout original `docs/dsp-reform-f0-baseline@8358cc4`, com arquivos
não rastreados preexistentes preservados. Trabalho isolado na branch
`codex/dsp-reform-f0-2-reproducible-baseline`, worktree `hydra-f02`, derivada da
#64 `3cc6c1b5f4ce8b93d7665744d266a82cb42e80e2`. #63 `6f3bc24` e #64 seguem
abertas/draft, sem merge na consulta desta execução; #64 é a base da nova PR.
#63 **não** foi cherry-picked. Main `6d94af5`; nenhuma F1–F11 incorporada.
Diff de `6849edf` até a base nas pastas DSP/include/src/tests/scripts e arquivos
de build: vazio. As evidências abaixo foram executadas nesta worktree, não #63.

Leitura: AGENTS, auditoria musical, G0/F0 e roteiro F0–F11, reconciliação,
README/CONSOLIDATION/manifesto/probe Python/JSON F0. Caminhos ativos conferidos:
`src/main.cpp:860–954` → adaptador `:138–165` → API C `:330–337` → TapeCore.
`tape_core.cpp:1110–1230` preserva gravação magnética, memória L/R compartilhada,
rota A em `:1174`; C permanece opt-in. Especificações dos parâmetros em
`hydra_dsp_c_api.cpp:23–61`, prepare/reset em `:213–228`; seeds/reset do core em
`tape_core.cpp:202,273,282–286,407,719–754`.
Ferramentas inspecionadas: `render_phase6_review.py` (ctypes/defaults/writer/loop),
`musical_sources.py`, `audio_analysis.py`, CMake, workflow core-equivalence,
testes/runners Native/WASM, relatórios Phase6 de equivalência/estabilidade e
decisão física C 7.2E, README do Listening Lab. Histórico Git de scripts:
`67f4445`, `f964e4b`, `11c1c5d`; não contém o gerador específico dos dez F0.

Hipótese inicial: receita/estado de host diferente; não atribuir automaticamente
a compilador ou mudança DSP. Hipótese musical: nenhuma melhoria de som é
proposta; estabelecer referência mensurável para A/B futuros. Custo incremental
CPU/RAM no ESP32-S3: **zero**. Plano executado: comparar RIFF/PCM, métricas e
prefixos; hipóteses isoladas; nova receita completa; dois processos de render;
gates Native/WASM e estabilidade. Firmware permanece intacto.

## Divergência histórica: resultado e limites

[diagnostic_comparison.json](diagnostic_comparison.json) contém os dez históricos
e sete pares com o probe F0.1. **10/10 hashes históricos íntegros**. Todos são
RIFF/WAVE PCM24 estéreo 44100 Hz, chunks `fmt ` (16 bytes) e `data`; sem chunks
de metadados adicionais. Nos sete pares, tamanho, formato e frames coincidem;
primeiro byte divergente está no payload: offset **44** nos seis musicais e
**1304** no silêncio/hiss (primeiro frame divergente 210). Hashes PCM e float64
canônico diferem. Não é uma diferença apenas de contêiner ou empacotamento.
Quantização de 24 bits não explica erros nessa escala; não foi recuperada a
saída float32 pré-exportação original para isolar o writer histórico.

| Histórico / probe | max_abs bruto | RMSE bruto | Classificação |
|---|---:|---:|---|
| 01 guitarra limpa | 0.638074 | 0.155664 | CAUSE_UNDETERMINED |
| 02 guitarra saturada | 0.868059 | 0.306619 | CAUSE_UNDETERMINED |
| 03 synth mono | 0.629340 | 0.152938 | CAUSE_UNDETERMINED |
| 04 synth poli | 0.346395 | 0.0820884 | CAUSE_UNDETERMINED |
| 05 bateria | 0.466722 | 0.0932084 | CAUSE_UNDETERMINED |
| 06 percussão | 0.503275 | 0.0823679 | CAUSE_UNDETERMINED |
| 07 impulso 10 s | — | — | HISTORICAL_RECIPE_UNKNOWN |
| 08 impulso 20 s | — | — | HISTORICAL_RECIPE_UNKNOWN |
| 09 impulso 60 s | — | — | HISTORICAL_RECIPE_UNKNOWN |
| 10 silêncio/hiss | 0.00387037 | 0.000868220 | CAUSE_UNDETERMINED |

O JSON registra hashes completos/PCM, estrutura/chunks, frames, primeiro offset,
primeira amostra/frame, energia da diferença, dBFS, ganho, correlação, lag,
prefixos 128/512/4096, métricas brutas e compensadas. A correlação não foi usada
como prova de equivalência. Nenhum dos sete pares satisfaz os limites existentes.

[differential_results.json](differential_results.json): DLL histórica idêntica,
guitarra limpa como caso representativo. Controle reproduz exatamente o hash do
probe F0.1. Commit explícito antes do processamento e bloco 64 em vez de 128
mantêm esse hash; reset explícito muda o hash mas não recupera F0. Cada teste
muda uma hipótese, sem busca combinatória. A hipótese única de impulso estéreo
0.5/frame0 não recupera nenhum dos três hashes históricos de input.

**Causa exata indeterminada**: os 38 valores históricos, inicialização completa,
possível preroll, ordem/reuso do motor e writer específico não foram preservados.
O runner citado cria um motor por cenário e usa somente um mapa parcial; os
valores restantes vêm da criação. A API é dirty/commit; process e consultas de
diagnóstico podem aplicar parâmetros. O reset não é idêntico a simplesmente
criar o handle (reinicializa, entre outros, seeds independentes de dropout).
Isso é evidência de sensibilidade ao ciclo de vida, sem provar a receita F0.
Não alegar diferença real de algoritmo nem inventar os parâmetros ausentes.
A investigação é encerrada de forma limitada em favor da solução 3 autorizada.

## Nova referência válida

[baseline_manifest.json](baseline_manifest.json), [baseline.py](baseline.py) e
[REPRODUCTION_PROTOCOL.md](REPRODUCTION_PROTOCOL.md) definem fonte/binário,
todos os 38 IDs, readback, seeds, lifecycle, bloco, formato e comandos.
Dois processos independentes no mesmo ambiente: **10/10 input, output float32,
PCM24 e WAV bit-exact**, incluindo três novos impulsos explicitamente definidos.
Os sete inputs musicais/silêncio mantêm hashes F0; não há pretensão de recuperar
os outputs antigos. O manifest congela cada hash individual, sem sobrescrever F0.
ID29=0 significa não disparar comando; ele não é um parâmetro de estado.

Fonte congelada: `3cc6c1b5f4ce8b93d7665744d266a82cb42e80e2`.
DLL nova: `8f6836c77368a3554a2a6c50ec681aa58d8e09b09758847d4d85f596d91724dc`.
O hash da DLL nova é diferente da histórica; nenhuma comparação causal se baseia
nessa igualdade. Os testes diferenciais históricos usam a DLL antiga.
O hash da receita executada está no manifesto; [validation.json](validation.json)
registra as duas verificações e o teste negativo de rejeição de corrupção.

## Testes desta execução

Builds novos Native MinGW Release e WASM Emscripten 3.1.62, sem flags B/C no
target A, `-ffp-contract=off`. Testes realizados sobre código da base `3cc6c1b`
mais somente scripts/documentos F0.2; a PR registra o commit da entrega.

| Gate | Resultado / evidência |
|---|---|
| CTest Native completo, ambiente padrão | **19/19 PASS**, [log](ctest-default.log) |
| Goldens Native | **3/3 bit-exact**, max_abs/RMSE/energia=0, [log](goldens.log) |
| ABI/símbolos/consumidor legado/API IDs | PASS no CTest; API 1.7/38 preservada |
| LUT, Reverse/Warp, Transport, Tap Tempo/MIDI | PASS no CTest |
| Goldens WASM | **3/3 PASS**, [log](ctest-wasm.log) |
| Native/WASM, cinco cenários existentes | **bit-exact**, max_abs=0, RMSE=0, [JSON](equivalence.json) |
| Input/LUT Native/WASM | **4096/1025 bit-exact, 0 ULP**, [JSON](input-lut-equivalence.json) |
| Full engine 44.1/48 kHz, tails 60 s | **27/27 PASS**, [JSON](engine-gates.json) |
| Feedback 60 s, seis casos | limited sustain ou decay, zero teto limiter, [JSON](feedback-stability.json) |
| Baseline nova | **duas execuções × 10 cenários bit-exact**, [validação](validation.json) |
| Hardware / escuta humana | não executados; não inferidos dos testes offline |

O diagnóstico `phase64_feedback_stability.py` ainda imprime `gate=FAIL` em quatro
casos para a regra **histórica** `last <= third` de 6 s. Isso foi preservado no
JSON (`gate_last_le_third=false`), sem ocultar falha: sua classificação estendida
é limited sustain, slope aproximadamente +0.00001…+0.00005 dB/s. A aceitação
vigente é o gate `tail_stable` de 60 s já versionado em
`phase64_validate_engine.py`, documentado em `PHASE6_FEEDBACK_STABILITY.md:114`;
27/27 passam. Nenhuma regra ou tolerância foi alterada na F0.2.

Comparação de infraestrutura: F0 histórico 14/19; F0.1 padrão 15/19; F0.1 com
TEMP no build 19/19; #63 separada 19/19 e Tap 30/30 (resultados históricos).
**Nesta HEAD sem #63: 19/19 padrão**, sem mudar TEMP/TMP. Isso não elimina a
corrida conhecida do teste de snapshots nem prova resolução das falhas antigas.

## Integridade, distribuição e decisão humana

- **A — Integridade:** F0 10/10 preservados; manifesto histórico inalterado.
- **B — Reprodutibilidade:** nova F0.2 10/10 × duas execuções verificadas;
  histórica não recuperada. Rebuild pode alterar identidade PE; procedimento
  distingue identidade do binário e exigência de hashes de áudio idênticos.
- **C — Equivalência:** Native/WASM cinco cenários existentes bit-exact;
  não extrapolar para toda combinação possível de parâmetros.
- **D — Musical:** PENDING_HUMAN; baseline técnica não significa preferência
  ou aprovação de uma mudança de som. Nenhuma melhoria proposta nesta fase.

[archive_manifest.json](archive_manifest.json) identifica pacotes locais F0 e
F0.2. WAVs/DLL/float32 não entram no Git. Não há URL pública disponível; publicar
os ZIPs externamente é pendência de distribuição, com hashes já registrados.
A receita F0.2 permite recriação sem depender de artifact expirável.

**READY_FOR_DSP_EXPERIMENT** satisfaz o gate offline autorizado da F0.2.
Próxima fase autorizável: investigação/calibração conjunta de HPF/head bump/gap
loss com filtros existentes, após decisão explícita da rota F1. Referência
disponível agora é A; C/C0 exigem congelar receita/flags próprias por decisão
explícita, sem promover C automaticamente. Validação física e A/B humano seguem
obrigatórios antes de promover qualquer mudança do hot path ao firmware.
Nenhuma autorização de implementação F1/F2 é presumida.

## Handoff

FASE F0.2; entrega técnica disponível, aprovação humana da PR PENDING.
Mudanças restritas a reprodução/documentação, custo firmware zero; rollback é
desconsiderar a referência nova, mantendo F0 intacta. Fases F1–F11 permanecem
não incorporadas. Consultar PR/commits da entrega e reproduzir pelo protocolo.
Riscos residuais: receita histórica desconhecida, distribuição externa pendente,
dependência de ambiente fixado, corrida de teste conhecida sem #63, ausência de
escuta/qualificação física de candidatos futuros.
