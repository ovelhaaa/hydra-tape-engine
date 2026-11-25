# Guia de escuta — F4 A/B

Pacote: `artifacts/dsp-reform-f4/listening/` (13 cenas, 52 WAVs PCM24
estéreo 44.100 Hz, RAW + MATCHED, player + gabarito separado + SHA-256).
Manifesto: `manifest.json`; respostas: `answer-key.json`.

## Abrir

```sh
python -m http.server 8765 --bind 127.0.0.1 \
  --directory artifacts/dsp-reform-f4/listening
```

Abra `http://127.0.0.1:8765/` no Edge/Chrome. O player usa o `Deck` sincronizado
do Listening Lab existente (`lab/audio.mjs`): dois WAVs no mesmo relógio,
troca com rampa de 8 ms, loop, seek e volume. Não é uma nova aplicação de
escuta; é a mesma base do Lab, com uma página A/B curta.

- **X / Y** são os candidatos cegos, em ordem sorteada por cena e fixa no
  manifesto. **RAW** mantém o ganho do render; **MATCHED** aplica um ganho
  constante por arquivo para igualar o RMS em 0,5–8,5 s (teto 0,98 FS).
- Cada cena mostra os instantes dos comandos. Eles são idênticos em A e B.
- Preencha preferência, confiança e comentário para **cada cena × formato**.
  O botão de revelar só libera depois de 26 avaliações completas.
- **Exportar avaliação JSON** grava o arquivo de decisão. Ele não substitui a
  decisão humana registrada no repositório; é a evidência da sessão.

## O que ouvir

Não julgue apenas o timbre estático: as duas versões são idênticas sem
automação (10/10 bit-exact). Ouça o **gesto**.

| # | Cena | Foco |
|---|---|---|
| 1 | Head 3 → 1+2+3 | nível e brilho no instante da troca; cauda permanece? |
| 2 | Quatro trocas de cabeçote em 150 ms | comandos durante fade; algum estalo? |
| 3 | Head 1 → 3 | salto de posição temporal e de nível |
| 4 | Heads 1+2 → 2+3 | material percussivo, ataque e corpo |
| 5 | Freeze ON/OFF | entrada silenciosa; release sem “splash” |
| 6 | Freeze com fb 95 | cauda longa; o release ainda é controlado? |
| 7 | Freeze + Reverse + multi-head | interação; o Reverse continua coerente? |
| 8 | Mix 0→100→0 | clareza dos ataques, sem bombeamento |
| 9 | Drive 0→100→25 | ataque do eco, sem dureza |
| 10 | Tone 0→100→0 | arampa existente; não deve haver clique |
| 11 | Multi-head + Mix 0→100 | soma de cabeçotes com mix |
| 12 | Reverse + troca de cabeçote | durante Reverse a máscara é ignorada |
| 13 | Controle sem automação | deve soar idêntico (A=B) |

Cenas de controle negativo: 13 (e as duas de controle da bancada). Se você
ouvir diferença na 13, registre; o esperado é equivalência bit-exact.

## Critério de decisão

- **X preferido / Y preferido**: qual versão parece mais controlada como
  instrumento, sem perder a identidade Vintage.
- **Equivalentes**: nenhuma diferença musical relevante no gesto.
- **Rejeitar todos**: alguma versão descaracterizou o Freeze, o cabeçote ou a
  cauda.

Uma preferência em RAW pode desaparecer em MATCHED; registre as duas. Não há
resposta “correta” e o teste automático não declara aprovação musical.

## Integridade

`sha256.json` cobre todos os arquivos do pacote, inclusive os WAVs. O player
verifica o SHA-256 de cada WAV antes de decodificar. O gabarito é um arquivo
separado e só é exibido após as 26 avaliações. O manifesto registra commit,
flags, hashes das duas bibliotecas, frames dos comandos, taxa, bloco e método
de matching.
