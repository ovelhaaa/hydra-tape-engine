# Implementação F4

## Flag e escopo

`dsp/core/performance_transitions_experiment.hpp`:

```cpp
#ifndef HYDRA_DSP_REFORM_F4_TRANSITIONS
#define HYDRA_DSP_REFORM_F4_TRANSITIONS 0
#endif
```

- `0` (default): Vintage F3-A, sem nenhuma expressão F4 compilada; o fonte
  mantém os mesmos caminhos da F3.
- `1`: habilita as rampas; exige `Classic C=1`, `F2=2`, `F3=0` (erro de
  compilação caso contrário).
- Constantes: head 10 ms; mix 10 ms; drive 10 ms; release do Freeze 12 ms;
  epsilon de normalização 1e-6.

Nenhum ID, nome, faixa, default de preset, estrutura ABI ou versão de API foi
tocado. `activeHeads` continua 1..7 e `p_mix` continua alimentando `dryWet` e
`delayWet` no host (`src/main.cpp:274–276`).

## Mapa do código

| Arquivo | Mudança |
|---|---|
| `dsp/core/performance_transitions_experiment.hpp` | novo; flag, guardas e constantes |
| `dsp/core/tape_core.cpp` | estado `Impl`, rampas por sample, leitura por ganhos, release do Freeze |
| `CMakeLists.txt` | alvo `hydra_f4_transitions_test`, sempre compilado com F4=1 |
| `platformio.ini` | novo `env:esp32-s3-f4` (extensão do Classic C, sem promover default) |
| `tests/core/f4_transitions_test.cpp` | contrato de bloco, convergência de Mix, bordas de Head/Freeze |

### Estado adicionado (`Impl`)

```text
headGain1..3, headTarget1..3, headNormInv, headGainStep
headTransition, headPrimed, f4TransitionActive
driveGainTarget, textureWetTarget, delayWetTarget
driveGainInc, textureWetInc, delayWetInc
freezeReleaseInc
```

Aproximadamente 64 bytes, uma vez por `TapeCore` (heap). Nenhum buffer novo.

### Atualização dos alvos

`updateCachedParams()` passou a calcular valores-alvo e, quando o motor já
processou, a armar incrementos de 10 ms em vez de saltar:

- Mix: `textureWetTarget`/`delayWetTarget`; `dryGain = 1 − textureWetGain` é
  recalculado durante a rampa e volta exatamente ao valor original no fim.
- Drive: `driveGainTarget = drive·0,05`.
- Cabeçotes: alvos 0/1 por máscara; `headNormInv = 1/Σníveis·ganhos`.
- Freeze: o release arma `freezeReleaseInc`; durante `freeze` o ganho é 0.

`updatePerformanceTransitions()` (`:311–350`) é chamado por sample **somente**
quando `f4TransitionActive` é verdadeiro (`:1183`). A função caminha os ganhos,
fixa os valores exatamente ao atingir o alvo, recalcula a normalização de
cabeçotes e desarma a flag quando tudo estabilizou.

### Leitura de cabeçotes

`readCh` (`:1248–1262`) usa os ganhos correntes:

```cpp
if (g1 > 0) delaySig += read1 * (1.00f * g1);
if (g2 > 0) delaySig += read2 * (0.75f * g2);
if (g3 > 0) delaySig += read3 * (0.55f * g3);
delaySig *= headNormInv;
```

Com `g=1` os produtos são bit-idênticos aos do original; com `g=0` o tap não é
lido. Durante a transição, no máximo os mesmos três taps do modo multi-head
estático são lidos; não existem leituras adicionais além do pior caso já
existente.

### Escrita / Freeze

`writeCh` (`:1379–1384`) mantém o gate `if (!freeze)` e multiplica `recSig`
pelo ganho de release apenas quando ele é < 1. Com o Freeze inativo e
estabilizado, o valor é 1 e a expressão `mag.process(recSig, allowBias)` é a
original.

## Contrato de preservação

- **F4=0**: nenhuma mudança de comportamento; 10/10 bit-exact com o manifesto
  F3-A, hashes de entrada e saída conferidos, e a expressão original preservada
  atrás do `#else`.
- **F4=1 estático**: 10/10 bit-exact com F4=0 nos cenários F0.2; controles sem
  automação bit-exact.
- **F4=1 após transição**: configurações estabilizadas recuperam os ganhos
  exatos. Mix converge bit-exact ao F4=0 após 100 ms (três cenas de um
  cabeçote); com três cabeçotes o resíduo máximo observado é `1,25e-5`
  (−98 dBFS), atribuído à memória dos filtros de playback, não a ganho.
- Tone: F4=0 = F4=1 bit-exact; o `pbTransition` existente não foi duplicado.

## Custo

- Native `.text`: 84.160 → 85.960 bytes (+1.800, +2,1%); data/bss 0.
- Estado: ~64 bytes no `Impl`.
- Hot path estável: um teste de bool por sample (F4=1); nenhum cálculo extra
  de normalização.
- Hot path em transição: ~20–30 ciclos/sample por ~441 samples (10 ms); pico
  limitado e não recorrente.
- Nenhuma alocação, lock, thread, fila, timer ou operação transcendental nova.

## Rollback

Duas camadas independentes:

1. compilar com `HYDRA_DSP_REFORM_F4_TRANSITIONS=0` (default) — nenhum código
   F4 é gerado;
2. reverter o commit/branch experimental, sem tocar em F0.2/F1/F2/F3.

O alvo de teste é independente e não altera a `main`. A promoção do default
continua exigindo escuta humana, campanha física e decisão explícita.
