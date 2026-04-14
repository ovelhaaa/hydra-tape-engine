# Hydra Tape Engine

Projeto de DSP compartilhado (native + web) para um emulador de tape delay multi-head, com API C estável para integração com hosts.

## API pública estável (`include/hydra_dsp.h`)

A API pública formalizada inclui:

- versão de API via `hydra_dsp_get_api_version()`;
- IDs de parâmetros congelados (`hydra_dsp_param_id` com valores explícitos);
- tabela oficial de parâmetros (`hydra_dsp_param_spec`) com:
  - `min_value`
  - `max_value`
  - `default_value`
  - `smoothing` (`NONE`, `BLOCK_EDGE`, `PER_SAMPLE_INTERNAL`)

### Compatibilidade de host

O contrato de compatibilidade é:

1. Hosts integram pelo header público e pelas funções C exportadas.
2. Mudanças internas de DSP (`dsp/core/*`) não devem exigir mudança de integração em host-native nem host-web.
3. Alterações de ABI exigem bump de versão de API e atualização explícita de testes de ABI.

---

## Build native

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build -j
```

## Build web (Emscripten)

```bash
emcmake cmake -S . -B build-web -DBUILD_WEB=ON -DBUILD_TESTING=ON
cmake --build build-web -j
```

Artefatos principais (web):

- `build-web/web/hydra_dsp.js`
- `build-web/web/hydra_dsp.wasm`
- `build-web/web/index.html`
- `build-web/web/main.js`
- `build-web/web/hydra-processor.js`

---

## Execução de testes

### Suite native

```bash
ctest --test-dir build --output-on-failure
```

Inclui:

- `core_smoke`
- `core_regression`
- `core_abi_symbols` (sanity check de símbolos ABI esperados)
- golden regressions

### Suite web + equivalência native/wasm

```bash
ctest --test-dir build-web --output-on-failure
```

Inclui também:

- `native_vs_wasm_equivalence`
- golden regressions wasm

---

## Tolerâncias numéricas

Para equivalência native vs wasm:

- `max_abs <= 1e-5`
- `rmse <= 1e-6`

Essas tolerâncias absorvem diferenças pequenas de backend/compilador e mantêm paridade comportamental.

---

## Guia: como atualizar goldens corretamente

Use somente o script controlado:

```bash
scripts/update_golden.sh --overwrite
```

Fluxo recomendado:

1. Garanta branch limpa e testes verdes antes.
2. Rode `scripts/update_golden.sh --overwrite`.
3. Rode novamente os testes nativos e, se aplicável, wasm.
4. Revise diffs dos CSVs em `tests/fixtures/golden/native`.
5. No commit/PR, registre o motivo da atualização (mudança intencional de DSP, correção de bug etc).

Observação: sem `--overwrite` o script recusa sobrescrever arquivos.

---

## Divergências de plataforma esperadas (sem hardware)

Mesmo com núcleo compartilhado, há diferenças práticas entre host-native e host-web:

- **Block size**: engine/host podem chamar `process` com tamanhos de bloco distintos;
- **Latência de browser**: AudioWorklet + pipeline do navegador adicionam latência não presente em execução CLI nativa;
- **Scheduler**: cadência de thread/event loop no browser difere de processos nativos, afetando timing operacional (não o contrato da API C);
- **Float backend**: pequenas diferenças numéricas entre toolchains.

Por isso a validação usa equivalência com tolerâncias, não igualdade bit-a-bit.

---

## Critério de aceite

Aceite para mudança interna de DSP:

- API pública (`include/hydra_dsp.h`) preservada ou versionada corretamente;
- teste de ABI (`core_abi_symbols`) passando;
- regressões e equivalência native/wasm passando;
- integração host-native/host-web não quebrada por mudanças internas.
