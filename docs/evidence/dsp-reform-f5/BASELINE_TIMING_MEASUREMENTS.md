# F5-A — Baseline timing characterisation (Vintage F3-A)

**Reference:** F3 `29f50258…`, Classic C=1 / F2=2 / F3=0 / F5=0.  
**Method:** read-only diagnostics added in `dsp/core/f5_timing_hooks.hpp`, compiled behind
`HYDRA_DSP_TEST_HOOKS`. The hook build is proven output-identical to the plain build
(`INSTRUMENTATION_EQUIVALENCE.json`: bit-exact on every scene). All numbers below are from
renders at 44.1 kHz, block 128, fresh `create_v1_4(44100,2000,8000)` lifecycle.

## 1. What the baseline does

The musical head delays are an instantaneous function of BPM:

```
tape_core.cpp:  musicalHeadDelay1 = beatMs*0.333f*fs*0.001f
                musicalHeadDelay2 = beatMs*0.75f *fs*0.001f
                musicalHeadDelay3 = beatMs      *fs*0.001f
```

There is no smoothing state for them. `smoothedDelaySamples` (one-pole, ~200 ms) only
feeds the millisecond path and the modulation depth. Tap, MIDI and `transport_set_bpm`
update `transportClock` and `currentParams.bpm`, but the head values are recomputed only
inside `updateCachedParams()`, i.e. on a parameter commit.

## 2. Measured defects (A2)

| Gesto | Caminho | Efeito medido | Unidade |
|---|---|---|---|
| BPM 120→100 | parâmetro + commit | cabeçote 3 salta 4.410 amostras no mesmo frame | 100,0 ms |
| BPM 120→100 | só transporte | transporte em 100 BPM; cabeçote fica em 22.050 para sempre até outro commit | 4.410 amostras (100 ms) |
| BPM 120→125 via MIDI | só transporte | erro final de 882 amostras após 6 s | 20,0 ms |
| Tap 120→90 | tap tempo | salto de 7.350 amostras | 166,7 ms |
| BPM 160→80 | parâmetro | salto de 11.025 amostras | 250,0 ms |
| delay 500→600 ms | parâmetro | velocidade inicial 0,50 amostra/amostra; ~−1.200 cents no primeiro instante | 1 oitava |
| delay 500→600 ms | parâmetro | one-pole estaciona ~8,6 amostras antes do alvo (resolução de float em ~26.460) | ≈0,2 ms |

O salto de endereço não é um glide de capstan: é uma descontinuidade de leitura. O caso
"só transporte" é pior porque a referência musical (GTC) e o delay divergem de forma
persistente. Nenhum dos dois comportamentos é intencional no código; ambos vêm de a mesma
verdade temporal não alimentar os três cabeçotes.

### Verificação reproduzível

```
python docs/evidence/dsp-reform-f5/run.py measure
```

`TIMING_MEASUREMENTS.json`, cenas `manual_*`, `ms_*`, `midi_*`, campo `metrics` de cada
variante. Os valores de A estão na chave `variants.0`.

## 3. O que já estava correto (patrimônio preservado)

- O Global Transport Clock preserva fase ao trocar BPM e não deriva: erro de posição
  ≤1e-9 beat no teste de contrato e no drift de 60 s.
- Tap Tempo (debounce 80 ms, timeout 3 s, outlier isolado, confirmação de dois taps) e
  MIDI Clock (24 PPQN, perda de 1/2 pulsos, holdover, reaquisição, Start/Stop/Continue)
  cumprem seus contratos; a F5 não os altera.
- Reverse V2 reconfigura as janelas com o BPM exato do GTC, não com o glide. Em 120→90
  com divisão de 1/4, `requested = effective = 666,67 ms`, `clamped = 0`.
- F5=0 é bit-exact com o manifesto F3-A em 10/10 cenários (`F5-A-reference.json`).

## 4. Conclusão da caracterização

O problema temporal da F5 é de **política de alvo e continuidade de leitura**, não de
clock. O GTC e os contratos de Tap/MIDI/Reverse são confiáveis; os cabeçotes musicais
precisam de um estado de glide próprio, e o alvo precisa acompanhar o GTC mesmo quando só
o transporte muda. Isso é exatamente o que as variantes B/C implementam sem tocar no GTC.
