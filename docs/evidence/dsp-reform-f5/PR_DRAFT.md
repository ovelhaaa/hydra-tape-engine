# F5 — Draft PR

**Título:** `experiment(F5): musical capstan glide and clock-focused timing`

**PR:** [#70](https://github.com/ovelhaaa/hydra-tape-engine/pull/70) (Draft)
**Base:** `experiment/dsp-reform-f3-saturation` @ `29f5025` (PR #68).
**Head:** `experiment/dsp-reform-f5-musical-timing`.
**Não integrar automaticamente.** Não altera a PR #69 (F4). A F4 não está incluída
nesta branch; `HYDRA_DSP_REFORM_F4_TRANSITIONS` não é definido aqui e permanece 0
no seu escopo.

## O que muda

- Flag nova `HYDRA_DSP_REFORM_F5_TIMING_VARIANT=0|1|2`, default 0.
- F5=0 continua bit-exact com a Vintage F3-A (10/10 cenários F0.2).
- F5=1 (Capstan Glide): um estado de glide em amostras/beat move os três cabeçotes
  musicais com um teto de velocidade; o `smoothedDelaySamples` existente recebe o mesmo
  teto no modo ms. Razões dos cabeçotes preservadas; o GTC não é suavizado.
- F5=2 (Clock-Focused): a mesma máquina com política por fonte. Tap/MIDI pequenos usam
  tau 30 ms e teto de 5%; mudanças grandes usam tau 150 ms e teto 8,5%; Internal/ms usam a
  política de capstan. Um hold de 0,5 amostra evita perseguir jitter residual.
- O alvo musical passa a acompanhar o GTC também em `transport_set_bpm` sem commit,
  corrigindo o caso em que o delay ficava permanentemente fora do tempo.
- Hook de diagnóstico read-only (`HYDRA_DSP_TEST_HOOKS`), comprovadamente sem efeito no
  áudio; três alvos CTest e três envs PlatformIO de build.

## F5.1 — limpeza e arquitetura de CI

- `build-web-legacy/` e `build-web-test/` (77 arquivos, 27.939 linhas de cache CMake,
  objetos, `.wasm` de 79 KB e de 672 KB) saíram do versionamento; continuam no disco
  local. `.gitignore` recebeu entradas específicas, sem regra genérica que esconda
  fontes, hosts, testes ou fixtures.
- `scripts/create_phase6_pr.py` saiu do diff e permanece fora do Git.
- A CI deixou de exportar `Classic C=1/F2=2/F3=0` como `CMAKE_CXX_FLAGS` globais.
  Regressão de produção roda na configuração original; a qualificação F5 usa
  definições explícitas por alvo, iguais em Native e WASM.
- Causa raiz reproduzida localmente: `core_regression` passa na configuração de produção
  e falha em `regression_core.cpp:255` com os três defines globais. Nenhum teste foi
  relaxado.
- `f5bench.build` passou a usar `-fPIC` fora do Windows, corrigindo o link da biblioteca
  compartilhada no Linux.

## Evidência principal

| Verificação | Resultado |
|---|---|
| F5=0 vs manifesto F3-A (CI Linux) | 10/10 bit-exact |
| F5=1/2 vs F5=0 em cenas sem mudança de tempo | 10/10 + controles bit-exact |
| CTest de produção Native (sem flags F5) | 20/20 PASS |
| CTest de produção WASM | 20/20 PASS |
| Contratos F5 A/B/C | 3/3 Native e 3/3 WASM |
| Paridade Native/WASM por variante | max_abs 0 / RMSE 0 nas três |
| Blocos 37/64/128/256 vs 128 | max_abs 0 nas três variantes |
| Drift de transporte em 60 s | ≤1,42e-14 beat |
| Caudas de 60 s, FB95 | finitas, 0 dB no último segundo |
| Reverse V2, 9 divisões | effective == requested, clamped=0 |
| Firmware ESP32-S3 builds | 3/3 SUCCESS (a/b/c), não é validação física |
| Hardware físico | `PENDING_PHYSICAL` |
| Pacote de escuta | 11 cenas, 66 WAVs RAW+MATCHED, `sha256.json` verificado |

Números completos: `MEASUREMENT_TABLES.md`, `TIMING_MEASUREMENTS.json`, `DRIFT_*.json`,
`LONG_TAIL_CHECK.json`, `REVERSE_VALIDATION.json`, `BLOCK_SIZE_CONTRACT.json`,
`INSTRUMENTATION_EQUIVALENCE.json`, `NATIVE_WASM_VALIDATION.md`.

## Achados

1. F5=0 tem duas semânticas de tempo: modo ms com one-pole (~200 ms) e modo musical sem
   smoothing. Em 120→100 o cabeçote 3 salta 4.410 amostras (100 ms).
2. Uma mudança só de transporte (o caminho de MIDI/UI web) deixa os cabeçotes no BPM
   antigo até o próximo commit: 882 amostras (20 ms) de erro permanente em 120→125.
3. O one-pole do modo ms aplica ~1.200 cents no primeiro instante de 500→600 ms e trava
   ~8,6 amostras antes do alvo por resolução de float.
4. Reverse V2 já reconfigura pelo BPM exato do GTC; não foi alterado e permanece alinhado.

## O que não muda

ABI 1.7/38, IDs, presets, goldens, LUT/bias, saturação F3, feedback, topologia/pesos dos
cabeçotes, Reverse, Warp, Freeze, Ping-Pong, Multi-Head, Mix/Drive/Tone, interface.
Nenhum seletor público de modo temporal. Nenhum leitor extra ou buffer novo.

## Publicação

**Publicado e verificado:** [artifact 14985866516 — `hydra-dsp-reform-f5-listening`](https://github.com/ovelhaaa/hydra-tape-engine/actions/runs/45532866456/artifacts/14985866516),
na execução [45532866456](https://github.com/ovelhaaa/hydra-tape-engine/actions/runs/45532866456),
head `a8bc9c5`. ZIP de 117.591.429 bytes, SHA-256
`a7115ef0c54a5d62798a217b237086b22bf8409631baf23f15d796caf2153feb`. Após o
download, 82/82 hashes conferem, 66 WAVs e 11 cenas confirmados; os 66 WAVs foram
reproduzidos bit-exact numa reconstrução local.

Artifacts anteriores classificados em `PUBLICATION_STATUS.md`: `14985175520`
(superseded, cena 02 sem semente) e `14982026827` (inválido, 97.286 bytes sem
WAVs, da execução 45524614234 que falhou antes do pacote existir).

## Riscos

- F5=2 é deliberadamente mais lenta que B em mudanças grandes; a limitação de leitor único
  está documentada e é candidata a F7 somente se a escuta justificar.
- O custo físico não foi medido; não promover com benchmark desktop.
- O artifact expira em 30 dias; `run.py`/`f5bench.py` permitem regerar o pacote.

## Decisão

`READY_FOR_HUMAN_LISTENING` condicionado a: artifact publicado e verificado, escuta humana
A/B/C e `PENDING_PHYSICAL` explicitamente aceito como pendência. Nenhuma promoção
automática, merge, rebase ou force push.
