# Estabilidade e clipping F3

Matriz local completa: **154/154 casos por candidata**,44.1/48kHz;
60 caudas60s/candidata, além de4 casos60s Multi-Head+Ping-Pong/candidata.
Suíte canônica:27/27/candidata. Finitude, estados pré-clamp e decay aprovados
com limites F2 originais. Instrumentação L/R bit-exact com build sem diagnóstico.
O pacote CI também passou todas as candidatas; JSONs completos no artifact.

| Guitarra full-level/fast/new/bump100/FB100 | A Vintage | B Smooth | C Bloom |
|---|---:|---:|---:|
| Teto durante entrada,44.100Hz | 38 | 38 | 30 |
| Teto durante entrada,48.000Hz | 17 | 17 | 13 |
| Teto após entrada, todos os casos | 0 | 0 | 0 |
| Pico record antes do clamp, máximo da matriz | 1.3279314 | 1.3276088 | 1.3207705 |

Contagens somam amostras L/R, threshold .9899. A reproduz os38/17 da Vintage
F2 congelada. B não aumenta o clipping neste conjunto; C reduz8/4 samples.
Ainda há clipping ativo em todas as variantes. Não foi mascarado por ganho
ou limiter novo, e a contagem não prova melhoria musical nem ausência de
intersample peaks. Headroom interno permanece bem abaixo do clamp±4;
estes picos de sinal não são medição de heap/stack ou dos estados de cada filtro.

Zero clipping persistente, NaN/Inf ou crescimento não controlado na matriz.
Caudas: crescimento RMS final≤.05dB, pico tardio/intermediário≤1.02 e gate
interno correspondente; regras históricas preservadas. As mesmas entradas,
parâmetros, seeds e lifecycle são usadas A/B/C. Fontes e casos não foram
atenuados para passar. Nenhum ganho/limiter de saída alterado.

`validation-summary.json` resume os gates; `variant*/stress.json`, `compound.json`,
`engine/engine-gates.json` e `dynamics.json` no ZIP guardam cada medição.
Matriz dinâmica576 cenas e true peak4x são complementares, não substituem
estabilidade60s. A comparação harmônica não usa loudness como ranking.

ESP32-S3: **PENDING_PHYSICAL**. Builds experimentais A/B/C passaram localmente
e na CI, sem flash. COM15 foi enumerada; isso não estabelece identidade/configuração
de uma campanha comparável. Não há nova medida DSP P50/P95/P99/peak, full-work,
stack, heap/PSRAM ou I2S/overruns para F3. Antes de promoção medir na mesma
placa240MHz/44100Hz/128/estéreo, deadline2902.49µs, arquivar flags/binários/logs
e comparar A/B/C. Histórico C não é aprovação física da F3.
