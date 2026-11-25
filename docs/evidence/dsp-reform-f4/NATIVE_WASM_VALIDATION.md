# Validação Native / WASM — F4

## Builds

- Native: CMake Release, MinGW GCC 14.2.0, `-ffp-contract=off`,
  `Classic C=1 / F2=2 / F3=0 / F4=1`.
- WASM: Emscripten 3.1.62 (emsdk), mesmas flags; `hydra_render_trace.js` e
  `f4_transitions_test.js` compilados diretamente do core canônico.
- F4=0: mesma toolchain Native, mesmas flags, `F4=0`.

## Referência F3-A

`run.py baseline` compilou o F4=0 e comparou com
`docs/evidence/dsp-reform-f3/F3-A-manifest.json`:

- **10/10** cenários F0.2 com hash de entrada e de saída **bit-exact**;
- `F4-A-reference.json`: `BIT_EXACT_F3_A`, zero falhas.

## Preservação estática F4=1

Os mesmos 10 cenários, com F4=0 e F4=1:

- **10/10 bit-exact** (`max_abs = 0`);
- controles negativos da bancada também bit-exact;
- `mix_0_100_0`, `mix_100_0`, `mix_25_75`: após a rampa de 10 ms e 100 ms de
  acomodação, F4=1 volta a ser **bit-exact** com F4=0;
- `multihead_mix`: resíduo máximo `1,25e-5` (−98 dBFS), compatível com memória
  dos filtros de playback; não há diferença de ganho.

## Native/WASM

| Verificação | Resultado |
|---|---|
| `tests/core/compare_native_wasm.py` (5 cenários, 1e-5 / 1e-6) | **max_abs = 0; RMSE = 0; PASS** |
| Contrato F4 nativo (CMake) | PASS |
| Contrato F4 WASM (Node) | **PASS** |
| Métricas do contrato nativo vs WASM | idênticas (head 0,0339277014; mix 3,28421593e-5; freeze 0,0123037472) |
| Blocos 1/37/64/128/256 no contrato | **max_abs = 0** em ambas as plataformas |

O contrato F4 compilado para WASM reproduziu exatamente as mesmas métricas do
nativo, incluindo o pós-transição. O comparador estrito manteve os limites
históricos, sem relaxamento.

## CTest Native

Build `build/f4-1-cmake`, 21 testes. Excluindo os goldens (que são da rota de
produção e não das flags experimentais C/F2/F3, como na F3), **16/18 PASS**:

- `core_smoke`, `core_audible_controls`, `f4_performance_transitions`, Warp,
  Reverse, Tap Tempo, MIDI Clock, Transport, ABI legado, LUT/bias,
  `fast_tanh_integrity`;
- `core_regression` falha **somente** na assertion de energia A
  `channel_energy(fullR, 2840, 2960) > 1e-5f`, conhecida e preservada desde a
  F0/F1; não foi relaxada nem mascarada;
- `core_transition_characterization` falha por permissão de limpeza de
  diretório temporário do Python neste ambiente Windows (trace
  `tempfile._resetperms`, `PermissionError`), antes de qualquer medição DSP.
  Não é uma falha do TapeCore; a bancada F4 equivalente roda fora do CTest.

Na rota de produção default (`build/f4-default`, sem flags experimentais), o
CTest também sofre o mesmo bloqueio de temp nos 3 goldens e na caracterização;
os 17 testes restantes passam. Executando os goldens **manualmente**, com
saída dentro do workspace, os três cenários (`delay_modes`, `reset_state`,
`param_automation`) retornam `max_abs = 0`, `rmse = 0` e **PASS**. Ou seja, a
rota de produção e seus goldens não foram alterados; a falha é do ambiente.

Os logs estão em `artifacts/dsp-reform-f4/ctest-native.log` e
`ctest-native-full.log`. A falha de CTest relacionada à energia da rota A não
foi ocultada. O flake de Tap Tempo da PR #63 não reapareceu nas execuções
desta fase.

## CTest WASM

O suite CTest completo não pôde ser gerado neste ambiente porque o emcc/CMake
encontra o mesmo bloqueio de permissão de diretórios temporários do Windows
sandbox (o `emcc` não consegue escrever no diretório temporário criado pelo
próprio processo). Não é uma falha de DSP nem do WASM.

Para qualificar o runtime sem mascarar o problema, os dois alvos relevantes
foram compilados diretamente com `emcc` e executados no Node:

- `hydra_render_trace.js`: comparador Native/WASM **PASS**, 0/0;
- `f4_transitions_test.js`: **PASS** com as mesmas métricas do nativo.

A pendência é de infraestrutura de CI Windows, não de paridade numérica. O
workflow `.github/workflows/dsp-reform-f4.yml` roda o CTest Native e WASM em
Ubuntu, onde esse bloqueio de ACL não existe.

## O que não foi validado

- Nenhuma execução em ESP32-S3 físico: **PENDING_PHYSICAL**.
- Nenhum benchmark físico P50/P95/P99/peak, full-work, I2S/overruns ou comandos
  sob carga.
- Nenhuma promoção de default; F4=0 continua a produção.
