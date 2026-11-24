# Medições de automação — F4

Dados completos: `artifacts/dsp-reform-f4/AUTOMATION_MEASUREMENTS.json`.
Bancada: `docs/evidence/dsp-reform-f4/f4bench.py` e `run.py measure`.
Entrada determinística, comandos na borda de bloco, 44.100 Hz, 128 frames.
Cada cena tem um render de controle com os mesmos parâmetros e **sem**
comando; as métricas de passo/HF são relativas a esse controle e ao máximo dos
50 ms anteriores.

## Cenas e comandos

| Cena | Fonte | Comandos |
|---|---|---|
| `mix_0_100_0` | bed harmônico | mix 0→100 em 1,501 s; 100→0 em 2,499 s |
| `mix_100_0` | bed | mix 100→0 em 1,501 s |
| `mix_25_75` | bed | mix 25→75 em 1,501 s |
| `drive_0_100_25` | bed | drive 0→100 em 1,501 s; 100→25 em 2,499 s |
| `drive_100_25_stored` | bed | drive 100→25 em 1,501 s |
| `head_3_to_123` | poly synth | máscara 4→7 em 1,501 s |
| `head_1_to_3` | poly synth | máscara 1→4 em 1,501 s |
| `head_12_to_23` | poly synth | máscara 3→6 em 1,501 s |
| `heads_rapid` | poly synth | 4→7→1→6→3 em 1,501/1,550/1,599/1,649 s |
| `freeze_on` | clean guitar | Freeze ON em 1,501 s |
| `freeze_on_off` | clean guitar | ON 1,501 s; OFF 2,499 s; delay 300 ms |
| `freeze_rapid` | clean guitar | 5 alternâncias entre 1,399 e 2,200 s |
| `freeze_high_fb` | clean guitar | fb 95; ON 1,501 s; OFF 2,499 s |
| `freeze_reverse_multi` | driven guitar | Reverse + multi-head; ON 1,501 s; OFF 3,001 s |
| `tone_0_100_0` | bed | tone 0→100 em 1,501 s; 100→0 em 2,499 s; fb 85 |
| `tone_100_0_fb95` | bed | tone 100→0 em 1,501 s; fb 95 |
| `reverse_head_switch` | poly synth | Reverse; máscara 4→7 e 7→1 |
| `multihead_mix` | bed | mask 7; mix 0→100 em 1,501 s |
| `control_no_automation` | bed | nenhum |
| `control_mix_no_automation` | bed | nenhum |

Frames exatos (múltiplos de 128) e parâmetros completos estão no JSON. O mesmo
frame é usado nas duas variantes.

## Resultados de borda (F4=0 → F4=1)

Passo = `|y[n]−y[n−1]|` entre os samples ao redor do comando. HF = RMS
high-pass 4 kHz em 5 ms, relativo ao controle (dB).

| Cena / evento | Passo borda | Passo 1 ms | HF vs controle |
|---|---:|---:|---:|
| mix 0→100 | 0,03964 → **0,00027** | 0,07851 → 0,01522 | +12,25 → +3,62 dB |
| mix 100→0 | 0,05238 → **0,01454** | 0,07418 → 0,03410 | −3,50 → −1,68 dB |
| mix 25→75 | 0,01554 → **0,00338** | 0,04044 → 0,01894 | +6,05 → +1,57 dB |
| multi-head + mix 0→100 | 0,03977 → **0,00027** | 0,07625 → 0,01436 | +6,90 → −0,25 dB |
| head 1→3 | 0,00858 → **0,00029** | 0,03800 → **0,00527** | **+9,72 → −1,12 dB** |
| head 1+2→2+3 | 0,00985 → **0,00390** | 0,03028 → **0,00421** | **+7,40 → −0,65 dB** |
| head 3→1+2+3 | 0,00498 → **0,00395** | 0,00824 → **0,00462** | +3,18 → +0,76 dB |
| head rápido 1 | 0,00498 → 0,00395 | 0,00824 → 0,00462 | +3,18 → +0,76 dB |
| head rápido 2 | 0,00694 → 0,00053 | 0,03043 → 0,00604 | +12,92 → +1,19 dB |
| head rápido 3 | 0,00679 → 0,00322 | 0,04218 → 0,00322 | +20,20 → +6,39 dB |
| head rápido 4 | 0,00102 → 0,00242 | 0,00355 → 0,00242 | +2,95 → +0,44 dB |
| freeze ON | 0,01327 → 0,01327 | 0,02054 → 0,02054 | ~0 dB (igual) |
| freeze OFF imediato | 0,00488 → 0,00488 | 0,01791 → 0,01791 | ~0 dB (igual) |
| tone 0→100 | 0,00427 → 0,00427 | 0,02246 → 0,02246 | +1,42 → +1,42 dB |
| tone 100→0 | 0,00418 → 0,00418 | 0,03066 → 0,03066 | +8,40 → +8,40 dB |
| Reverse + head | 0,00144–0,00898 (idêntico) | idêntico | 0,00 dB |

Head: **6/9** eventos com passo de borda menor; o quarto comando rápido ficou
2,4e-3 contra 1,0e-3 (abaixo do natural) e os dois eventos de Reverse são
idênticos porque a máscara é ignorada no Reverse pleno. Mix: **5/5**. Freeze de
entrada: idêntico ao original em todos os casos.

## Drive e Freeze no arrivo do eco

O ganho de Drive e o release do Freeze atuam na **escrita**; o efeito chega ao
monitor `delayTime` depois. A tabela mede o frame `comando + delay`.

| Caso | Passo 1 ms F4=0 → F4=1 | HF vs controle |
|---|---:|---:|
| drive 0→100, eco 2,001 s | 0,04013 → **0,01985** (−51%) | +2,18 → **−5,85 dB** |
| drive 100→25, eco 2,001 s | 0,03907 → 0,05189 | −1,17 → −6,51 dB |
| drive 0→100→25, eco 2,999 s | 0,04354 → 0,05726 | +6,09 → −6,47 dB |
| freeze fb 70, eco 2,799 s | 0,02191 → 0,02117 | **+5,37 → −18,89 dB** |
| freeze fb 95, eco 2,799 s | 0,02116 → 0,02068 | **+5,98 → −18,45 dB** |
| freeze release rápido 2 | 0,00677 → **0,00234** | −23,25 → −60,26 dB |

Drive: apenas **1/3** dos arrivos teve passo de 1 ms menor. O primeiro evento
(0→100) melhora de forma clara; os casos 100→25 não melhoram o máximo de
inclinação, embora o HF caia 5–7 dB e o passo permaneça abaixo do máximo
natural (0,0836 e 0,0778). A conclusão honesta é que a rampa de 10 ms remove o
ataque de HF do aumento de drive, mas não é superior em toda direção de
variação. Ela foi mantida por ser curta, limitada e por não piorar o pico
acima do sinal; se a escuta preferir o gesto sem rampa, remover é uma linha
(`kF4DriveFadeSeconds = 0` não é suportado; usar o flag F4=0 para a referência).

## Freeze: conteúdo e cauda

- O Freeze ON não altera os samples do monitor; a cauda de 100 ms é idêntica
  entre F4=0 e F4=1.
- O release não grava durante o congelamento; a rampa de 12 ms começa somente
  quando `freeze` volta a falso.
- Não há NaN/Inf em nenhum render (`finite=true` em todas as cenas).
- Pico máximo observado: **0,9328**; nenhum estouro do limiter.

## Preservação estática

20 cenários F0.2 com todos os parâmetros, sem automação: F4=1 vs F4=0
**10/10 bit-exact** (`max_abs = 0`). Os controles negativos da bancada também
são bit-exact. Após o Mix estabilizar, o F4=1 recupera o F4=0 bit-exact em
100 ms nas três cenas de um cabeçote; em multi-head o resíduo máximo é
`1,25e-5` (−98 dBFS), compatível com memória de filtros de playback, sem
diferença de ganho.

## Caudas de 60 segundos

`run.py tail` renderizou três casos de 60 s a 44,1 kHz com impulso inicial:

| Caso | F4=0 vs F4=1 | Pico | Crescimento no último segundo |
|---|---|---:|---:|
| estático fb 95 | **bit-exact** | 0,25 | 0,000000 dB |
| estático fb 100 | **bit-exact** | 0,25 | 0,000000 dB |
| automação (cabeçote, Freeze, fb 95, seno 0,05 em 18–32 s) | difere intencionalmente | 0,25 | 0,000000 dB |

As duas caudas estáticas são bit-exact porque a F4 não altera nenhum estado
sem comando. A cauda com automação permanece finita, sem crescimento, e o
pico não se aproxima do limiter. `LONG_TAIL_CHECK.json` contém o RMS por
segundo das duas variantes.

## Tamanho de bloco

Com o comando no frame comum 303.104 (múltiplo de 1, 37, 64, 128 e 256):

| Caso | 37 | 64 | 128 | 256 |
|---|---:|---:|---:|---:|
| head | 0 | 0 | 0 | 0 |
| freeze | 0 | 0 | 0 | 0 |
| mix | 0 | 0 | 0 | 0 |
| drive | 0 | 0 | 0 | 0 |
| tone | 0,015268 | 0,008445 | 0 | 0,015129 |

`max_abs` em relação ao bloco 128. O contrato C++
(`tests/core/f4_transitions_test.cpp`) acrescenta o bloco 1 e também obtém
`max_abs = 0` para head, freeze e mix. O Tone difere porque o `pbTransition`
existente avança uma vez por bloco (20 ms por bloco, ver `IMPLEMENTATION`), e
os valores são **idênticos em F4=0 e F4=1**: é uma propriedade pré-existente
do mecanismo de Tone, não uma dependência introduzida pela F4. Ela está
registrada para uma fase futura; não foi alterada nesta fase.

## Limitações da bancada

- Métricas de passo/HF são sensíveis ao material; por isso há controle
  negativo e máximo pré-janela. Nenhum artefato foi declarado apenas por
  diferença numérica.
- O bed harmônico não é uma gravação real; as conclusões de audibilidade
  dependem do pacote A/B e da escuta humana.
- A posição do comando é quantizada à borda de bloco, como no host real; os
  frames exatos estão no JSON.
- Benchmarks Native não são performance física.
