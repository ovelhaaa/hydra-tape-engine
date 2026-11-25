# F5 — Validação Native/WASM

**Limites de paridade:** `max_abs ≤ 1e-5`, `RMSE ≤ 1e-6` por cenário. Nenhuma tolerância
foi relaxada.

## 1. Native

`cmake -S . -B build-f5-native -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON`,
`ctest` completo: **23/23 PASS**. Inclui:

- F5 v0, v1 e v2 (`f5_timing_v0/1/2`);
- goldens `delay_modes`, `reset_state`, `param_automation`;
- ABI 1.7/38 e consumidor legado v1.2;
- Tap Tempo, MIDI Clock, Global Transport Clock, Reverse V2, Warp, Ping-Pong,
  Multi-Head, bias/LUT, feedback stability e caracterização de transições.

Saída do contrato F5 (resumo):

```
F5 v0: PASS   (transport exato; heads Vintage; MIDI stale 888,60 amostras)
F5 v1: PASS   (glide limitado; convergência 1,27 s em 120->100; MIDI 9,31 amostras)
F5 v2: PASS   (política de clock; MIDI 3,83 amostras; tap 0,50 amostra)
```

## 2. WASM

Compilado com Emscripten 3.1.62 (`-O3 -DNDEBUG -ffp-contract=off`, `-sWASM_BIGINT=1`).

- `hydra_f5_timing_test_v0/v1/v2.js` executados com Node: **3/3 PASS**, mesmos valores de
  convergência do Native.
- `hydra_golden_regression.js` contra os mesmos goldens: **3/3 PASS**,
  `max_abs = 0`, `RMSE = 0`.
- `hydra_render_trace.js` vs `hydra_render_trace_native`:

```
native_vs_wasm_equivalence: max_abs=0.000000000000e+00 rmse=0.000000000000e+00 status=PASS
```

## 3. Alvos CMake

`CMakeLists.txt` cria `hydra_f5_timing_test_v0/1/2` com
`HYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING=1`, `HYDRA_DSP_REFORM_F2_VARIANT=2`,
`HYDRA_DSP_REFORM_F3_VARIANT=0`, `HYDRA_DSP_REFORM_F5_TIMING_VARIANT=n` e
`HYDRA_DSP_TEST_HOOKS=1`. O core de produção continua F5=0.
Sob Emscripten os alvos recebem `-sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=64MB`, porque
o contrato renderiza várias cenas longas; isso não altera o DSP.

## 4. Ambiente local e ressalvas

- O CTest Native usa `build-f5-native` e um `sitecustomize` **fora do repositório** que
  contorna uma restrição do sandbox Windows: `tempfile.mkdtemp` cria diretórios com modo
  0o700 que o token confinado não consegue reabrir. Sem o shim, os testes que usam
  `tempfile` falham por permissão, não por DSP. O CI Linux não precisa do shim.
- O build WASM local usou um wrapper que executa `emcc.py` sem `-E` para permitir o mesmo
  shim; o workflow usa `emcmake` normal.
- O comparador estrito, os goldens e o contrato F5 foram executados com o core canônico
  (F5=0) e com as três variantes; nenhuma diferença foi atribuída a compilador sem
  verificação.

## 5. O que não foi validado

- CTest WASM completo de todos os alvos legados (o workflow o executa em Linux).
- Placa física: `PENDING_PHYSICAL`.
