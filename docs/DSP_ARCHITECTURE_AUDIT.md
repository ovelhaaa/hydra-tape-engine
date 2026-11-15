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
a `tests/core/build_ab_review_package.py`. O ZIP com os renders é
`%LOCALAPPDATA%/HydraAudit/528ebb8/reference-renders.zip`.

Os cinco traces CSV são byte a byte idênticos à referência dentro de cada
backend após as alterações. A comparação cruzada MinGW/WASM já excedia o
limite de RMSE de `1e-6` na base. Em `baseline_glue` o máximo é `7.436e-6` e
o RMSE é `2.49805e-6` antes e depois. O pacote A/B também falha nesses
toolchains.

No GitHub Actions, com Linux e Emscripten 3.1.62, a comparação cruzada
registrou `max_abs=3.4746e-5` e `RMSE=1.52453e-5` tanto no SHA base
quanto na branch. Os cinco pares de traces antes/depois foram idênticos
em cada backend também no runner hospedado. A CI continua a reprovar a
equivalência cruzada pelos limites originais (`1e-5` e `1e-6`) e publica
os relatórios de falha. As fixtures permanecem versionadas com o mesmo conteúdo.
[Comparação hospedada com o SHA base](https://github.com/ovelhaaa/hydra-tape-engine/actions/runs/43677511076/job/132700929038)

A mutação temporária que exigia minor da API igual a `999` fez o
job `test_native_core` do GitHub Actions falhar em Release com o diagnóstico
da expressão e código de saída 8. A mutação de `859f5af` foi revertida em
`5730536`. [Log da falha intencional](https://github.com/ovelhaaa/hydra-tape-engine/actions/runs/43677156862/job/132699803823)

## Validação de build

- CMake Release em MinGW com 8/8 testes aprovados, incluindo símbolos ABI e
  goldens. Os 16 testes do host web também passaram, incluindo presets.
- CMake Release em Emscripten 5.0.7 produziu o runtime web. Os 8 testes do
  backend passaram, incluindo os três goldens WASM. Os dois testes cruzados
  falharam conforme as métricas de referência acima.
- PlatformIO `esp32-s3-devkitc-1` compilou e linkou com Arduino 3.3.6 e
  Xtensa GCC 14.2. O build usou 25.616 bytes de RAM estática e 543.455 bytes
  de flash. O ELF contém o adaptador, a ABI C, TapeCore, Fripp, Bubbles e
  Freeverb. A tabela de símbolos e o mapa do linker confirmam a ausência
  de TapeModel. O único ajuste no core foi explicitar `int32_t` no máximo
  da janela reverse para acomodar a definição desse tipo no Xtensa.
- O simulador histórico PlatformIO `native` e o firmware em placa ficaram
  fora da validação.

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
  A falha cruzada reproduzida em Linux e MinGW mantém o PR em rascunho.
- Tap tempo pode entrar na camada host. O host poderá calcular e publicar
  `bpm` para heads musicais e `delayTimeMs` para atraso em milissegundos
  usando o snapshot existente e o commit na fronteira de processamento.

## Validação complementar da PR #46

### Equivalência numérica

O trace curto continua em 2.048 frames, 48 kHz e blocos de 32. No
runner Ubuntu 26, o GCC 15.2.0 e o Emscripten 3.1.62 reproduziram
`max_abs=3.4746e-5` e `RMSE=1.52453135792e-5` no SHA original e
na PR. As compilações usam C++17 e `-O3`. A base usa asserts ativos
para executar o runner histórico. A PR usa `-DNDEBUG` com checks ativos.
O target nativo do runner define `__FMA__`. Nenhum perfil de teste abaixo
foi aplicado ao firmware ou aos builds de produção.

| Ponto de observação | Primeira diferença | Resultado |
|---|---:|---|
| Estímulo do trace, com seno calculado por backend | 10 | Pico de `5.96e-8` no estímulo, com diferença na saída seca no mesmo frame. |
| Saída do passa-altas de entrada, com estímulo comum | 3 | Pico de `3.69e-4`. O biquad usa recorrências com produtos e somas sobre `z1` e `z2`. O desvio deixa de aparecer nesse estágio quando a contração também é desativada. |
| Coeficiente `a2` do high shelf, flags padrão | Inicialização | Diferença de um ULP. A sonda mede retornos de trigonometria e coeficientes separadamente. |
| Leitura da textura, estímulo comum e contração desativada | 201, canal R | O erro de `9.31e-10` passa pelas etapas de playback e realimentação. A maior diferença residual na saída ocorre no frame 1979, canal L. |
| Saída Release original, sem instrumentação | 10, L/R | Maior erro L no frame 1078 (`3.4746e-5`), R no 1272 (`3.1859e-5`). Os frames são contados a partir de zero. |

| Perfil de diagnóstico | max_abs | RMSE |
|---|---:|---:|
| Release base e PR | 3.4746e-5 | 1.52453e-5 |
| Estímulo comum float32 | 3.6508e-5 | 1.52561e-5 |
| Contração desativada nos dois compiladores | 3.7298e-5 | 1.39484e-5 |
| Estímulo comum e `-ffp-contract=off` | 8.285e-6 | 2.25515e-6 |

O controle combinado elimina o desvio no condicionamento de entrada nesse
cenário, mas o RMSE residual ainda excede `1e-6`. A medição delimita o efeito
da contração e da geração do estímulo. Ela ainda não isola cada contribuição
numérica na escrita, na interpolação e nos filtros de reprodução.

O script gera cópias instrumentadas via `git show`. Em um ambiente com os
mesmos compiladores e Node, execute:

```bash
git fetch --no-tags --depth=1 origin 528ebb87281d29331f7df19b0a375f7892d7fd2d
python3 tests/core/diagnose_equivalence.py \
  --base 528ebb87281d29331f7df19b0a375f7892d7fd2d \
  --output artifacts/equivalence/operations.json
```

O JSON registra flags, versões, hashes de traces, valores float em hexadecimal
e primeiro frame e pico por canal e estágio instrumentado.
[Execução diagnóstica no runner hospedado](https://github.com/ovelhaaa/hydra-tape-engine/actions/runs/43678985420/job/132705413617)

**Critério proposto para revisão futura.** Manter o relatório absoluto atual com
limites `1e-5` e `1e-6`. Se for adotado um gate adicional por plataforma, usar
estímulos float32 compartilhados, toolchains identificados e uma referência
imutável para cada backend. Aplicar os limites atuais ao erro de cada backend
contra a própria referência e à diferença entre o residual cruzado atual e
o residual cruzado de referência, calculada amostra a amostra. Manter os
goldens e invariantes de comportamento e exigir retomada explícita das
medições quando o toolchain mudar. Antes de adotar esse gate, verificar
sensibilidade com mutações de ganho, posição de head e troca L/R.
Nenhum limite nem golden foi alterado nesta rodada.

### Renders de escuta

O job `listening_review` produz seis pares Native/WASM de 12 s a 48 kHz
com entrada float32 compartilhada e WAV estéreo PCM24 sem normalização.
Inclui impulso com head único, transientes musicais multi-head, sinal
harmônico com feedback alto, impulso ping-pong, trecho com wow/flutter e
trecho com drive de 5 a 100 em passos de 5. Os trechos padrão são
sintetizados e há suporte a WAV estéreo PCM16/48 kHz com `--excerpt-wav`.
Os renders conservam a entrada seca para referência e reservam cauda após o
fim da entrada nos trechos musicais. No impulso de head único, os cinco picos
das janelas de repetição foram 0,338, 0,156, 0,093, 0,069 e 0,048 no native local.

```bash
cmake --build build --target hydra_listen_native
cmake --build build-web --target hydra_listen_wasm
python3 tests/core/listening_review.py \
  --native build/hydra_listen_native \
  --wasm-js build-web/wasm-tests/hydra_listen.js \
  --output-dir artifacts/listening
```

O pacote registra picos de eco, cronogramas e métricas de transição em
`review.json`. A geração valida frames, finitude e atividade dos sinais.
O job de escuta é independente do gate de equivalência. Os traces e o
null-test curto continuam disponíveis para diagnóstico numérico.

### Transições e limitações

Medições locais em Release, a 48 kHz e em fronteiras de blocos de 128 frames.
Os valores abaixo são do Native. O WASM reproduziu o mesmo padrão.

| Caso | Observação | Limitação |
|---|---|---|
| Freeze ligado aos 3 s e liberado aos 6 s | Com seno de 440 Hz, o maior passo antes/depois em 20 ms ficou próximo de 0,0575 nos dois comandos. As bordas ficaram em 0,0453. O controle sem freeze ficou na mesma faixa. | A escrita é interrompida e retomada imediatamente. `freezeFade` é atualizado, mas o áudio não consulta esse estado. O teste de tom não confirma suavidade para material arbitrário. |
| Reverse estéreo com marcadores idênticos | Com entrada em apenas um canal, o canal oposto permaneceu em zero nas duas direções. Subtraindo o caminho de textura, os inícios dos quatro marcadores diferiram em −1, 0, −1 e −1 frame. Os picos dominantes diferiram em −6.401, 0, +6.399 e −6.400 frames. | Os canais compartilham um contador incrementado por leitura. O início quase simultâneo convive com picos dominantes em repetições distintas, a cerca de 133 ms de distância neste ensaio. |
| Delay de 400→700 ms e 700→220 ms | O maior passo em 20 ms subiu de 0,0562 para 0,0718 na primeira transição e de 0,0588 para 0,1297 na segunda. Removendo a textura, cruzamentos por zero estimaram aproximadamente 406 e 1.374 Hz nos primeiros 50 ms, com entrada de 440 Hz. | O smoothing move a posição de leitura e produz mudança de pitch e aumento de inclinação. As bordas do comando não foram o maior passo da janela. O ensaio não confirma audibilidade de clique nem mede uma frequência fundamental durante toda a transição. |

O teste Release de caracterização verifica atividade e separação de canais
e registra as métricas. Os desvios temporais e de pitch ficam como
caracterização do comportamento atual para refinamento sonoro.

### Resultado de validação

CMake native Release passou 9/9. O backend WASM passou seus oito testes de
Release, incluindo goldens. Os 16 testes do host web passaram. O build
PlatformIO ESP32-S3 voltou a passar e a imagem incremental manteve o mesmo
SHA-256. A equivalência estrita e o pacote curto de comparação cruzada local
seguem fora dos limites. A escuta humana, o simulador PlatformIO native
e a execução em placa ficaram fora desta rodada.
