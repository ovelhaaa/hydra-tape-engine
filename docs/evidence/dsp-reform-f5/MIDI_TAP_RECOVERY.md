# F5 — Tap Tempo e MIDI Clock

**Contratos preservados.** F5 não altera `tap_tempo.hpp` nem `midi_clock_sync.hpp`; os
testes `tap_tempo_test`, `midi_clock_test` e `transport_clock_test` continuam passando.
A única mudança é o alvo de leitura dos cabeçotes musicais e sua política de glide.

## 1. Tap Tempo

| Cena | A (Vintage) | B (Capstan) | C (Clock-Focused) |
|---|---|---|---|
| 120→90 regular, convergência a 1 ms | nunca (erro 166,7 ms) | 0,591 s | 0,960 s |
| 120→90, pico de pitch | salto (7.350 amostras) | 223 cents | 156 cents |
| Acelerando (100→143 BPM) | 20,0 ms de erro | 0,438 s | 0,496 s |
| Ritardando | 40,0 ms de erro | 0,267 s | 0,166 s |
| Dois taps nova cadência (120→90) | 166,7 ms | 1,658 s | 1,913 s |
| Timeout + nova aquisição (90) | 166,7 ms | 0,241 s | 0,497 s |
| Tap fora do padrão (isolado) | sem mudança | sem mudança | sem mudança |
| Bounce <80 ms | rejeitado, sem atualização | idem | idem |

O contrato de Tap permanece: o primeiro tap só estabelece a base; dois taps atualizam a
estimativa; um outlier isolado não muda o BPM; bounce é rejeitado; timeout de 3 s reinicia
a sessão. As diferenças acima são só a transição do delay depois que o Tap já decidiu o
BPM.

Leitura musical: B é mais rápida e mais expressiva (223 cents) em degraus grandes; C é
mais lenta e mais suave (156 cents). Em cadências pequenas (ritardando, acelerando com
passos menores) C é mais rápida. Isso é exatamente a troca declarada, não uma regressão.

## 2. MIDI Clock (24 PPQN)

| Cena | A (Vintage) | B (Capstan) | C (Clock-Focused) |
|---|---|---|---|
| Estável 120, status | Locked | Locked | Locked |
| 120→125, convergência a 1 ms | nunca (20,0 ms) | 0,416 s | 0,242 s |
| 120→125, pico de pitch | 0 (não move) | 73 cents | 86 cents |
| 120→90, convergência a 1 ms | nunca (166,7 ms) | 1,188 s | 1,751 s |
| 120→90, pico de pitch | 0 (não move) | 223 cents | 156 cents |
| Jitter ±1,5 ms, pico de pitch | 0 | 50 cents | 53 cents |
| Jitter, erro final vs alvo do motor | 95,6 amostras | 59,3 | 60,1 |
| Perda de 1 pulso, missed/final BPM | 4 / 119,99 | 4 / 119,99 | 4 / 119,99 |
| Perda de 2 pulsos, missed/final BPM | 6 / 119,99 | 6 / 119,99 | 6 / 119,99 |
| Holdover 2,5 s + reaquisição 125 | 20,0 ms de erro | 0,275 s | 0,060 s |
| Start/Stop/Continue | BPM 120,08 | 120,08 | 120,08 |
| Troca Internal→MIDI (125) | salto de 882 amostras | 0,101 s | 0,002 s |

O jitter de C ficou praticamente igual ao de B (53 vs 50 cents de pico, 60 vs 59 amostras
de erro final), porque o dead-band de 2 ms no alvo e a classificação de micro-episódio
impedem perseguir o PLL pulso a pulso. A diferença real aparece em mudanças reais de BPM:
C converge mais rápido em 120→125 (0,242 s vs 0,416 s) e mais devagar em 120→90
(1,751 s vs 1,188 s), com menos pitch (156 vs 223 cents).

`midi_status = 4` no fim de algumas cenas é `Holdover` porque os pulsos terminam antes do
fim do render; durante a cena o estado é `Locked` (ver `clock_sequence` em
`TIMING_MEASUREMENTS.json`). Start/Stop/Continue usa o transporte existente e não é
alterado pela F5.

## 3. Limitação documentada

Com um único leitor, uma mudança grande de clock (120→90) não pode ser instantânea sem
salto. C escolhe ser mais lenta e mais suave; B escolhe ser mais rápida e mais expressiva.
Se a escuta considerar que nenhuma das duas é suficiente para saltos muito grandes, a
alternativa estrutural (dois leitores/crossfade curto) pertence à F7 e **não** foi
implementada aqui.
