# Gate de release — Accidental Warp V1.3

Auditoria de 2026-11-15 da [PR #47](https://github.com/ovelhaaa/hydra-tape-engine/pull/47).

## Revisões e builds de referência

| Referência | Commit |
|---|---|
| HEAD no início da auditoria | `d942a7b26523e5a50193e0fb9206093f070cc130` |
| Correções de PCM e Freeverb | `b9fbbd342e3ead2054ca02c49705e385ba4fd985` |
| `main` comparada | `e4cae4354716cd2ac207a67afe95303b30bb6e4b` |
| Performance Rescue medido anteriormente | `c9bc17eaeb9888e9fb037c3459091e3251d5e46e` |
| A/B anterior ao Performance Rescue | `f8d2851c663d62ae4850087dc5bb255bd8b818be` |

O diff de `c9bc17e` até `d942a7b` contém quatro documentos. O BIN local encontrado antes da reprodução tinha SHA-256 `6fc77303aa1db11db6db71002e8b8935b758e908e9e5b01f1465c46455b74e7b`. O build novo descrito abaixo tem identidade própria. Os 183 WAVs já presentes em `artifacts/` foram inventariados em `artifacts/release-gate/preexisting-wav-sha256.csv`. Os novos renders ficam em uma árvore própria dentro de `artifacts/`.

| Build local (SHA-256) | PR | `main` |
|---|---|---|
| Trace Native | `e863f9ee77b5a6caab4bd22f5a2795c8da658f49c0b98527845d3864d070accd` | `a17d884e5f8dc32ed6ee5db1320e9dc54f2889e054eb9ac4350eca9a9c073002` |
| Binário do trace WASM | `340cd2bfe1c6296c76feba365124730a6cd7ea527fed1ba0a0cb83d08b45a775` | `c55190ecb2e16532ac28dfd1e7f30ec9f2929569fd21a4d9f6bbdaae2cbcaecb` |

## CI e proposta de exceção

No HEAD `d942a7b`, passaram `build_native_core`, `test_native_core`, `build_wasm_core`, `validate_warp` (tri-engine e consumidor ABI com AddressSanitizer), `listening_review` e `build_esp32_s3`. Falhou `test_native_vs_wasm`. Na workflow Web Demo, `build` passou e `deploy` foi pulado no evento de PR. Na `main`, o comparador estrito também falhou e o deploy web passou. [Core Equivalence PR](https://github.com/ovelhaaa/hydra-tape-engine/actions/runs/43776656015) · [Web Demo PR](https://github.com/ovelhaaa/hydra-tape-engine/actions/runs/43776656027) · [Core Equivalence main](https://github.com/ovelhaaa/hydra-tape-engine/actions/runs/43680877692) · [Web Demo main](https://github.com/ovelhaaa/hydra-tape-engine/actions/runs/43680877711)

| Ambiente | PR max_abs / RMSE | `main` max_abs / RMSE |
|---|---:|---:|
| GitHub Actions, Ubuntu, Emscripten 3.1.62 | `4,15300e-5 / 1,58022e-5` | `3,47460e-5 / 1,52453e-5` |
| Windows, GCC 14.2.0, Emscripten 5.0.7 e Node 24.11.1 | `8,47900e-6 / 2,74890e-6` | `7,43600e-6 / 2,49805e-6` |

Os dois commits falham com `max_abs <= 1e-5` e `RMSE <= 1e-6`. No Windows só o RMSE excede o limite. No Linux o HEAD subiu ~19,5% em max_abs e ~3,65% em RMSE sobre uma divergência já presente. Em escala de áudio normalizada, o aumento absoluto foi `6,784e-6` no máximo e `5,569e-7` no RMSE. A exceção proposta vale para esse teste e essas revisões. **Decisão do responsável pelo merge: pendente.** Critérios, fixtures e estado do job continuam estritos.

Para reproduzir, crie worktrees nos dois SHAs e execute em cada uma com o mesmo compilador Native, SDK WASM e Node:

```sh
cmake -S . -B build-native -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-native --target hydra_render_trace_native
emcmake cmake -S . -B build-wasm -DCMAKE_BUILD_TYPE=Release -DBUILD_WEB=ON -DBUILD_TESTING=ON
cmake --build build-wasm --target hydra_render_trace_wasm
python tests/core/compare_native_wasm.py --native build-native/hydra_render_trace_native \
  --wasm-js build-wasm/wasm-tests/hydra_render_trace.js --max-abs-threshold 1e-5 \
  --rmse-threshold 1e-6 --report-json equivalence.json
```

No Windows com Ninja, use o sufixo `.exe` no runner Native. Use builds novos para cada revisão. O caminho de trace padrão tem Warp desligado. O teste gera o estímulo com `std::sin` em cada backend e inicializa coeficientes e LUT magnética com libm. Diferenças de arredondamento podem se propagar pelo IIR e pela realimentação. As flags de PlatformIO não são usadas por esses builds CMake.

No diagnóstico Windows, o primeiro desvio do estímulo ocorreu no frame 18 (`sinf` de 330 Hz, pico `5,96e-8`). O compilador Native desse ensaio não define `__FMA__`. Usar o mesmo estímulo float32 e desabilitar contração nos dois binários de diagnóstico deixou `max_abs = 6,929e-6` e `RMSE = 2,61838e-6`. Reutilizar também a LUT magnética Native no binário WASM de diagnóstico zerou o desvio desse trace. Em Ubuntu, o diagnóstico anterior da PR #46 também isolou a contração no IIR de entrada. A sonda identifica os efeitos de libm e contração sob as respectivas configurações. O comparador Release continua usando os caminhos de produção. [Auditoria Native/WASM anterior](DSP_ARCHITECTURE_AUDIT.md#equivalência-numérica)

O script de diagnóstico foi ajustado para localizar a atribuição de saída do filtro depois da alteração do comentário em TapeCore e obter `check.h` da revisão cujo runner o inclui. Execute `python tests/core/diagnose_equivalence.py --base e4cae43 --head d942a7b --output artifacts/equivalence/operations.json` com GCC, Emscripten e Node disponíveis. No Windows, `--wasm-cxx` pode receber o caminho do Python do SDK seguido do caminho de `em++.py`.

Na reprodução local da PR passaram 11/11 CTest Native, 3/3 goldens WASM e 22/22 testes Web em Node com o WASM real. O pacote para revisão perceptual está no [registro de escuta](ACCIDENTAL_WARP_LISTENING_SIGNOFF.md).

## Matemática do TapeCore

Os três chamadores de `fastRecip()` usam denominadores positivos e normais para parâmetros finitos e sample rates de 44,1/48 kHz:

- Saturação racional: o argumento é limitado a `[-4, 4]`, portanto `27 + 9|x|² ∈ [27, 171]`.
- One-pole do dropout: `fc` é limitado a `[20 Hz, 0,45·fs]`. `1 + 2πfc/fs` está entre 1 e aproximadamente 3,827434.
- Warp: a inicialização e o alvo do grão têm piso de 64 samples. A suavização é uma atualização convexa com peso 0,005. Com o teto de 400 ms os máximos são 17.640 e 19.200 samples. Na API escalar, o mínimo nominal é 20 ms. A CLI aceita 10 ms e o piso interno continua valendo.

Zero, sinais negativos, subnormais, Inf e NaN ficam fora da pré-condição do recíproco, assim como configurações que produzam tamanho de grão infinito. A divisão de referência usa precisão dupla e arredondamento final para float. Parâmetros em lote da ABI e entradas de áudio têm de respeitar o domínio operacional. A presença de `-fno-finite-math-only` não transforma a API em um sanitizador.

A sonda no ESP32-S3 executou o `recip0.s` e os dois refinamentos copiados do TapeCore, sob as mesmas flags de compilação. Foram 131.073 valores por faixa, incluindo os extremos. Cada faixa válida ficou em até 1 ULP e erro relativo de até `1,19209e-7` (aproximadamente `2⁻²³`, resolução de float32). Esse limite é adequado para a aproximação: até o maior incremento de fase `2/64`, a contribuição de 1 ULP no recíproco é da ordem de `4e-9` por frame, com acumulação avaliada no render.

| Faixa no Xtensa | Máx. erro absoluto | Máx. erro relativo | Máx. ULP |
|---|---:|---:|---:|
| One-pole, [1, 3,827434] | `5,96046e-8` | `1,19209e-7` | 1 |
| Saturação, [27, 171] | `3,72529e-9` | `1,19180e-7` | 1 |
| Grão, [64, 19.200] | `1,16415e-10` | `1,19207e-7` | 1 |
| [0,5, 1,5] | `1,19209e-7` | `1,19180e-7` | 1 |
| Normais pequenos, [1e-30, 1e-20] | `5,62950e14` | `1,19205e-7` | 1 |
| Normais grandes, [1e20, 1e30] | `2,40741e-35` | `1,19208e-7` | 1 |
| Negativos fora do contrato, [-170, -1] | `1,49012e-8` | `1,19208e-7` | 1 |

A sonda retornou NaN para `±0`, `±Inf` e o menor subnormal positivo. Isso distingue o comportamento fora do contrato da divisão IEEE, que dá infinito, zero com sinal ou NaN conforme a entrada. O maior subnormal positivo, o menor normal e o maior float finito coincidiram com a referência, inclusive a saída subnormal no último caso. O log de bits e a fonte da sonda ficam em `artifacts/release-gate/target-audit.log` e `recip-probe/src/`.

`flushDenormals()` atua no final de cada bloco sobre magnetização, filtros de ruído e modulação, Warp, EQ de reprodução, dropout, azimute, DC, condicionamento de entrada, filtros de feedback e envelopes. Os estados de spring são tratados enquanto ativos. No DC blocker, `x1` é a amostra anterior e é sobrescrito na próxima amostra. Os allpasses com memória de delay têm guarda própria de `1e-15`. Os estados mecânicos recebem excitação pseudoaleatória contínua. A guarda por bloco cobre os estados recursivos listados, permitindo resíduos abaixo de `1e-20` ao longo de um bloco. A linha de delay não é varrida no fim do bloco.

Na sonda Xtensa, o silêncio digital permaneceu exatamente zero a 44,1 e 48 kHz. Um impulso de `1e-38` produziu duas amostras subnormais no caminho de saída direta em cada taxa, com pico `2e-39` e cauda zero. Nesse ensaio, processar em blocos de 128 com flags de produção e processar amostra a amostra com flags estritas gerou o mesmo sinal, inclusive nos limites dos blocos.

## Efeito de `-ffast-math` na saída

Para isolar as flags, a sonda Xtensa compilou duas instâncias do mesmo TapeCore a `-O2`. Cada instância recebeu as mesmas amostras e eventos. Ambas mantiveram o `recip0.s` e o flush por bloco. A tabela resume os resultados a 44,1 kHz, com feedback Warp de 95% e Tape a 95% nos cenários de estresse Ping-Pong e ruído/dropout.

| Cenário | RMSE | Diferença máxima | Observação |
|---|---:|---:|---|
| Tape Clean | `3,71e-4` | `1,62e-3` | RMS de saída ≈ 0,263 em ambos. |
| Warp, impulso e cauda | `8,19e-5` | `1,63e-2` | A cauda final chegou a zero nas duas variantes. |
| Warp, automação de Grain/Speed | `2,89e-4` | `1,34e-2` | RMSE 0,16% do RMS de referência. |
| Warp, Raw Integer | `5,16e-4` | `1,61e-2` | RMSE 0,27% do RMS de referência. |
| Ping-Pong, feedback alto | `3,24e-2` | `2,98e-1` | A cauda sustentada diverge em forma de onda. |
| Tape com ruído, dropout e modulação, 60 s | `2,279e-1` | `1,410` | RMS de saída 0,364/0,365. A cauda sustentada diverge apesar do nível semelhante. |

Os cenários medidos chegaram ao fim com saída finita. Nas regiões de realimentação alta, as diferenças de arredondamento nas recorrências e no disparo não linear podem acumular e fazer os sinais divergir. O controle Native de 60 s apresenta RMSE de 0,0129 durante a entrada e 0,1336 na cauda entre 10 e 60 s. A banda de 300 Hz a 1 kHz caiu 3,44 dB nessa cauda, medida por Welch. O pacote A/B inclui o sinal original, a versão de nível compensado e as métricas por banda. A decisão sobre equivalência musical fica com o revisor.

A compilação estrita de controle permaneceu sob a meta de P99 nas janelas curtas de desempenho, ao custo de cerca de 31,45 µs adicionais por bloco de cadeia completa em média. Se a escuta identificar uma perda musical, repetir o ensaio sustentado com build estrito ou aplicar as flags agressivas apenas a módulos de custo comprovado são opções verificáveis. A PR mantém as flags de produção durante a revisão perceptual.

## Saída de áudio

O limitador interno do TapeCore tem joelho em `±0,90` e limite em `±0,99`. Frippertronics e Bubbles passam pela proteção `tanhf` do caminho alternativo. O `tanhf` também continua no caminho de MP3. Para saída do TapeCore sem Freeverb, a escala máxima de PCM é 90% de full scale e o pico teórico é 89,1%.

Com Freeverb, a soma wet/dry vem depois do limitador. A sonda Xtensa alimentou o TapeCore e o Freeverb com seno estéreo de 55 Hz, amplitude 0,99, Drive 100 e parâmetros padrão do Freeverb. O pico antes do PCM foi 1,573513 após a retirada do `tanhf`, contra 1,233776 no controle com `tanhf`. Com volume mestre máximo e escala de 0,90, houve respectivamente 116.997 e 2.357 amostras fora da faixa de conversão no sinal de seis segundos. O teste isola os estágios TapeCore, Freeverb e PCM, sem captura analógica. A conversão anterior fora da faixa de `int32_t` tinha comportamento indefinido em C++. A correção satura explicitamente na borda PCM e mantém a conversão por truncamento no intervalo representável. O teste Native cobre os limites positivos e negativos, finitos e não finitos. Um limitador dinâmico alteraria ganho e envelope e exigiria outro A/B e benchmark.

A sonda repetiu o ensaio com a correção em 55, 110, 220, 440 e 997 Hz. Todas as amostras positivas e negativas fora da faixa foram convertidas para o extremo correto. O sinal ainda atinge o limite PCM nessa condição extrema. A sonoridade do Drive com Freeverb faz parte da escuta.

Na mesma bancada, destruir instâncias de Freeverb abortava em `tlsf_free`. Tanto o engine quanto seus filtros liberavam os buffers. A correção dá posse única aos filtros e descarta a instância de firmware quando a inicialização falha. Os cinco ciclos de construção e teardown no ESP32-S3 passaram com integridade do heap. Um ensaio com injeção de falha em cada um dos 24 buffers cobriu o caminho com SRAM e com fallback para PSRAM. Os 50 casos de inicialização e teardown terminaram sem ponteiros vivos no alocador de teste.

A auditoria anterior encontrou pico de 0,9274 em 26 WAVs de TapeCore e custo de 311–340 µs por bloco para o estágio externo. No teste de curva em 440 Hz, o estágio acrescentou de 0,083% a 5,637% de THD com amplitudes de 0,10 a 0,90. No A/B novo, reaplicar a curva ao mesmo High Drive reduz o RMS em 1,216 dB. A análise perceptual de ataque, harmônicos e textura fica pendente no [registro de escuta](ACCIDENTAL_WARP_LISTENING_SIGNOFF.md). [Auditoria do Performance Rescue](ESP32_S3_PERFORMANCE_AUDIT.md)

## Revalidação física do baseline da PR (`d942a7b`)

ESP32-S3 N16R8 a 240 MHz, PSRAM de 8 MiB, sintetizador interno, 44,1 kHz estéreo e blocos de 128 frames. Orçamento de 2.902,49 µs. Build `FW Nov 15 2026 21:04:05`, PlatformIO 6.1.18, plataforma Espressif 55.3.36, Arduino 3.3.6 e GCC Xtensa 14.2.0. O esptool verificou o digest da aplicação em flash no endereço `0x10000`.

- BIN SHA-256 `5fa33a048422f753218e39292899fc02a28d2a6c2094b20e0afb73af3a0b8576`.
- ELF SHA-256 `13c076d02787249b9eb58e5445fc278c9e9f3184684e01a498df01dd0323deb4`.
- ELF com 86.687 B em `.iram0.text`, 1.028 B em vetores, 15.888 B de dados e 10.016 B de BSS em DRAM. A RAM estática informada pelo PlatformIO é 25.904 B e o uso de flash é 588.767 B. `hydra_dsp_process`, `TapeCore::processStereoBlock`, `flushDenormals` e `FreeverbEngine::processStereoBlock` estão em IRAM.

| Cenário | Blocos | Média (µs) | P95 (µs) | P99 (µs) | Pico (µs) | Overruns / I2S |
|---|---:|---:|---:|---:|---:|---:|
| Tape Default | 1.024 | 1.676,27 | 1.716 | 1.724 | 1.885 | 0 / 0 |
| Warp Feedback 50% | 1.024 | 1.605,21 | 1.663 | 1.672 | 1.835 | 0 / 0 |
| Warp Feedback 95% | 1.024 | 1.624,57 | 1.672 | 1.717 | 1.886 | 0 / 0 |
| Cadeia completa | 1.024 | 1.792,72 | 1.858 | 1.884 | 2.085 | 0 / 0 |
| Cadeia completa, 300,002 s | 103.360 | 1.771,57 | 1.848 | 1.860 | 2.117 | 0 / 0 |

Houve 256 blocos de aquecimento por janela. A cadeia completa sustentada consumiu 64,08% do orçamento no P99 e 72,94% no pico. Heap interno de 128.284 B e PSRAM livre de 3.830.424 B ficaram estáveis nessa janela. A tarefa completou os cinco minutos sem resets observados. Os buffers DSP ficam pré-alocados e o processamento usa a stack e os buffers existentes. O histograma também é pré-alocado. A telemetria de DSP permanece ativa por bloco e `stageprof` e `membench` têm custo adicional quando acionados.

O firmware confirmou no log o modo dual-tap Hermite, Grain 80 ms, Speed 1, Stereo 0,5 e feedback de 50% e 95% nas janelas correspondentes. A atualização de parâmetros é medida fora do cronômetro DSP. O maior tempo acumulado durante a configuração entre janelas foi 20.608 µs, com `mod 1` habilitando a limpeza já existente dos dois buffers de delay. Os zeros nos contadores demonstram a operação em regime nas janelas e as escritas aceitas pela API I2S. O ensaio não capturou continuidade no conversor durante a troca de modo. O log bruto e os comandos estão em `artifacts/release-gate/bench-head.log` e `bench_gate.py`.

Um build de controle que trocou apenas `-ffast-math` por `-fno-fast-math` manteve o `recip0.s`, IRAM e `-O2`. Na cadeia completa de 1.024 blocos, a média passou de 1.792,72 para 1.824,17 µs (+1,75%) e o P99 de 1.884 para 1.923 µs (66,25% do orçamento). Uma segunda janela de 1.024 blocos teve P99 de 1.949 µs e pico de 2.092 µs. Todos os cenários curtos desse build tiveram zero overruns e zero falhas I2S. Isso mostra que há margem para avaliar flags por módulo. A configuração seletiva e a variante estrita por cinco minutos não foram medidas.

## Revalidação do firmware corrigido (`b9fbbd3`)

Build `FW Nov 15 2026 21:41:19` gravado e verificado na mesma placa. A RAM estática permaneceu em 25.904 B, as seções IRAM e DRAM do ELF mantiveram os tamanhos do baseline, e o PlatformIO informou 589.015 B de flash. Preservados o clock, as flags de produção, os parâmetros de DSP e o roteiro de bancada.

- BIN SHA-256 `046adf92172d521c4c33541719f52e77b39e23ca361c372ec149aaa61aa70ad0`.
- ELF SHA-256 `0b1e94ed017920cdbc566e32cadc0c33b31ee3675fce375735ca4ac4ca7ba559`.

| Cenário | Blocos | Média (µs) | P95 (µs) | P99 (µs) | Pico (µs) | Overruns / I2S |
|---|---:|---:|---:|---:|---:|---:|
| Tape Default | 1.024 | 1.700,95 | 1.733 | 1.792 | 1.965 | 0 / 0 |
| Warp Feedback 50% | 1.024 | 1.601,43 | 1.670 | 1.680 | 1.842 | 0 / 0 |
| Warp Feedback 95% | 1.024 | 1.627,83 | 1.681 | 1.713 | 1.914 | 0 / 0 |
| Cadeia completa | 1.024 | 1.808,69 | 1.866 | 1.891 | 2.047 | 0 / 0 |
| Cadeia completa, 299,998 s | 103.360 | 1.786,89 | 1.863 | 1.875 | 2.155 | 0 / 0 |
| Grain 400 ms, Speed 0 | 1.024 | 1.776,18 | 1.809 | 1.825 | 2.032 | 0 / 0 |
| Grain 20 ms, Speed 2 | 1.024 | 1.779,92 | 1.797 | 1.812 | 2.005 | 0 / 0 |

No ensaio sustentado, o P99 ocupou 64,60% do orçamento, com 1.027,49 µs de folga. O heap interno ficou em 128.180 B e a PSRAM livre em 3.830.424 B durante a janela. A tarefa completou os ensaios sem reset observado. Depois dos cinco minutos, a CLI confirmou os extremos de Grain e Speed com Warp e Freeverb ativos. O log bruto está em `artifacts/release-gate/bench-final.log`.

**Parecer: `READY FOR HUMAN REVIEW`.** A validação funcional do Warp, a matemática de tempo real, o build e as correções de PCM e Freeverb passaram nos ensaios descritos. O responsável pelo merge deve decidir a exceção da equivalência Native/WASM. O revisor de áudio deve registrar a escuta, com atenção ao Drive pós-Freeverb e à cauda tradicional com feedback alto.
