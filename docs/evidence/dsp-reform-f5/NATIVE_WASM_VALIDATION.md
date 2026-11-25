# F5 — Validação Native/WASM

**Limites de paridade:** `max_abs ≤ 1e-5`, `RMSE ≤ 1e-6`. Nenhuma tolerância foi
relaxada e nenhum golden foi regenerado.

## 1. Duas categorias de build, de propósito

O workflow `dsp-reform-f5.yml` foi corrigido na F5.1. Antes, `Classic C=1`,
`F2=2` e `F3=0` eram exportados como `CMAKE_CXX_FLAGS` **globais** apenas na
árvore Native. Isso produzia dois erros ao mesmo tempo:

1. os testes de regressão de produção rodavam numa topologia experimental;
2. o binário Native comparado com o WASM tinha uma configuração diferente da
   do WASM (que não recebia as flags).

Causa raiz reproduzida localmente com o mesmo código:

```
configuração de produção (sem defines): regression_core: ok
configuração F5 (Classic C=1/F2=2/F3=0 global): regression_core.cpp:255:
  check failed: channel_energy(fullR, 2840, 2960) > 1e-5f
```

A falha da execução 45524614234 era, portanto, de arquitetura de CI, não de DSP.

Categorias atuais:

| Job | Configuração | O que roda |
|---|---|---|
| `production-regression` | flags originais de produção, sem defines experimentais | CTest completo (20 testes), Native e WASM, goldens, ABI, Reverse, Warp, Tap, MIDI, Transport, parâmetros públicos |
| `f5-qualification` | `Classic C=1/F2=2/F3=0` **por alvo**, matriz A/B/C | contrato F5 de cada variante em Native e WASM + comparação estrita Native/WASM por variante |
| `f5-evidence-and-listening` | bibliotecas compiladas pelo próprio `run.py` com as flags de `f5bench.FLAGS` | baseline F3-A, preservação estática, equivalência de instrumentação, blocos, Tap/MIDI, Reverse, drift, caudas, render e pacote |

Os alvos F5 são excluídos do `ctest` de produção com `-E '^f5_'`; eles rodam no
job de qualificação, com nome explícito por variante.

## 2. Produção (execução 45529104237)

```
Native: 100% tests passed out of 20
WASM:   100% tests passed out of 20
```

O CTest de produção cobre goldens (`delay_modes`, `reset_state`,
`param_automation`), ABI 1.7/38 e consumidor legado v1.2, Reverse V2, Warp,
Tap Tempo, MIDI Clock, Global Transport Clock, Ping-Pong, Multi-Head,
bias/LUT, caracterização de transições e IDs de parâmetros web. Os goldens não
foram tocados.

## 3. Qualificação F5 (mesma execução)

Contratos próprios, um job por variante:

```
f5-0 Native: PASS    f5-0 WASM: PASS
f5-1 Native: PASS    f5-1 WASM: PASS
f5-2 Native: PASS    f5-2 WASM: PASS
```

Paridade estrita Native/WASM, comparando dois binários com as **mesmas**
definições experimentais:

```
f5_v0_native_wasm: max_abs=0.000000000000e+00 rmse=0.000000000000e+00 status=PASS
f5_v1_native_wasm: max_abs=0.000000000000e+00 rmse=0.000000000000e+00 status=PASS
f5_v2_native_wasm: max_abs=0.000000000000e+00 rmse=0.000000000000e+00 status=PASS
```

## 4. Alvos CMake

`CMakeLists.txt` cria, dentro de `BUILD_TESTING`:

- `hydra_f5_timing_test_v0/1/2` — contrato F5, com
  `HYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING=1`, `HYDRA_DSP_REFORM_F2_VARIANT=2`,
  `HYDRA_DSP_REFORM_F3_VARIANT=0`, `HYDRA_DSP_REFORM_F5_TIMING_VARIANT=n` e
  `HYDRA_DSP_TEST_HOOKS=1`. Sob Emscripten recebem
  `-sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=64MB`.
- `hydra_f5_trace_v0/1/2` — testemunhas de paridade, com as mesmas definições
  experimentais e sem o hook. Sob Emscripten saem em `wasm-tests/` com
  `-sENVIRONMENT=node`.

O alvo de produção `hydra_render_trace_native` continua sem qualquer define
experimental, então `native_vs_wasm_equivalence` segue medindo produção A.

## 5. Ambiente local (não é o CI)

- O sandbox Windows desta sessão não consegue concluir `cmake` configure (um
  probe de compilador fica bloqueado) nem gravar no diretório temporário criado
  pelo `emcc`; a validação local usou compilação direta `g++`/`emcc` com as
  mesmas flags dos alvos e `EMCC_TEMP_DIR` pré-criado.
- O CI Linux não tem essas restrições; os jobs acima rodaram `cmake`/`emcmake`
  normais.
- `run.py build` falhou no primeiro run corrigido por falta de `-fPIC` no
  `g++ -shared`; `f5bench.py` agora passa `-fPIC` fora do Windows, como os
  benches da F1–F3 já faziam.

## 6. O que não foi validado

- Placa física: `PENDING_PHYSICAL` (DSP P50/P95/P99/peak, full-work, I2S,
  overruns, memória e stack).
- Escuta humana: pendente; nenhuma variante promovida.
