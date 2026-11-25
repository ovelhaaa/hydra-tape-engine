# F5-C — Clock-Focused Timing

**Flag:** `HYDRA_DSP_REFORM_F5_TIMING_VARIANT=2`. **F5=0 permanece o default.**

## 1. Hipótese

Quando o tempo vem de Tap ou MIDI, o músico espera alinhamento rápido ao pulso, mas não
aceita um pitch dive. Com um único leitor, velocidade e suavidade são uma troca. A C
escolhe a política pelo **tamanho do episódio de clock**:

| Episódio | tau | teto de pitch | comportamento |
|---|---|---|---|
| micro: ≤1% de um beat | 0,200 s (capstan) | 0,120 | jitter/fase: suave como B |
| pequeno: 1–5% de um beat | 0,030 s | 0,050 (~86 cents) | converge rápido com pouco pitch |
| grande: >5% de um beat | 0,150 s | 0,085 (~156 cents) | mais lento e mais suave |

O episódio é medido do último alvo estável até o alvo atual (cumulativo), então a rampa
de BPM que o MIDI entrega em passos pequenos é classificada pelo total. Um dead-band de
2 ms no alvo ignora o “respirar” do PLL; um hold de 0,05 amostra finaliza sem mover a
cabeça (um snap de meia amostra seria um blip de ~700 cents por uma amostra). Internal e
o modo ms usam a política de capstan da B.

## 2. Resultados (44,1 kHz, bloco 128)

| Cena | A | B (Capstan) | C (Clock-Focused) |
|---|---|---|---|
| MIDI 120→125, 1 ms | nunca (20 ms) | 0,416 s / 73 cents | **0,242 s / 86 cents** |
| MIDI 120→90, 1 ms | nunca (166,7 ms) | **1,188 s / 223 cents** | 1,751 s / 156 cents |
| Tap 120→90, 1 ms | nunca (166,7 ms) | **0,591 s / 223 cents** | 0,960 s / 156 cents |
| Tap acelerando, 1 ms | nunca (20 ms) | 0,438 s / 198 cents | 0,496 s / 86 cents |
| Tap ritardando, 1 ms | nunca (40 ms) | 0,267 s | **0,166 s** |
| MIDI jitter ±1,5 ms, pico | 0 | 50 cents | 53 cents |
| MIDI jitter, erro final | 95,6 amostras | 59,3 | 60,1 |
| Holdover + reaquisição, 1 ms | nunca (20 ms) | 0,275 s | **0,110 s** |
| Perda de 1/2 pulsos | Locked, 119,99 | idem | idem |
| Troca Internal→MIDI | salto | 0,101 s | 0,101 s (mesma política de B no evento) |

Leitura: C é mais rápida e mais precisa em **mudanças pequenas/médias de clock**
(120→125, reaquisição, accelerando), com pitch abaixo de 90 cents. Em mudanças grandes
(120→90), C é mais lenta que B, mas com ~70 cents menos de pitch. No jitter, C ficou
praticamente igual a B — não persegue cada pulso.

O erro final de C em algumas cenas (31–60 amostras ≈ 0,7–1,4 ms) é o dead-band de 2 ms
do alvo, não o glide: o motor mantém a posição musical exata e filtra o ruído do PLL.
Contra a grade absoluta, o erro dominante é o próprio viés do estimador MIDI
(≈0,5 BPM de jitter), igual em B e C.

## 3. Limitação explícita

Uma mudança de 120→90 (16,7% de um beat) não pode ser instantânea com um leitor sem um
salto audível. C escolhe o caminho mais suave e leva ~1,75 s. Se a escuta julgar esse
tempo excessivo, a solução estrutural é um leitor de transição/crossfade curto — decisão
da F7, não desta fase. Não foi implementado segundo leitor nem crossfade de memória.

## 4. Custo

Mesmos 56 bytes de estado da B, sem buffer. A seleção de política acontece em eventos de
tempo, não a cada amostra; o loop só escolhe entre dois conjuntos de constantes já
calculadas. Nenhuma alocação, lock ou transcendente por amostra.
