# Head switching — implementação F4

## Causa

`updateCachedParams()` recalcula `activeHeadMask` e `invHeadGainSum`
(`dsp/core/tape_core.cpp:530–535`). No sample seguinte à borda do bloco, a
leitura soma um conjunto diferente de taps e aplica uma normalização diferente:

```text
delaySig = (read1·1 + read2·0,75 + read3·0,55) · 1/Σpesos
```

Para 1→3, o peso do tap e a normalização mudam de `1/1` para `0,55/0,55`; para
1+2→2+3 o conjunto e o denominador mudam. O resultado é um degrau de nível e de
conteúdo, medido em `TRANSITION_AUDIT.md` acima do máximo natural do próprio
sinal.

## Solução

`HYDRA_DSP_REFORM_F4_TRANSITIONS=1` introduz três ganhos de pertencimento
(`headGain1..3`), um alvo por cabeçote e uma normalização corrente. A cada
sample, apenas durante a transição:

1. cada ganho caminha linearmente 10 ms em direção ao alvo 0/1;
2. `hSum = g1 + 0,75·g2 + 0,55·g3` e `headNormInv = 1/hSum`;
3. ao alcançar os alvos, os ganhos são **fixados exatamente** em 0/1 e
   `headNormInv` volta a `invHeadGainSum`, a mesma expressão do original.

O caminho de leitura (`:1248–1262`) continua lendo **as mesmas posições** e na
mesma ordem; só os pesos se movem. Não há leitor duplicado, buffer novo,
delay longo novo ou segunda cadeia de filtros. Se um ganho é zero e o alvo
também é zero, o tap não é lido — o custo estático de uma configuração de 1
cabeçote continua sendo **uma leitura**, não três.

Normalização durante a transição é uma média ponderada dos taps, portanto não
produz salto de nível: no instante do comando, os pesos correntes são os
antigos; no fim, são exatamente os novos.

## Bit-exactness

Quando estabilizado, `g ∈ {0,1}` e os produtos são exatamente os do original
(`1,0·1,0 = 1,0`; `0,75·1,0 = 0,75`; `0,55·1,0 = 0,55`), na mesma ordem de
soma, com `headNormInv = invHeadGainSum`. Por isso:

- F4=0 vs F3-A: **10/10 bit-exact**;
- F4=1 vs F4=0, sem automação: **10/10 bit-exact**;
- blocos 1/37/64/128/256 com o comando no mesmo frame: **max_abs = 0**.

A primeira configuração (`!headPrimed || !hasProcessed`) e `reset()` fixam os
ganhos diretamente, sem rampa, para que um render offline configurado antes do
primeiro bloco permaneça idêntico.

## Comandos durante um fade

Os alvos são reavaliados a cada commit. Um novo comando no meio de uma rampa
apenas troca o alvo; os ganhos continuam do valor corrente, sem reinicialização
e sem estalo. O teste de quatro comandos em 150 ms (1→7→1+2+3→2+3→1+2) cobre
exatamente essa condição. O caso 2+3→1+2 (último comando) ficou **0,00102 →
0,00242** de passo de borda: numericamente pior que o original, porém ainda
abaixo do máximo natural e com HF −0,50 dB; não é um artefato acima do sinal.
Isso está registrado como não-superioridade, não como melhoria.

## Reverse e Multi-Head

`reverseBlend ≥ 1` usa a janela reversa e ignora a máscara. A troca de
cabeçotes durante Reverse não altera a leitura (F4=0 = F4=1 bit-exact nas duas
trocas medidas), evitando misturar posições temporais incompatíveis. Quando o
Reverse termina, os ganhos já estão no alvo; não há salto na saída do Reverse.

`headsMusical` muda as **posições** dos taps, não apenas a máscara. A F4 não
altera esse caminho; uma troca simultânea de modo musical e máscara continua
sendo uma mudança de posição temporal, fora do escopo desta fase.

## Custo

- 3 ganhos + 3 alvos + normalização: ~64 bytes no `Impl` (heap; buffers
  inalterados).
- Por sample, somente com transição ativa: 3 passos + 3 multiplicações + 2
  somas + 1 divisão, ~20–30 ciclos estimados no LX7.
- Configuração estável: uma leitura de bool e as mesmas leituras de tap que o
  original; nenhum cálculo de normalização por sample.
- `.text` Native: +1.800 bytes (+2,1%); data/bss 0. Benchmark físico pendente.
