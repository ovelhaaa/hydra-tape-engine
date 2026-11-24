# Auditoria das transições — F4

Fonte: `experiment/dsp-reform-f4-performance-transitions`, derivada de
`29f50258f1ac47139ac8f1fdb21aa59424ca9a75`. Flags de medição:
`HYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING=1`, `HYDRA_DSP_REFORM_F2_VARIANT=2`,
`HYDRA_DSP_REFORM_F3_VARIANT=0`, `F4=0` (original) e `F4=1` (experimento).
Native Release a 44.100 Hz, estéreo, blocos de 128. Entradas determinísticas
(seed 20261124), comandos na borda de bloco; frames exatos estão em
`AUTOMATION_MEASUREMENTS.json`.

## Como ler as métricas

- **Passo de borda**: `|y[n] − y[n−1]|` exatamente entre o último sample antes
  do comando e o primeiro depois.
- **Passo de 1/5 ms**: máximo local após o comando. Ajuda a ver a rampa, mas
  também captura ataques musicais naturais; por isso é comparado ao controle
  sem automação e ao máximo dos 50 ms anteriores.
- **HF 4–16 kHz**: RMS de um high-pass de 2ª ordem (4 kHz) em 5 ms, em dBFS.
  `HF vs pré` compara com os 50 ms anteriores do **mesmo** sinal; `HF vs
  controle` compara com o render sem comando. Um clique acrescenta energia HF
  que não existia antes; uma simples mudança de fase não.
- **RMS/peak**: janelas de 10/50 ms e cauda de 100 ms.
- Um valor numérico diferente não é declarado artefato auditável por si só.
  A decisão usa passo de borda, HF acima do pré e o controle negativo.

## Baseline F4=0 — o que existe hoje

### Mix (IDs 14/15, `dryWet`/`delayWet`)

`updateCachedParams` (`dsp/core/tape_core.cpp:477–507`) grava
`textureWetGain`, `delayWetGain` e `dryGain = 1 − textureWetGain` de uma vez.
No caminho de áudio (`:1185–1190 e :1290`) esses ganhos entram no monitor; o feedback
Classic C usa `delaySig` antes dos ganhos, portanto o mix **não** acopla na
cauda. O host ESP32 usa `p_mix` para os dois IDs (`src/main.cpp:274–276`),
semântica preservada.

Medição original: Mix 0→100 produziu passo de borda **0,03964**, 1 ms
**0,07851**, +12,25 dB de HF vs controle; 100→0 passo **0,05238**. É uma
descontinuidade de ganho real, não um ataque da fonte: o controle negativo
(sem comando) não tem esse passo.

### Drive (ID 3)

`driveGain = drive*0.05` (`:477`) escala a entrada fresca antes da escrita
magnética (`:1368–1385`). O efeito audível chega ao monitor um tempo de delay
depois; por isso a medição específica é feita no **arrivo do eco**
(`evento + delay_ms`). Medição original: Drive 0→100 em 1,5 s, eco em 2,0 s,
passo de 1 ms **0,04013** e HF +2,18 dB vs controle; o cenário 100→25 também
foi medido. O ganho não entra no feedback; ele altera o conteúdo gravado.

### Tone (ID 20)

O TapeCore já interpola os parâmetros de playback (`pbSpeed`, `pbAge`,
`pbTone`, `pbBump`) e recalcula os coeficientes com `pbTransition` a cada bloco,
com constante de 20 ms (`:980–993`, `updatePlaybackFilters` `:672–691`). A
primeira configuração é aplicada de imediato (`pbPrimed`), e mudanças
posteriores rampeiam. Não existe um segundo smoothing F4.

Medição original (F4=0 e F4=1 são idênticos por construção): Tone 0→100 com
feedback 85, passo de borda **0,00427**, 1 ms **0,02246**, abaixo do máximo
natural dos 50 ms anteriores (**0,02487**); Tone 100→0 idem; HF no comando
não excede o pré-janela de forma relevante (+1,4 dB e +0,2 dB). **Não há
descontinuidade de amostra**; o `pbTransition` existente é a proteção
adequada. Decisão: `ALREADY_SATISFACTORY`, sem código F4.

### Head switching (ID 16)

`activeHeadMask` e `invHeadGainSum` são recalculados em `updateCachedParams`
(`:530–535`). O caminho de leitura (`:1247–1263`) soma os cabeçotes ativos com
pesos 1/0,75/0,55 e normaliza por `1/Σpesos`. Quando a máscara muda, os pesos e
a normalização mudam no mesmo sample, na borda do bloco.

Medições originais (fonte `poly_synth`, 1,5 s):

| Mudança | Passo borda | Passo 1 ms | HF vs pré | Máx. pré 50 ms |
|---|---:|---:|---:|---:|
| 1 → 3 | 0,00858 | 0,03800 | **+9,97 dB** | 0,01143 |
| 1+2 → 2+3 | 0,00985 | 0,03028 | **+7,40 dB** | 0,01110 |
| 3 → 1+2+3 | 0,00498 | 0,00824 | +2,28 dB | 0,01141 |
| quatro trocas em 150 ms | 0,00102–0,00694 | 0,00355–0,04218 | +3,0 a +20,2 dB | 0,009–0,042 |

Os dois primeiros casos têm passo acima do máximo natural e acréscimo de HF:
são descontinuidades de ganho audíveis. O caso 3→1+2+3 é menos exposto porque
os taps são correlacionados no material usado; ele foi mantido na medição para
não superestimar a melhoria.

### Freeze (ID 22)

`freeze` interrompe a escrita magnética (`:1379`), mas `freezeFade` era
atualizado em `:1457–1463` e nunca lido pelo áudio. Duas consequências:

1. **Entrada**: o corte de escrita não altera os samples lidos; a saída é
   contínua. A medição confirma: eventos de Freeze ON têm passo e HF
   **idênticos** ao F4=1 e iguais ao controle (diferença ~0). Não há evidência
   de clique de entrada; a hipótese de que o fade existente resolveria algo é
   falsa.
2. **Saída**: ao liberar, `recSig` volta com amplitude plena no mesmo sample.
   O conteúdo novo só chega ao monitor depois de `delayTime`; é nesse arrivo
   que a borda aparece. Medição original com delay 300 ms: no arrivo do eco,
   HF vs controle **+5,37 dB** (fb 70) e **+5,98 dB** (fb 95), com passo de
   1 ms 0,02191/0,02116.

O Freeze não é um loop estático: a cabeça de escrita continua avançando e as
leituras percorrem o histórico. A F4 **não altera essa semântica**.

### Reverse e Multi-Head

Em Reverse pleno (`reverseBlend ≥ 1`) o caminho de máscara é substituído pela
janela reversa; trocar `activeHeads` durante o Reverse não muda a leitura. A
medição de `reverse_head_switch` mostra F4=0 = F4=1 exatamente, como esperado.
Isso evita misturar posições temporais incompatíveis.

## Controle negativo

Cenas `control_no_automation` e `control_mix_no_automation` têm os mesmos
parâmetros e entrada, sem eventos. F4=0 e F4=1 são **bit-exact** entre si
(`max_abs = 0`). Elas calibram os máximos naturais usados nas razões acima.

## O que a F4 mediu antes de mudar

1. Head: causa confirmada (`activeHeadMask`/`invHeadGainSum` instantâneos) com
   artefato acima do natural em 1→3 e 1+2→2+3.
2. Freeze: entrada já correta; saída com borda no arrivo do eco.
3. Mix: descontinuidade real de ganho.
4. Drive: borda no conteúdo gravado, visível no arrivo do eco; melhora apenas
   parcial com rampa.
5. Tone: já protegido; nenhuma mudança justificada.

