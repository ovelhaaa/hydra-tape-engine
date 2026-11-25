# Estimativa de custo — F4 (não é benchmark físico)

Alvo: **ESP32-S3 N16R8 @ 240 MHz, estéreo 44.100 Hz, 128 frames,
deadline 2.902,49 µs**. Este documento registra apenas indicadores de desktop
e o orçamento estático. **Nenhuma medição física foi executada:
`PENDING_PHYSICAL`.**

## Indicadores Native

| Item | F4=0 | F4=1 | Δ |
|---|---:|---:|---:|
| `.text` do objeto `tape_core.cpp` | 84.160 B | 85.960 B | +1.800 B (+2,1%) |
| `.data` / `.bss` do objeto | 0 / 0 | 0 / 0 | 0 |
| Estado novo no `Impl` | — | ~64 B | +64 B (heap, por instância) |
| Buffers de áudio | 2 × history | 2 × history | **0** |
| Leituras de fita por sample | máscara | máscara | 0 (pior caso já era 3) |

O objeto foi compilado com a mesma toolchain e flags
(`-O3 -DNDEBUG -std=c++17 -ffp-contract=off`, Classic C=1/F2=2/F3=0).

## Custo por sample

- Estável: 1 teste de bool (`f4TransitionActive`) + as mesmas leituras e
  multiplicações do original. Nenhuma normalização por sample.
- Em transição: até 3 passos de ganho, 3 multiplicações, 2 somas e 1 divisão
  por sample (~20–30 ciclos estimados no LX7), por no máximo:
  - 441 samples (10 ms) para cabeçotes, mix e drive;
  - 529 samples (12 ms) para o release do Freeze.
- Não há operação transcendental nova, alocação, lock, fila ou memset.

## Cenários a medir na placa

Comparar o mesmo firmware/flags/cenário com a Vintage F4=0:

1. Head 3→1+2+3 e quatro trocas rápidas, com e sem áudio de entrada.
2. Freeze ON/OFF e ON com feedback 95.
3. Mix 0→100→0 e Drive 0→100→25.
4. Rajada de 20 comandos em 200 ms.
5. Operação normal após as transições (30 s).

Registrar DSP P50/P95/P99/peak, `fullWorkUs`, memória/stack, I2S, overruns e
comandos enviados sob carga. A Classic C já usa parcela significativa do
deadline; qualquer pico novo precisa ser limitado e comprovado. Nenhum número
deste documento deve ser convertido em estimativa de tempo físico.

## Quando a F4 pode ser considerada sem risco de CPU

- Nenhum pico de transição acima do P99 atual da Classic C na mesma placa;
- zero overrun/I2S fail nos cenários 1–4;
- memória estática e PSRAM sem crescimento além dos ~64 B de estado;
- operação normal idêntica ao F4=0 dentro da variação medida.

Até lá: **PENDING_PHYSICAL**, sem promoção.
