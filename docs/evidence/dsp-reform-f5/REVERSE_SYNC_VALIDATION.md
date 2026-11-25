# F5 — Reverse V2 e continuidade temporal

**Arquivo de referência:** `docs/REVERSE_V2.md` (não modificado).  
**Código envolvido:** `dsp/core/reverse_windows.hpp` (não modificado) e
`tape_core.cpp` (apenas o alvo dos cabeçotes musicais).

## 1. Contrato preservado

- O Reverse V2 lê a **mesma** memória circular do TapeCore; nenhum buffer novo.
- As janelas são configuradas com o BPM **exato** do Global Transport Clock, nunca com o
  glide: `applyEffectiveBpm()` e `updateParams()` continuam chamando
  `reverseWindows.configure(...)` com `transportClock.bpm()`.
- A ordem reversa, o overlap/smoothstep, o limite de hop e o modo legado V1 não mudaram.
- O feedback e o blend Reverse continuam sendo calculados depois da leitura, como antes.

## 2. Medição por divisão

Cena a 120 BPM, mudança para 90 BPM em 3,0 s, histórico de 8 s, `mode=0` (Rhythmic),
`headsMusical=1`, três variantes. A tabela mostra `effectiveMs` depois da mudança; o
valor esperado é `60000 × beats / 90`.

| Div | Subdivisão | Esperado (ms) | A | B | C |
|---|---|---|---|---|---|
| 0 | 1/4 | 666,67 | 666,67 | 666,67 | 666,67 |
| 1 | 1/1 | 2.666,67 | 2.666,67 | 2.666,67 | 2.666,67 |
| 2 | 1/2 | 1.333,33 | 1.333,33 | 1.333,33 | 1.333,33 |
| 3 | 1/8 | 333,33 | 333,33 | 333,33 | 333,33 |
| 4 | 1/16 | 166,67 | 166,67 | 166,67 | 166,67 |
| 5 | 1/4 pontuada | 1.000,00 | 1.000,00 | 1.000,00 | 1.000,00 |
| 6 | 1/8 pontuada | 500,00 | 500,00 | 500,00 | 500,00 |
| 7 | 1/4 tercinada | 444,44 | 444,44 | 444,44 | 444,44 |
| 8 | 1/8 tercinada | 222,22 | 222,22 | 222,22 | 222,22 |

- Todas as divisões: `clamped = 0`, saída finita, nenhuma reconfiguração destrutiva.
- `max_step_vs_control_db = 0` nas três variantes: o evento de BPM não introduziu um
  degrau de saída maior que o material de controle (a cena usa um pad sustentado; o
  degrau dominante é o próprio conteúdo).
- O teste de contrato `f5_timing_v*` também verifica, com Reverse ligado, que
  `requested == 60000/90` e `effective == requested` (≤0,5 ms) e que a saída permanece
  finita.

## 3. Separação explícita

| Fenômeno | Tratamento |
|---|---|
| Transição do delay convencional | glide F5-B/C (alvo dos cabeçotes) |
| Reconfiguração das janelas Reverse | BPM exato do GTC, sem glide |
| Alteração real da fase musical | `transportClock.locate/setBpm`, inalterado |

A F5 **não** corrige o Reverse alterando o GTC e não usa o glide para “consertar” janelas.
Como o Reverse V2 não lê os cabeçotes musicais convencionais, a política B/C não o
desalinha. Se no futuro o Reverse passar a ler o alvo suavizado, isso exige uma decisão
própria e um teste de fase dedicado; não é o caso desta fase.

## 4. Observação sobre o modo legado V1

`reverseMode == 2` continua usando `musicalHeadDelay3` como distância. Em F5-B/C esse
valor é o efetivo (suavizado) durante o glide, o que torna o V1 coerente com o capstan;
em F5=0 permanece bit-exact com a Vintage. O V1 não é promovido nem alterado
semanticamente.
