# Auditoria da arquitetura DSP

Base de trabalho: `528ebb87281d29331f7df19b0a375f7892d7fd2d` (`main`).
Branch: `codex/consolidate-tape-core`.

## Dependências e targets

| Target | Caminho ativo na base | Após a consolidação |
|---|---|---|
| PlatformIO `esp32-s3-devkitc-1` | `main.cpp` → `HydraDspEsp32Adapter` → ABI C → `TapeCore`. O adaptador recebia o `::TapeParams` de `TapeDelay.h`. `src/hydra_dsp_build.cpp` incluía três `.cpp`. O PlatformIO compilava `TapeDelay.cpp` junto dos três efeitos, embora o firmware ativo não referencie `TapeModel`. | `main.cpp` publica um snapshot `hydra_dsp_params`. O script de build compila o core, a API C e o adaptador em unidades separadas. A biblioteca `TapeDelay` compila Fripp, Bubbles e Freeverb por meio de seu manifesto. |
| PlatformIO `native` | Ferramenta de simulação e geração WAV sob `test/test_native`. Declara `TapeModelTest` e um `TapeParams` local e ignora a biblioteca `TapeDelay`. | A ferramenta histórica continua disponível nesse target. A validação do DSP de produção é feita pelo CMake. |
| CMake native | `hydra_dsp_core` fornece `TapeCore` e o bridge C a smoke, regression, audible controls, trace e golden. | Mesmo caminho. Os checks executam em Release e o trace native é produzido pelo toolchain nativo. |
| CMake WASM e GitHub Pages | `hydra_dsp_web`, runners de trace e golden usam os mesmos fontes canônicos. Em uma configuração Emscripten, o runner com nome `_native` também virava WASM. | A comparação usa um executável nativo externo. O runner golden WASM lê fixtures e escreve resultados no filesystem do Node. |
| Browser | Worklet → ABI C → `TapeCore`. O host web normaliza presets e publica IDs. | Mesmo caminho, incluindo serializer e IDs. |

O `hydra::dsp::TapeParams` interno é preenchido pelo bridge C. O
`::TapeParams` de `TapeDelay.h` se aplica a `TapeModel` e à magnetização legada.
A busca por `TapeModel` nos fontes compiláveis da base encontrou sua própria
declaração e implementação, sem ponto de chamada ativo. Backups e relatórios
históricos ainda citam a classe. `Frippertronics-Core` é uma biblioteca histórica
distinta de `FrippEngine` e não é dependência do firmware ativo.

`TapeDelay.h` ainda fornece tipos e helpers aos três efeitos e à UI embarcada.
O manifesto limita o build aos três efeitos e deixa a implementação de
`TapeModel` disponível para consulta. A troca do adaptador usa a mesma conversão
de valores da UI e mantém `p_mix` como origem de `dryWet` e `delayWet`.

## Referência de áudio e testes Release

Os renders anteriores à alteração foram gerados com GCC 14.2 (MinGW) e Emscripten
5.0.7, com `-O3` e checks ativos via flags de Release sem `NDEBUG` na base.
O pacote A/B está em `%LOCALAPPDATA%/HydraAudit/528ebb8/renders` com WAVs e métricas
para `baseline_glue`, `short_slap_bright`, `feedback_edge`, `saturated_dark` e
`modulated_space`. Para reproduzir a partir do SHA base, compile os dois runners
com `-DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE=-O3` e passe os binários
a `tests/core/build_ab_review_package.py`.

Os cinco traces CSV são byte a byte idênticos à referência dentro de cada
backend após as alterações. A comparação cruzada MinGW/WASM já excedia o
limite de RMSE de `1e-6` na base. Em `baseline_glue` o máximo é `7.436e-6` e
o RMSE é `2.49805e-6` antes e depois. O pacote A/B também falha nesses
toolchains. As tolerâncias e fixtures versionadas foram preservadas.

A mutação temporária que exigia minor da API igual a `999` falhou em Release
com diagnóstico de linha e expressão. O comando do job native,
`set -o pipefail; ctest --output-on-failure | tee …`, preservou o status 8 de
CTest na reprodução local. Após desfazer a mutação, o smoke passou.
O log local está em `%LOCALAPPDATA%/HydraAudit/validation/native/ctest_failure_probe.txt`.

## Arquivo histórico

Antes da remoção, `git archive` copiou todo o SHA base para
`%LOCALAPPDATA%/HydraAudit/528ebb8/repository.zip`.
SHA-256: `ecb681487310b14dba2134af19cc2e6310c0f3d97e2907d30960038c85804cc6`.
É possível reconstruir o arquivo a partir da revisão acima.

- `build-web/` continha 336 caminhos rastreados, incluindo objetos, cache
  CMake e cópias do host web. O build local agora é ignorado pelo Git.
- `extracted_logs/`, `extract_from_logs.py`, `recovered_*`, os dois
  `.bak` e o bytecode Python constam do arquivo.
- `docs/CODE_REVIEW.md` e `docs/FRIPPERTRONICS_REPORT.md` descrevem fases
  anteriores. A especificação `Apollo/docs/UI_UX_PHASE_1.md` fica junto do
  contexto Apollo. Esses documentos e a interface serial `web-interface/`
  continuam disponíveis na árvore.

## Riscos e escopo de validação

- O build ESP32 valida resolução de símbolos e dependências. Alocação de
  PSRAM, custo em tempo real e escuta de Fripp, Bubbles e Freeverb ainda
  dependem de execução em placa.
- A equivalência cruzada depende dos toolchains e da biblioteca matemática.
  O resultado MinGW/WASM acima é uma pendência conhecida para revisão.
- Tap tempo pode entrar na camada host. O host poderá calcular e publicar
  `bpm` para heads musicais e `delayTimeMs` para atraso em milissegundos
  usando o snapshot existente e o commit na fronteira de processamento.
