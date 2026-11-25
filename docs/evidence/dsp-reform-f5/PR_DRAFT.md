# F5 — Draft PR

**Título:** `experiment(F5): musical capstan glide and clock-focused timing (Vintage F3-A preserved)`

**Base:** `experiment/dsp-reform-f3-saturation` @ `29f5025` (PR #68).  
**Head:** `experiment/dsp-reform-f5-musical-timing` (HEAD registrado em `build.json`).  
**Não integrar automaticamente.** Não altera a PR #69 (F4). A F4 não está incluída nesta
branch; `HYDRA_DSP_REFORM_F4_TRANSITIONS` não é definido aqui e permanece 0 no seu escopo.

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

## Evidência principal

| Verificação | Resultado |
|---|---|
| F5=0 vs manifesto F3-A | 10/10 bit-exact |
| F5=1/2 vs F5=0 em cenas sem mudança de tempo | 10/10 + controles bit-exact |
| CTest Native | 23/23 PASS |
| Native vs WASM (comparador estrito) | max_abs 0 / RMSE 0 |
| F5 v0/v1/v2 no WASM (Node) | 3/3 PASS |
| Blocos 37/64/128/256 vs 128 | max_abs 0 nas três variantes |
| Drift de transporte em 60 s | ≤1e-9 beat |
| Firmware ESP32-S3 builds | 3/3 SUCCESS (a/b/c), +980/+1.136 B flash |
| Hardware físico | `PENDING_PHYSICAL` |
| Pacote de escuta | `artifacts/dsp-reform-f5/listening/` (11 cenas, 66 WAVs) |

Números completos: `docs/evidence/dsp-reform-f5/MEASUREMENT_TABLES.md`,
`TIMING_MEASUREMENTS.json`, `DRIFT_*.json`, `LONG_TAIL_CHECK.json`,
`REVERSE_VALIDATION.json`, `BLOCK_SIZE_CONTRACT.json`,
`INSTRUMENTATION_EQUIVALENCE.json`.

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

## Riscos

- F5=2 é deliberadamente mais lenta que B em mudanças grandes; a limitação de leitor único
  está documentada e é candidata a F7 somente se a escuta justificar.
- O custo físico não foi medido; não promover com benchmark desktop.
- A publicação do artifact depende do workflow `dsp-reform-f5.yml`; o pacote local está
  verificado por SHA-256, mas o link público só existe após a CI.

## Decisão

`READY_FOR_HUMAN_LISTENING` condicionado a: CI publicar o artifact, escuta humana A/B/C e
`PENDING_PHYSICAL` explicitamente aceito como pendência. Nenhuma promoção automática.
