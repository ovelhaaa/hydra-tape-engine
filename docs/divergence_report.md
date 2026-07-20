# Auditoria estática: `TapeCore` versus `TapeModel`

Data da leitura: 2026-07-18. As referências abaixo são para a árvore de trabalho; números de linha devem ser atualizados por `git blame` se o DSP for editado. Não foi encontrado comentário ou commit que documente uma intenção para as diferenças marcadas **(b)**.

## Cobertura de `TapeParams`

| Parâmetro/comportamento | TapeCore (arquivo:linha) | TapeModel (arquivo:linha) | Diferença numérica/comportamental | Classificação | Impacto |
|---|---|---|---|---|---|
| `flutterDepth` | `dsp/core/tape_core.cpp:290`, `:219`: `0.00010*p*base*flutter` | `lib/TapeDelay/src/TapeDelay.cpp:315`: `0.001*p*base*flutter` | p=0/14/100: 0/10x/10x; firmware é 10× | (b) | alto |
| `wowDepth` | `tape_core.cpp:290`, `:219`: `0.00025*p*base*wow` | `TapeDelay.cpp:316`: `0.001*p*base*capstan` | p=0/10/100: 0/4x/4x (além de forma de onda diferente) | (b) | alto |
| `dropoutSeverity` | `tape_core.cpp:344`: clamp `p*.01` | `TapeDelay.cpp:286`: valor cru | 0/5/100: 0/0.05/1 contra 0/5/100; depende de `DropoutGenerator` esperar normalizado | (c): correção em `TapeDelay.cpp:286`, multiplicar por `.01f` e clamp | alto |
| `drive` | `tape_core.cpp:245,472`: `p*.05` | `TapeDelay.cpp:573,776`: `p*.05` | 0/40/100: 0/2/5, igual | (a) mesmo código herdado | baixo |
| `noise` | `tape_core.cpp:271,385`: `p*.001`, duck por envelope | `TapeDelay.cpp:641,807`: `p*.001`, sem duck | 0/30/100: mesmo ganho base, core reduz para 22% com entrada | (b) | médio |
| `tapeSpeed` | `tape_core.cpp:292,299`: bump 60–200 Hz | `TapeDelay.cpp:123,159`: head shelf fixo 100 Hz | p=0/50/100: core 60/130/200, firmware 100/100/100 | (b) | alto |
| `tapeAge` | `tape_core.cpp:299-302` | `TapeDelay.cpp:160-208` | mesma curva de cutoff; core usa também pré/de-ênfase no feedback | (b) | médio |
| `headBumpAmount` | `tape_core.cpp:300`: peak, `p*.06`, 0–6 dB | `TapeDelay.cpp:154`: low shelf, `p*.05` | 0/30/100: 0/1.8/6 dB vs 0/1.5/5 dB, tipo EQ diferente | (b) | alto |
| `azimuthError` | `tape_core.cpp:253,371`: normaliza `.01`, suaviza | `TapeDelay.cpp:650`: estéreo normaliza; mono `:445` não normaliza | p=0/10/100: estéreo igual alvo, mono 100× | (c): `TapeDelay.cpp:445` deve aplicar `*.01f` | alto |
| `flutterRate`, `wowRate` | `tape_core.cpp:282-289`: taxas + filtros SR-normalizados | `TapeDelay.cpp:126-131,228-232` | fases iguais, core introduz estados mecânicos adicionais | (b) | médio |
| `delayActive`, `delayTimeMs` | `tape_core.cpp:333-347,359` | `TapeDelay.cpp:262-290,620` | ambos 200 ms smoothing/250 ms ramp; core reseta mais filtros e write head | (b) | médio |
| `feedback` | `tape_core.cpp:249,441-464`: clamp .88, ping-pong real | `TapeDelay.cpp:748-769` | ambos .88; core usa saturação racional/pre-emphasis em vez de `TapeMagnetics` | (b) | alto |
| `dryWet` | `tape_core.cpp:246-248,427` | `TapeDelay.cpp:795` | core é textura wet e delay tem controle separado; firmware mistura todo tape | (b) | alto |
| `delayWet` | `tape_core.hpp:23`, `tape_core.cpp:247,427` | ausente de `TapeDelay.h:424-456` | core 0/50/100 separa 0/.5/1 de delay; firmware não pode representar | (b), não portado | alto |
| `activeHeads`, `bpm`, `headsMusical` | `tape_core.cpp:254-258,417-426` | `TapeDelay.cpp:676-728` | fórmulas/ganhos iguais; core protege BPM mínimo 1 | (c): firmware deve usar `max(1,bpm)` antes de dividir | médio |
| `guitarFocus`, `tone` | `tape_core.cpp:297-303` | `TapeDelay.cpp:136-208` | fórmulas de filtro iguais fora do head-bump | (a) mesmo código herdado | médio |
| `pingPong` | `tape_core.cpp:477-478` cruza fontes L/R | `TapeDelay.cpp:448`, sem uso no processamento | toggle sem efeito no firmware | (c): cruzar `feedbackSource` antes do feedback em `processStereo` | alto |
| `freeze` | `tape_core.cpp:472,498` | `TapeDelay.cpp:790,853` | ambos deixam de escrever; `freezeHead` firmware não é usado | (b) | médio |
| `reverse`, `reverseSmear` | `tape_core.cpp:223,420,486` | `TapeDelay.cpp:346-405,701,810` | fórmulas iguais, mas firmware mantém contadores `static` globais compartilhados | (c): mover os dois `static` para membros de `TapeModel` | alto |
| `spring`, `springDecay`, `springDamping`, `springMix` | `tape_core.cpp:259-262,306-307,487-496` | `TapeDelay.cpp:248-258,818-842` | fórmulas de percentagem iguais; comentário firmware diz 0–1 e construtor usa `.5`, produzindo 100× menos decay/damping até UI sobrescrever | (c): defaults firmware devem ser 60/45 ou conversão deve ser removida | alto |

## Comportamentos sem par e robustez

| Parâmetro/comportamento | TapeCore (arquivo:linha) | TapeModel (arquivo:linha) | Diferença numérica/comportamental | Classificação | Impacto |
|---|---|---|---|---|---|
| `FreeverbEngine`, `FrippEngine`, `BubblesEngine` | ausentes | `TapeDelay.h:690-979` | módulos firmware adicionais; **não portados**, fora deste projeto | (a) escopo explícito | médio |
| mono versus estéreo | `TapeCore::process` delega ao estéreo (`tape_core.cpp:351`) | `TapeDelay.cpp:410-592` tem cadeia distinta de `:594-857` | mono não tem ping-pong/reverse/spring/noise igual, tem escalas de modulação e azimuth diferentes | (c): implementar mono via `processStereo(x,x)` | alto |
| ruído determinístico | `TapeCore::reset` reseeda valores fixos (`tape_core.cpp:323-327`) | `TapeNoiseGenerator` usa `micros()` (`TapeDelay.h:210-214`) | firmware varia entre execuções; inviabiliza null test sem stub | (b) | médio |
| alocação | `TapeCore` falha de forma observável com `isValid` (`tape_core.cpp:190,319`) | firmware reduz silenciosamente máximo para .4 s após PSRAM falhar (`TapeDelay.cpp:20-45`) | o mesmo parâmetro pode ser clampado por memória disponível | (b) | médio |
| sample rate | core limita bias a `.45*fs` e estados com `srScale` (`tape_core.cpp:84,283-289`) | firmware usa coeficientes por `fs`, mas Freeverb externo tem escalas próprias | resultado diverge em 44.1/96/192 kHz; confirmar A/B | (b) | médio |

## Harness desktop e limitações

`tests/core/firmware_stubs/Arduino.h` fixa `micros()` em zero, torna `Serial` no-op, implementa `constrain` e remove `IRAM_ATTR`. `esp_heap_caps.h` mapeia as duas capacidades de heap para `malloc/free`. Isso é equivalente para a matemática do `TapeModel`, mas não para política de PSRAM, logging, IRAM ou seed de relógio; essa limitação é deliberadamente declarada, não escondida.
