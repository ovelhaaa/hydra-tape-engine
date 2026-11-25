# F5 — Arquitetura temporal e critérios pré-registrados

**Branch:** `experiment/dsp-reform-f5-musical-timing`  
**Base:** F3 `29f50258f1ac47139ac8f1fdb21aa59424ca9a75` (PR #68), sem integrar a F4/#69.  
**Flag:** `HYDRA_DSP_REFORM_F5_TIMING_VARIANT=0|1|2` (default **0**).  
**Referência:** Classic C=1, F2=2, F3=0, F4 ausente/0. F5=0 é bit-exact com a Vintage F3-A.

## 1. Camadas de tempo (separação obrigatória)

O Hydra já possui um Global Transport Clock (GTC) correto e baseado em amostras. A F5 **não**
suaviza o GTC e não cria outra fonte de BPM. A política temporal é separada em cinco
camadas, cada uma com um único dono:

| Camada | Estado / dono | O que representa |
|---|---|---|
| 1. Tempo solicitado | `manualBpm`, `tapTempo`, `midiClockSync` | Comando do usuário / estimativa da fonte |
| 2. Tempo efetivo da fonte | `applyEffectiveBpm()` → `transportClock.setBpm()` | BPM aplicado, fase preservada |
| 3. Posição musical global | `GlobalTransportClock` | Verdade musical; nunca suavizada |
| 4. Alvo de cada cabeçote | `musicalHeadDelay1/2/3` (musical) ou `delayTimeMs` (ms) | Distância de leitura desejada |
| 5. Posição de leitura suavizada | `smoothedDelaySamples` (ms) / glide musical | O que o áudio lê durante a transição |

O ponto central: **a camada 5 pode se mover, a camada 3 não**. Tap, MIDI, Reverse V2,
transporte e telemetria continuam lendo a camada 3. O smoothing sonoro não introduz
deriva porque nunca realimenta o GTC.

### Por que a F5 existe (A2)

Em `F5=0`, `headsMusical=1` usa diretamente os valores derivados do BPM:

```
dsp/core/tape_core.cpp:  musicalHeadDelay1 = beatMs*0.333f*fs*0.001f
                         musicalHeadDelay2 = beatMs*0.75f *fs*0.001f
                         musicalHeadDelay3 = beatMs      *fs*0.001f
```

Não há estado suavizado para esses três valores. Consequências medidas:

1. Uma mudança de BPM por parâmetro (`HYDRA_DSP_PARAM_BPM` + commit) troca o endereço de
   leitura dos três cabeçotes **no mesmo frame**. Em 120→100 BPM o cabeçote 3 salta 4.410
   amostras (100 ms) instantaneamente: descontinuidade, não glide.
2. Uma mudança só pelo transporte (`hydra_dsp_transport_set_bpm`, o caminho usado pelo
   MIDI externo e pela UI web) atualiza o GTC mas **não** recalcula os cabeçotes até o
   próximo commit de parâmetros. O delay fica permanentemente fora do tempo (4.410
   amostras em 120→100; 882 amostras em 120→125 via MIDI) enquanto nenhum outro parâmetro
   for tocado.
3. Em modo milissegundos o one-pole existente (`delaySmoothAlpha`, ~200 ms) é aplicado a
   `smoothedDelaySamples`; uma mudança de 500→600 ms produz velocidade inicial de
   ~0,5 amostra/amostra, ou seja, ~1.200 cents (uma oitava abaixo) no primeiro instante.

Esses três comportamentos são o alvo da F5. O caráter de fita em modo ms é preservado; o
que muda é a política de alvo e a continuidade da leitura.

## 2. Variantes

### F5-A (`=0`) — Vintage Timing Reference

Nenhuma expressão, estado ou ramo novo entra na compilação. O GTC, `smoothedDelaySamples`,
os três cabeçotes, Reverse V2 e o feedback são exatamente a F3-A. É o controle bit-exact.

### F5-B (`=1`) — Musical Capstan Glide

Um único estado de glide serve os três cabeçotes musicais, pois suas razões são fixas
(0,333 : 0,75 : 1). O estado é mantido como **erro restante em amostras/beat**, não como
posição absoluta: acumular um passo sub-amostra em ~22.050 amostras trava no ulp de float
(~8 amostras), enquanto acumular o erro permanece exato até o bit.

- Alvo musical: `samplesPerBeat = 60000/bpm * fs/1000`.
- Atualização por amostra: `err -= clamp(alpha*err, ±maxPitch)`; `alpha` é o mesmo
  `delaySmoothAlpha` de ~200 ms já aprovado.
- `maxPitch = 0,12` amostra/amostra no cabeçote 3 (~208 cents), reduzindo para os
  cabeçotes 1/2 pela razão de cada um.
- Dead-band de 0,05 amostra: quando o erro restante é menor, o estado "aterra" exatamente
  no alvo da Vintage (sem vibrato residual).
- Modo ms reutiliza `smoothedDelaySamples` e o mesmo `alpha`, adicionando apenas o limite
  de velocidade; a expressão original continua no `#else` de F5=0.
- Ao entrar no modo musical, reativar o delay ou resetar, o estado é "primed" para o alvo
  exato da Vintage: cenas estáticas permanecem bit-exact.

### F5-C (`=2`) — Clock-Focused Timing

Mesma máquina de glide, com política dependente da fonte e do **episódio** de clock:

| Episódio | tau | maxPitch | dead-band |
|---|---|---|---|
| Internal ou modo ms | 0,200 s (existente) | 0,120 | 0,05 amostra |
| Micro: ≤1% de um beat (jitter/fase) | 0,200 s (capstan) | 0,120 | 0,05 amostra |
| Pequeno: 1–5% de um beat (ex.: 5 BPM) | 0,030 s | 0,050 (~85 cents) | 0,05 amostra |
| Grande: >5% de um beat | 0,150 s | 0,085 (~145 cents) | 0,05 amostra |

A classificação usa o deslocamento **cumulativo** desde o último alvo estável, não o
incremento de cada pulso: o MIDI aplica uma mudança grande como uma rampa de passos
pequenos, e um episódio de jitter precisa permanecer classificado como micro. O episódio
termina quando o glide chega ao alvo.

Um dead-band de **2 ms** no alvo musical ignora o “respirar” sub-milissegundo do PLL;
uma mudança real de BPM fica muito acima disso. Na chegada, o estado é marcado estável
sem mover a cabeça de leitura — um snap de meia amostra seria, ele próprio, um blip de
~700 cents por uma amostra. Para deslocamentos grandes a política é **mais lenta e mais
suave** que a B; a limitação de leitor único é documentada em `CLOCK_FOCUSED_TIMING.md`,
não mascarada com um pitch dive.

## 3. Custo

- Estados novos apenas em F5≠0: 11 floats + 2 bool ≈ **52 bytes** no `Impl` (heap), sem
  buffer, sem alocação na audio task.
- Por amostra em regime estacionário: um teste de `stable` e um branch (F5-B) ou dois
  compares de `hold` (F5-C). Durante o glide: 1 subtração, 1 multiplicação, 2 compares,
  1 soma, 3 multiplicações/escritas de cabeçote. Nenhuma transcendente por amostra.
- Constantes de política calculadas em `updateSampleRateConstants()` (uma vez).
- Nenhum leitor extra, nenhum segundo TapeCore, nenhum lock/thread.

## 4. Critérios de aceitação pré-registrados

Fixados **antes** da calibração final e antes de observar o pacote de escuta. Não são
reajustados para acomodar resultados.

1. **Bit-exact:** F5=0 reproduz a F3-A 10/10 nos cenários F0.2; F5=1 e F5=2 reproduzem
   F5=0 10/10 nos mesmos cenários e nos controles sem comando.
2. **Transporte:** após qualquer evento, `transportClock.bpm()` é exatamente o BPM
   solicitado no frame do evento; a posição musical é contínua (erro ≤ 1e-9 beat) e não
   deriva (erro ≤ 1e-9 beat em 60 s).
3. **Alvos coerentes:** os três cabeçotes mantêm as razões 0,333/0,75/1,0 com erro
   ≤ 4e-3 amostra durante todo o glide; o alvo acompanha o GTC mesmo em mudança só de
   transporte.
4. **Continuidade:** em F5=1/2, `|Δdelay|` por amostra ≤ `maxPitch + ulp` (≤0,125); sem
   overshoot > 0,1 amostra; reversão monotônica em direção ao alvo.
5. **Convergência:** 120→100 musical atinge ≤1 ms em ≤1,5 s e ≤1 amostra em ≤3,5 s.
6. **Pitch:** F5-B ≤ 230 cents no pior gesto manual; F5-C ≤ 90 cents em correções
   pequenas de clock e ≤ 160 cents em mudanças grandes; nenhuma candidata pode exceder a
   outra no mesmo cenário pequeno.
7. **Clock-focused:** para degraus ≤5 BPM vindos de Tap/MIDI, C converge a ≤1 ms em menos
   tempo que B; para degraus grandes, C pode ser mais lenta e isso é registrado como
   troca deliberada.
8. **Tap/MIDI:** contratos de debounce (80 ms), timeout (3 s), outlier, dois taps,
   PPQN=24, perda de 1/2 pulsos, holdover e reaquisição preservados; `midi_status`
   alcança Locked; `appliedBpm` final dentro de ±1% do alvo.
9. **Reverse V2:** `effectiveMs` acompanha `requestedMs` (≤0,5 ms ou clamped declarado)
   em mudança de BPM e de divisão; sem NaN/Inf; o GTC de Reverse continua o mesmo.
10. **Blocos:** 1/37/64/128/256 com o evento no mesmo frame absoluto → `max_abs ≤ 1e-6`
    (esperado 0) em cada variante.
11. **Paridade:** Native/WASM `max_abs ≤ 1e-5`, `RMSE ≤ 1e-6`; F5 v0/v1/v2 passam no nativo
    e no WASM; goldens e ABI 1.7/38 preservados.
12. **Estabilidade:** caudas de 60 s com FB95 e automação de BPM sem crescimento
    descontrolado, sem NaN/Inf e sem teto do limiter.
13. **Instrumentação:** a biblioteca com hooks de diagnóstico produz exatamente o mesmo
    áudio que a sem hooks (hash idêntico).
14. **Custo:** estado ≤64 bytes; `.text` adicional estimado ≤2 KB; sem alocação nova na
    audio task. Nenhuma aprovação física é inferida de benchmark desktop:
    `PENDING_PHYSICAL`.

## 5. Não objetivos desta fase

- Não implementar dois leitores completos nem crossfade estrutural de memória (F7).
- Não introduzir seletor público de modo temporal nem mudar a ABI 1.7/38.
- Não alterar presets, goldens, pesos/topologia dos cabeçotes, feedback, saturação ou LUT.
- Não suavizar o GTC nem criar fonte de BPM paralela.
- Não integrar a F4/#69 nem a F2/F3 experimentais.
