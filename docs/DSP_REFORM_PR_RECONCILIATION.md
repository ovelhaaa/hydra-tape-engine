# F0.1 — Reconciliação arquitetural das PRs #58–#62

2026-11-23. Auditoria documental sobre `main@6d94af5`; nenhuma promoção,
mudança DSP ou autorização de próxima fase. SHAs completos, bases, checks e
discussões: [snapshot](evidence/dsp-reform-f0/branch-snapshot.json).
Arquivos e linhas de **todos os diffs reais**, relativos à base de cada PR:
[inventário](evidence/dsp-reform-f0/changed-lines.md). Números abaixo são da
HEAD da PR indicada; referências A pertencem à main. Links por SHA no inventário
permitem consultar versões que ainda não existem na main.

## Estado e matriz de integração

| PR / HEAD / base | Implementação e hot path | Estados / memória | ABI / Native-WASM | Testes e aprovação | Situação / conflito com roteiro |
|---|---|---|---|---|---|
| [#58](https://github.com/ovelhaaa/hydra-tape-engine/pull/58) `d2d36dd` / main | Seleciona C já implementada por macro no firmware e target web real; rollback A separado. Não introduz outro algoritmo em tape_core. Hot path muda de A para C **nos targets selecionados**. | Nenhum estado novo próprio; habilita os filtros privados C existentes. | 1.7.0 / 38; runners históricos/goldens ainda A, demo WASM C. Paridade dos runners A não valida por si só a demo C. | Checks atuais successful; C musicalmente aceita historicamente. Firmware padrão/rollback atuais ainda precisam qualificação própria. | EXPERIMENTAL; não integrada. F1 não deve recriar independência já atendida por C. |
| [#59](https://github.com/ovelhaaa/hydra-tape-engine/pull/59) `20b3b26` / #58 | C1 spectral wear, C2 transient softening, C3 combinação 70%+70%, após playback C e antes eletrônica feedback; macro opt-in. | Dois RepeatAgingChannelState (12 floats/canal = 96 B) + coeffs (12 floats = 48 B), aprox. 144 B, sem delay longo. Estados atualizados mesmo em blend neutro. | 1.7.0 / 38; C0 sem custo/estados adicionais quando macro=0. | Job atual: 11/11 subset + C0 bit-exact em 10 cenários contra #58 `e815117`; Native/WASM CI verde. C1/C2/C3 aceitáveis na escuta, muito semelhantes; preservar C0. Sem físico novo. | EXPERIMENTAL / PENDING_PHYSICAL; F8 parcialmente atendida, não incorporar silenciosamente à #61. |
| [#60](https://github.com/ovelhaaa/hydra-tape-engine/pull/60) `c1331a7` / #58 | Motion M1 drift/inércia, M2 capstan/flutter, M3 eventos/tensão, builds separados; referência para #61. | Coeficientes/PRNGs/estados float fixos, sem novo buffer longo; custo por modo, não soma de motores Tape. | 1.7.0 / 38; M0 comparação congelada; API WASM reparada. | Checks atuais successful; M1–M3 aprovados como opções, sem ranking sobre M0. Histórico COM15 com flags não verificadas; rerun não executado após fix. | EXPERIMENTAL; referência de F9, não baseline física qualificada. |
| [#61](https://github.com/ovelhaaa/hydra-tape-engine/pull/61) `c1f996b` / #58 | Motor único M0–M3 em runtime, fade de deslocamento 150 ms, slew de Amount/Stereo 30 ms; UI, telemetry, fila de parâmetros ESP32. Hot path e param-update mudam. | TapeMotionCoeffs + TapeMotionState fixos, sem buffer Tape/Reverse/Warp extra; transição computa união dos modos necessários, sem avançar M1/M2 duas vezes. | **1.8.0 / 47**, IDs 38–46, nova função telemetry, estruturas C legadas preservadas. | Checks atuais successful, job unified_motion_lab auditado; M0 exact e M1–M3 parity nos cenários definidos. Não constitui sign-off auditivo do motor unificado. Físico incompletamente rastreado (abaixo). | EXPERIMENTAL; depende #58, **não contém #59**, usa #60 como fonte. F9 movimento já implementado, F5/F7 tempo não resolvidas por esse fade. |
| [#62](https://github.com/ovelhaaa/hydra-tape-engine/pull/62) `565ae35` / #61 | Studio dry unidade após wet/aux, Wet Level 0–200% fora feedback, sem texture tap no monitor; morph ~30 ms. Filtros Studio também rodam em Classic. | Seis biquads + dois one-poles + dois allpasses, quatro floats de controle; ChannelFrame temporário + float; nenhum Spring/Smear/tape buffer extra. | **1.9.0 / 49**, IDs 47–48; legado 0–46 mantido; preset v2 extensível, imports antigos Classic/100%. | Checks atuais successful; relatório 22/22 Native e WASM, 32 Classic scenes idênticas à #61, dry-muted 132 casos erro numérico zero e dez renders Native/WASM. Sem sign-off humano Studio nem CPU física. | EXPERIMENTAL; depende #61. Atende parte F4 sem substituir Classic ou resolver ergonomia inteira. |

Todas as PRs estão abertas/draft e **nenhuma integrada** à main. Termos
“production default” em documentos das branches #58–#62 descrevem a configuração
dessas branches, não a produção aprovada da main. Checks foram consultados nos
SHAs atuais; `deploy` skipped é esperado, não falha nem publicação.

Pacotes atuais localizados pela API (IDs/digests/expiração no
[manifesto de artefatos](evidence/dsp-reform-f0/artifact-snapshot.json)):
[#58 A/B/C](https://github.com/ovelhaaa/hydra-tape-engine/actions/runs/45092867386/artifacts/14782575290),
[#59 C0–C3](https://github.com/ovelhaaa/hydra-tape-engine/actions/runs/45093042484/artifacts/14782835106),
[#60 Motion](https://github.com/ovelhaaa/hydra-tape-engine/actions/runs/45105796732/artifacts/14786998341),
[#61 Unified](https://github.com/ovelhaaa/hydra-tape-engine/actions/runs/45257834877/artifacts/14859537980),
[#62 Studio](https://github.com/ovelhaaa/hydra-tape-engine/actions/runs/45263172793/artifacts/14861902617).
Identificação/metadata foram conferidos; os ZIPs não foram baixados e re-hasheados
nesta rodada. Esses pacotes expiram; não são arquivo permanente da baseline F0.

## A, B, C e C0: não confundir topologias com revisões

- **A/main:** `tape_core.cpp:1149–1177`: monitor `textureSig*textureGain +
  delaySig*delayGain`, dropout/azimute/hiss; feedback deriva desse monitor e faz
  outro blend Reverse. `src/main.cpp:274–276` liga p_mix a dryWet e delayWet.
- **B:** `tape_core.cpp:1170`: delaySig antes dos ganhos de monitor, já com
  primeiro blend Reverse; não aplica a degradação de playback C. Independência
  barata não comprova equivalência sonora à C.
- **C:** `tape_core.cpp:1165,1239–1265`: delaySig com dropout, azimute, shelf,
  bump, gap privados e 0.20*hiss antes da eletrônica de feedback original
  (`1191–1230`). Estados privados evitam avançar duas vezes filtros do monitor.
- **C0:** nome da referência congelada **C sem Repeat Aging**, não uma quarta
  rota. #59 usa comparação `e815117` e #60 usa `d2d36dd`; esses SHAs não são
  intercambiáveis como identidade de build, embora #58 não altere o algoritmo C.
  Registrar sempre a referência e as flags usadas no comparador.

C já atende à independência dos ganhos de monitoração e remove a necessidade
de recriar uma rota degradada independente na F1. Continuam abertos promoção,
rollback, medição física comparável da HEAD e eventual redução de custo que
preserve preferência auditiva. Uma C enxuta só se justifica por **economia física
medida mantendo o caráter C**, não por suposta carência da independência.
Não compartilhar estados de playback nem substituir TapeCore pelo TapeModel legado.

## Repeat Aging: alcance real da F8

`repeat_aging_experiment.hpp:152–211` usa envelopes de amplitude/HF e diferença
fast/slow para modular LPFs e allpass, com blend `x+amount*(wet-x)`.
`tape_core.cpp:1294–1321` seleciona C1, C2 ou C3. Coeficientes exponenciais são
calculados fora do loop de amostras. O custo estimado pelo relatório da #59 é
C1 20–38, C2 27–48, C3 47–86 µs/128 frames no S3; **não medido** e não cumulável
com folgas de outra toolchain como prova de deadline.

Não é preciso criar de novo um modelo de perda dinâmica de HF para satisfazer
F8(b). Falta demonstrar benefício perceptivo discriminável e custo físico.
Pré/pós-ênfase espectral real F8(a) não é implementada por esse envelope, nem
pelos ganhos escalares da eletrônica existente. C3 não recebe preferência
automática: decisão anterior preserva C0 quando a diferença foi pequena.

## Unified Motion: o que a evidência demonstra

Probe de `sizeof` em MinGW GCC 14.2.0, incluindo os headers dos SHAs auditados:
#60 OrganicMotionCoeffs=152 B, OrganicMotionState=112 B (264 B);
#61 TapeMotionCoeffs=192 B, TapeMotionState=160 B (352 B). São tamanhos host
dos objetos adicionados, não delta total de Impl, heap/stack ESP32 ou custo CPU.
Reproduzir incluindo os dois headers e imprimindo `sizeof` dos quatro tipos
(`hydra::dsp::phase74b` para Organic e `hydra::dsp` para TapeMotion).
[Resultado](evidence/dsp-reform-f0/f01-size-probe.txt).

`tape_core.cpp:365–461` mantém mechanicalMod original para M0 nominal e usa
TapeMotionState para os demais modos. Transição mistura **deslocamentos de
leitura**, com smoothstep; um retarget preserva o último deslocamento por hold
offset. Buffers, writeHead, feedback e relógios não são reinicializados. Isso
não é crossfade de dois motores de áudio, nem resolve saltos de BPM/cabeçote.

`tape_motion_engine.hpp:73–267` calcula coeffs em update; `270–642` contém
estados, PRNGs e passos limitados. Não foram encontrados new/malloc, bloqueios,
I/O ou chamadas exp/pow/trig de libm **por amostra** nas adições. fastSin é
polinomial com wrap de fase; entradas/frequências são limitadas. Atenção:
`coeffs.update()` usa exp/pow/sqrt na atualização de parâmetros. Na #61,
`hydra_dsp_esp32_adapter.cpp:221–224` drena SetParameter e faz commit; portanto
custo de atualização na audio task precisa medição sob rajadas, mesmo sem
alocação no processamento por amostra. Não declarar atualização gratuita.

Evidências da HEAD: [job do motor](https://github.com/ovelhaaa/hydra-tape-engine/actions/runs/45257834877/job/137984223449),
comparador `scripts/verify_phase74c_equivalence.py` e
`tests/core/phase74c_unified_motion_test.cpp`. Logs CI mostram M0 max_abs=0
nos oito cenários definidos e parity M1/M2/M3 exact nominal Amount=100,
Stereo=100, advanced=50, contra referências explicitamente compiladas. Não
estender bit-exact a outros controles ou a uma história que alternou modos.
Native/WASM CI cobre o core comum; não equivale a demonstrar toda combinação
do espaço experimental em hardware. Testes de stress cobrem FB85/95,
Reverse/Multi-Head/Spring e retarget rápido; Warp preserva o mesmo buffer e
leitores e aparece na comparação M0, sem alegação de matriz física completa.

StereoMotion=0 elimina a componente diferencial do **deslocamento** e o teste
de tom verifica L/R em trecho wet; isso não garante saída mono para entradas
estéreo, hiss/dropout independentes ou todos os efeitos. Continuar escuta mono,
M/S e correlação. M1/M2/M3 foram aceitos nos builds #60; o unificado e suas
transições ainda requerem decisão humana própria.

UI mapeia IDs 38–46 a engine, modo por MessagePort e contínuos por AudioParam,
telemetria vem do core. Testes Node simulados não são navegador real; CI inclui
Playwright e WASM real, reset foi estabilizado em `c1f996b`.
Preset schema **continua v2**, não surgiu “preset v3”: V1 migra e V2 esparso
completa defaults M0/100/100/50. Não confundir versão API com versão preset.
A afirmação antiga “34 Parameters Mapped” não foi encontrada na HEAD HTML
auditada; pendências no corpo da PR podem estar desatualizadas.

### Auditoria da medição COM15 da #61

Os arquivos existem **localmente**, apesar de não estarem versionados na PR:
`artifacts/phase7-4c/physical/benchmark_results.json` e `serial_log.txt`.
Cópias intactas preservadas nesta PR em
[physical-61](evidence/dsp-reform-f0/physical-61). SHA de firmware **declarado**:
`59cbb93fce0bd6482217f777e58c59c1c2d4d3a5e9146360b108c1b8efeae9ea`.
O binário em `.pio/build/esp32-s3-phase74c-unified/firmware.bin` não está
disponível neste checkout; não foi possível recalcular esse hash.

| Registro atribuído | P99 / peak DSP (µs) | Limite da conclusão |
|---|---|---|
| M0 single / multi+spring / reverse+spring | 1614/1829; 1844/2007; 1858/2067 | 1024 blocos/cenário, não teste sustentado |
| M1 mesmos cenários | 1641/1821; 1881/2135; 1902/2099 | mean abaixo de P50 em alguns casos não prova ganho geral de CPU |
| M2 mesmos cenários | 1763/1955; 1949/2207; 1946/2226 | comparação atribuída ao mesmo ensaio, sem flags verificadas |
| M3 mesmos cenários | 1834/2063; 2129/2340; 2129/2331 | M3 P99 multi=73.35% do deadline; não confundir mean 69.52% com P99 |
| Transições+auto declaradas | 1953/2239 | captura não prova que automação ocorreu dentro da janela |

Há zero overruns/I2S nos registros, heap mínimo 116420 B, PSRAM final 1662792 B,
MaxParamUpdUs até 387; serial registra StackMinWords=27268 (conservar unidade
do rótulo; não converter a bytes sem auditar implementação). Não há fullWorkUs,
flags efetivas/verbose build, ELF/source identity da placa, nem associação
imutável entre código atual e binário medido. O script atual calcula hash de
arquivo **local**, não confirma imagem em execução via `identity`.

O serial foi capturado antes dos contadores atuais: faltam `AutoScheduled` e
`AutoApplied` que `bench_phase74c_esp32.py:109–117` hoje exige. O JSON também não
os contém. Além disso, comandos wwd/ftd solicitam valores diferentes daqueles
que aparecem no `Cfg` (p.ex. pedido 25/25, snapshot 9/11); isso exige reconciliar
host smoothing/semântica/snapshot antes de comparar o cenário declarado.
Não é prova automática de medição falsa: são lacunas de rastreabilidade.

Conclusão: **evidência física parcial, insuficiente para aprovação definitiva**.
O checklist físico já está marcado na HEAD de LISTENING_ACCEPTANCE, ao contrário
do corpo antigo da PR; essa marca não supre os itens ausentes. Novo sign-off
requer binário/ELF+hash, comandos efetivos, identity correspondente, flags
arquivadas, cenários/automação confirmados, full-work e duração sustentada.
**PENDING_PHYSICAL**; sem flash autorizado nesta fase.

## Studio: custo permanente em Classic e continuidade

Confirmação obrigatória: **sim**, `tape_core.cpp:1337–1346` na #62 processa
Studio por canal **antes de qualquer teste de pathBlend**. Faz sempre três
biquads/canal; dropout one-pole e azimuth allpass são condicionais ao efeito,
não ao modo de monitor. Atualiza filtros privados com os mesmos coeficientes
do playback (`706–716`) e faz flush por bloco. Em Classic não mistura sua
saída, mas paga o trabalho. O bloco Classic original permanece avaliado em
Studio também. São três cadeias de playback na build C+Studio (Classic monitor,
C feedback, Studio monitor), embora só um buffer Tape e uma cadeia auxiliar.

Motivo: preparar memória e interpolação de coeficientes para entrar em Studio
imediatamente, sem reset/cold start, e preservar o Classic congelado.
Custo estrutural mínimo: **6 avaliações biquad por frame estéreo = 768 por
bloco 128**, mais ramos, hiss, controle e estados condicionais. Operações por
biquad variam com a interpolação de coeficientes; contagem não é ciclo S3.
Memória permanente exata deve ser medida com sizeof/map da toolchain, incluindo
coeficientes de interpolação, não somente dois delays de cada filtro.

Medições já publicadas: Native 0.0838/0.0883 ms Classic/Studio local (~5.3%),
CI 0.019866/0.020239 ms (~1.9%) em outra máquina; WASM também no artifact.
**Ambos os modos da #62 já incluem o custo dos filtros quentes**: esses ratios
medem apenas a diferença de modo dentro da #62, **não** o overhead #62 Classic
contra #61 Classic. Nenhum desses números prevê µs no ESP32. Falta esse A/B
físico e medição de full-work por modo/transição/automação. Tamanhos estáticos
de firmware compilado não são heap/stack runtime.

Possíveis reduções futuras, não implementadas: (a) adiar ativação e aquecer
antes do fade, aceitando latência e validando caudas; (b) reutilizar **saída e
estados de uma cadeia cujo sinal seja matematicamente idêntico**, nunca
processar duas entradas no mesmo estado; (c) especializar caminho quando efeitos
estão neutros, provando paridade e transições. C feedback recebe outro ganho e
0.20*hiss; Classic contém texture tap; portanto não são substitutos triviais
da memória Studio. Copiar coeficientes é seguro; compartilhar estados distintos
causaria contaminação, alteração de feedback e regressão Classic.

Studio endpoint em `1439–1490`: morph de entrada na única cadeia Spring/Smear,
trim externo, soma dry unidade depois dos auxiliares e guard [-1,1]. Não é
crossfade exato de duas cadeias completas não lineares; histórico auxiliar
recebe excitação intermediária. Retornar Classic pode conservar caudas dessa
excitação: teste de finitude não prova equivalência musical dessas caudas.
Não há reset de TapeMotion, writeHead ou feedback ao trocar monitor.

132 calibrações dry-muted 44.1/48 kHz: erro **numérico** zero nos níveis medidos,
não garantia de signed-zero bit-exact. Wet Level depois do aux não altera o
loop; delayWet histórico ainda controla playback. Texture tap não entra no
Studio monitor; continua existindo no Classic. Presets anteriores restauram
Classic/100%, inclusive presets de firmware via handoff.

Headroom: 25/132 casos de wet pleno provocaram guard em sobrecarga deliberada;
os dez cenários moderados não atingiram full scale. True peak ~1.2144 em 4x
(~+1.69 dBFS): clamp de amostra não garante headroom intersample. Hard clipping
pode ser audível sob sobrecarga; esses números não demonstram por si só clipping
audível em uso nominal nem transparência universal. Falta escuta dos casos de
pico e caudas, e decisão de política de ganho; não substituir dry unidade por
normalização silenciosa. [Job Studio atual](https://github.com/ovelhaaa/hydra-tape-engine/actions/runs/45263172793/job/138003194574).

## ABI e compatibilidade por branch

| Branch | Versão / count | IDs novos | Legado |
|---|---|---|---|
| main e F0 | 1.7.0 / 38 | nenhum | 0–37 produção |
| #58, #59, #60 | 1.7.0 / 38 | nenhum | mesmos headers públicos |
| #61 | 1.8.0 / 47 | 38 mode; 39 amount; 40 stereo; 41 inertia; 42 drift; 43 texture; 44 irregularity; 45 instability; 46 tension recovery | 0–37 preservados; telemetry aditiva |
| #62 | 1.9.0 / 49 | 47 signalPathMode; 48 studioWetLevel | 0–46 preservados |

`include/hydra_dsp.h:14–26,159–199` main; #61 `202–216,311–323`; #62
`202–221`. Nenhuma renumeração/reuso encontrado. Estruturas C v1_2/v1_3/v1_4
não crescem; TapeParams interno cresce. Consumidor legado/símbolos e testes
web de IDs continuam gates CI. Novos packagers aceitam minor aditiva mantendo
major; isso não prova compatibilidade de um host que exige count==38.
Antes de integrar, revisar todos esses consumidores e manter contratos por
target (golden A vs web C). A API de produção **não** passou a 1.9 nesta F0.1.

## Recomendação priorizada: benefício por ciclo

Escalas qualitativas são **hipóteses**, não resultados de nova escuta. Todas
dependem de autorização própria, baseline recuperada e rota explicitamente
congelada. A recomendação não promove C/#58 nem contorna F1.

| Ordem / candidato | Impacto provável e evidência | CPU / RAM incrementais | Risco / complexidade / A-B |
|---|---|---|---|
| **1 — B: calibrar HPF/bump/gap do loop** | Alto potencial de corpo/clareza; HPF 300 Hz e bump 60–200 Hz já coexistem; ainda falta medir cancelamento/ganho por repetição, sem alegar defeito auditivamente provado. | Aproximadamente zero por amostra / zero buffer, se apenas coeficientes existentes. | Médio (ringing/energia); baixa-média complexidade; A/B fácil com mesma topologia e loudness. |
| **2 — C: progressividade FB/saturação** | Alto potencial de sweet spots 65–95%; plateau estável já demonstrado, C1–C3 não calibram essa curva. | Aproximadamente zero / zero buffer para curvas e trims. | Médio-alto: muda sustain; média; A/B por níveis e cauda 60 s. |
| **3 — D: transições tempo/cabeçotes** | Alto durante performance; política dupla demonstrada no código; Motion fade não cobre tempo BPM. | Baixo com alvo/slew; variável se leitores extras / poucos estados. | Alto para sync/pitch/Reverse; média-alta; A/B gestual claro, custo de PSRAM deve ser medido. |
| **4 — A: feedback independente mais barato que C** | Independência já atendida; benefício novo seria margem CPU mantendo som C, ainda não demonstrado. | Meta negativa CPU / menos estados; nenhum número validado. | Alto de descaracterizar C; média-alta; comparar A/B/C/candidata, não só correlação. CONDITIONAL ao déficit físico medido. |
| **5 — E: otimizar filtros Studio quentes** | Benefício de CPU, sem melhoria sonora comprovada; evitar perder continuidade aprovada futuramente. | Economia possível / estados talvez iguais. | Alto para Classic parity/caudas; média-alta; comparação #61/#62 por flags iguais. CONDITIONAL à adoção Studio. |

Hipótese recomendada: mantendo topologia/ganhos escolhidos e filtros existentes,
calibração conjunta pode aumentar energia 80–250 Hz por repetição sem ringing,
acúmulo ou perda de definição. Primeiro medir, depois uma variante pequena;
comparar RAW e MATCHED com baixo/guitarra grave, bateria, acordes, impulso e
tails 60 s. Registrar energia por bandas/repetição, decay, picos internos,
limiter, estabilidade, Native/WASM, testes de preset/Reverse/Warp. Mesmo custo
teórico zero exige verificação física do comportamento alterado e A/B humano.
Escolher rota C de laboratório somente por decisão explícita, preservando A
produção e compatibilidade #59/#61/#62; não fundir implementações experimentais.

## Decisão e gates restantes

**BLOCKED** para próximo experimento DSP: [consolidação F0](evidence/dsp-reform-f0/CONSOLIDATION.md)
mostra que hashes atuais estão íntegros, mas a regeneração dos dez WAVs não
está especificada. Recuperar receita e demonstrar 10/10 antes de remover esse
bloqueio. Separadamente: físico main atual pendente; #60 flags/rerun; #61
identidade/flags/full-work/automação; #62 overhead Classic-vs-#61 e caudas/escuta.
Nenhum impedimento documental foi transformado em alegação de regressão DSP.
Fases F0–F11 permanecem no roteiro, com objetivos reconciliados e sem duplicar
Repeat Aging, Motion ou independência C. Nova implementação depende de nova ordem.
