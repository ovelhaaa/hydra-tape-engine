# F5-B — Musical Capstan Glide

**Flag:** `HYDRA_DSP_REFORM_F5_TIMING_VARIANT=1`. **F5=0 permanece o default e a
referência bit-exact.**

## 1. Hipótese

Uma mudança de tempo deve mover a cabeça de leitura como um capstan: velocidade
controlada, sem salto de endereço, com o pitch transitório proporcional ao gesto e
limitado. As três cabeças devem manter suas razões musicais durante todo o movimento
(0,333 : 0,75 : 1). O Global Transport Clock não participa do glide.

## 2. Implementação

- Um único estado de erro em amostras/beat (`timingBeatErrorSamples`); a posição é
  reconstruída como `alvo − erro`. Isso evita o travamento de ~8 amostras que ocorre ao
  acumular passos sub-amostra em um float de ~22.050.
- `alpha` é o `delaySmoothAlpha` aprovado (~200 ms); o passo é limitado a
  `maxPitch = 0,12` amostra/amostra no cabeçote 3 (≈208 cents). Cabeçotes 1/2 herdam a
  razão, então o pitch deles é proporcionalmente menor.
- Dead-band de 0,05 amostra: ao chegar, o estado “aterra” nos valores exatos da Vintage.
- Modo ms usa o mesmo one-pole existente + o mesmo limite; a expressão original fica no
  `#else` de F5=0.
- Ao entrar no modo musical, reativar o delay ou resetar, o estado é primado no alvo
  exato; cenas sem mudança de tempo continuam bit-exact.

## 3. Resultados medidos (44,1 kHz, bloco 128)

| Gesto | A (Vintage) | B (Capstan) |
|---|---|---|
| 120→100 musical | salto de 4.410 amostras (100 ms) | glide; 1 ms em 1,270 s; pico 223 cents |
| 100→120 musical | salto de 4.410 amostras | glide; 1,270 s; 198 cents |
| 120→90 musical | salto de 7.350 amostras (166,7 ms) | 1,825 s; 223 cents |
| 90→135 musical | salto de 2.450 amostras | 2,286 s; 198 cents |
| 80→160 musical | salto de 16.537 amostras (375 ms) | 3,558 s; 198 cents |
| 160→80 musical | salto de 16.537 amostras | 3,558 s; 223 cents |
| 500→600 ms (modo ms) | one-pole: 1.200 cents no 1º instante; trava 8,6 amostras | 219 cents; converge a 0,00 amostra |
| 600→500 ms | 702 cents; trava 8,6 amostras | 195 cents; 0,00 |
| Rampa 120→100 (8 passos) | degraus instantâneos | glide contínuo; 1,645 s até 1 ms |

O “salto de X amostras” de A não é um pitch: é uma descontinuidade de leitura. A métrica
de cents satura nesse caso (não deve ser lida como 23.000 cents literais). B nunca
ultrapassa 223 cents em nenhum gesto manual; o limite teórico é 208 cents e o excesso é
quantização de float na reconstrução da posição.

### Razões dos cabeçotes

Durante o glide, a maior diferença medida entre `head1` e `0,333 × head3` foi 0,0005
amostra, e entre `head2` e `0,75 × head3`, 0,002 amostra. As três cabeças permanecem na
mesma “fita” em todos os instantes.

### Continuidade e HF

O passo máximo de leitura por amostra é 0,121 (limite 0,12 + ulp). Não há overshoot: a
posição é monotônica em direção ao alvo e o estado chega exatamente a ele. O excesso de
HF em 5 ms após o comando fica dentro do controle (a linha de base já é rica em HF); os
números por cena estão em `TIMING_MEASUREMENTS.json`.

## 4. Custo

56 bytes de estado, sem buffer. Em regime estacionário, um branch. Durante o glide,
~8 operações escalares por amostra. `.text` +3.064 B no objeto; firmware +980 B. Nenhuma
transcendente por amostra.

## 5. O que a B não resolve

- Mudanças muito grandes levam segundos (80→160: 3,6 s até 1 ms). É o preço do limite de
  pitch com um leitor único.
- O modo ms fica mais lento que o one-pole original para chegar a 1 ms (1,27 s vs 0,92 s
  em 500→600), mas sem o dive de uma oitava e sem o travamento residual.
- Se a escuta preferir a resposta mais rápida da Vintage apesar do salto, F5-A permanece
  a escolha e a B deve ser rejeitada.
