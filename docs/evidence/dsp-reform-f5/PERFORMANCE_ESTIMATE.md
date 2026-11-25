# F5 — Estimativa e medição de custo

**Sem aprovação física.** Os números abaixo são compilação e medição desktop; a campanha
na placa ESP32-S3 continua `PENDING_PHYSICAL`.

## 1. Estado e código

| Item | F5=0 | F5=1 | F5=2 |
|---|---|---|---|
| `tape_core.o` `.text` (MinGW g++ 14.2, `-O3 -DNDEBUG`) | 84.160 B | 87.224 B (+3.064) | 87.384 B (+3.224) |
| `.data` / `.bss` do objeto | 0 / 0 | 0 / 0 | 0 / 0 |
| Firmware ESP32-S3 `firmware.bin` (build CI local) | 633.664 B | 634.640 B (+976) | 634.800 B (+1.136) |
| RAM estática do firmware (linker) | 26.368 B | 26.368 B | 26.368 B |
| Estados novos no `Impl` | 0 | 56 B | 56 B |

Contagem dos estados: 13 `float` (52 B) + `uint8`/3 `bool` (4 B) = 56 B. Não há
buffer, alocação, thread, lock ou leitor extra. O firmware usa 3,8% de 16 MB de flash;
o delta de 1 KB não é restrição de memória.

O objeto isolado cresce mais que o firmware porque `stepTimingGlide` é inlined no loop de
amostras e a seleção de política de F5=2 adiciona dois caminhos. A estimativa preliminar
era ≤2 KB de `.text`; a medição corrigiu para ~3,1–3,2 KB no objeto e ~1 KB no firmware
linkado. A diferença está registrada, não escondida; nenhum critério musical foi movido.

## 2. Custo por amostra

- **Regime estacionário (F5-B):** um `if (timingBeatStable) return;` — 1 branch.
- **Regime estacionário (F5-C):** 1 branch + 2 comparações de `hold`.
- **Durante o glide musical:** subtração do erro, multiplicação por `alpha`, 2 compares de
  clamp, subtração, 1 subtração de reconstrução, 3 multiplicações/escritas de cabeçote.
  Sem `exp`, `log`, `sin`, divisão ou acesso a memória além do `Impl` (já quente).
- **Modo ms:** o one-pole aprovado + clamp + comparação de precisão. Nenhuma
  transcendente.
- Constantes de política (`alpha`, caps, dead-bands) calculadas uma vez em
  `updateSampleRateConstants()`, fora do loop.

Nenhuma alocação é feita na audio task: o `Impl` é alocado em `create` (comportamento
existente). O hook de diagnóstico (`HYDRA_DSP_TEST_HOOKS`) não entra no firmware nem na
biblioteca de produção.

## 3. O que falta para aprovação física

Medir, na mesma placa e configuração:

- P50/P95/P99/peak de DSP e `fullWorkUs`, baseline F5=0 vs B/C;
- atualização de parâmetros sob rajada (Tap/MIDI e knobs);
- I2S, overruns, heap/PSRAM/stack;
- cenários multi-head, Reverse V2, FB95 e automação de BPM.

Até lá: **PENDING_PHYSICAL**. Benchmarks desktop e tamanho de firmware não são aprovação
de deadline.

## 4. Builds realizadas

```
pio run -e esp32-s3-f5-a -e esp32-s3-f5-b -e esp32-s3-f5-c
esp32-s3-f5-a SUCCESS
esp32-s3-f5-b SUCCESS
esp32-s3-f5-c SUCCESS
```

Build não é execução: nenhum firmware foi gravado nesta fase.
