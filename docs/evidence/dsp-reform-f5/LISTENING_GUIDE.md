# F5 — Guia de escuta A/B/C

Pacote: `artifacts/dsp-reform-f5/listening/` (11 cenas, 66 WAVs PCM24, player local).
Abra `index.html` por um servidor local (o player usa `fetch` e `decodeAudioData`):

```sh
python -m http.server 8765 --bind 127.0.0.1 --directory artifacts/dsp-reform-f5/listening
```

## Cegueira

Cada cena tem três slots **X/Y/Z** embaralhados com semente `20261124`. A chave
`answer-key.json` só é exibida depois de completar as 22 avaliações (11 cenas × RAW e
MATCHED). A identidade do pacote é o SHA-256 do manifesto; as respostas ficam no
`localStorage` do navegador. Não presuma que X é a Vintage.

## Como ouvir

1. Escolha a cena e o formato **RAW** (ganho real do render) e depois **MATCHED**
   (mesmo RMS em 0,5–8,0 s, teto 0,98 FS).
2. Use 1/2/3 para alternar X/Y/Z **no mesmo relógio**. O player faz crossfade de 8 ms,
   sem reposicionar o áudio.
3. A linha vermelha no waveform marca o comando de tempo (BPM, Tap ou MIDI); o aviso
   acima pisca quando a reprodução cruza o comando.
4. Julgue o **gesto**, não o timbre estático:
   - o eco continua na grade rítmica?
   - o capstan acelera/desacelera de forma natural ou dá um salto?
   - a afinação transitória é musical ou é um dive?
   - a cauda e os ataques permanecem intactos?
   - o Reverse continua coerente com o pulso?
5. Registre preferência (X/Y/Z/Equivalentes/Rejeitar todos), confiança e comentários.
6. Exporte o JSON. A decisão musical é humana; o teste automático não aprova áudio.

## Cenas

| # | Fonte | Gesto |
|---|---|---|
| 01 | guitarra limpa | BPM 120→100 abrupto |
| 02 | guitarra saturada | BPM 80→160 grande salto para cima |
| 03 | pad polifônico | rampa 120→100 em 8 passos de 0,25 s |
| 04 | guitarra limpa | Tap 120→90 |
| 05 | pad polifônico | MIDI clock 120→125 |
| 06 | baixo | MIDI perdido 2,5 s e reacquired em 125 |
| 07 | poly synth + Reverse V2 | divisão 1/4→1/8 |
| 08 | multi-head musical | BPM 120→100 |
| 09 | pad + Reverse V2 | BPM 120→90 |
| 10 | percussão irregular | BPM 120→90 |
| 11 | guitarra limpa | controle negativo, sem comando |

As cenas 01–10 compartilham exatamente comandos, parâmetros e instantes entre as três
candidatas. A cena 11 deve soar idêntica nas três; use-a como controle de expectativa.

## Leitura dos resultados objetivos

`TIMING_MEASUREMENTS.json` e `MEASUREMENT_TABLES.md` trazem convergência, erro de fase,
pitch transitório, HF, overshoot e drift. Não trate qualquer mudança de pitch como falha:
a pergunta é se o gesto é **intencional e musical**. A candidata Clock-Focused troca
velocidade por suavidade em mudanças grandes; isso está documentado, não escondido.

## Depois da escuta

Preencha `human-listening-decision.json` (modelo em `HUMAN_DECISION_TEMPLATE.md`) e
registre preferência/equivalência/rejeição por cena e formato. Nenhuma candidata é
promovida automaticamente; F5=0 continua o default e a referência.
