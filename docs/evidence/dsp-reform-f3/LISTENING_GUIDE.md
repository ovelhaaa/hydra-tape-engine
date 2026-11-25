# Audição F3

Extraia o artifact GitHub Actions. Na pasta que contém index.html e manifest.json,
execute `python -m http.server 8765 --bind 127.0.0.1` e abra
[Listening Lab local](http://127.0.0.1:8765/). O ZIP pode conter
artifacts/dsp-reform-f3/listening/. Player e Deck existentes reutilizados.

13 cenas, 78 WAVs PCM24 estéreo/44100Hz. Sete fontes sintéticas:
guitarra limpa/saturada, baixo simulado, synth grave, acordes, bateria e
percussão. Nenhuma gravação DI real. FB50/75/85/95 e100; Single, Multi-Head,
Reverse e Ping-Pong. Dez cenas16s e três estresses60s, caudas completas.

Compare X/Y/Z com teclas1/2/3 na mesma posição. RAW observa ganho/headroom;
MATCHED reduz viés de loudness. Atenção aos ataques, densidade em FB75–95,
agressividade dos harmônicos e sustain após parar a fonte.
Mais RMS ou mais harmônicos não estabelece qualidade. Equivalência audível
é uma decisão válida. A gravação magnética inicial permanece idêntica.

RAW: sem normalização/pós-processamento. MATCHED: RMS estéreo comum0–6s,
ganho único por arquivo relativo a Vintage F3-A e trim comum para pico≤.98.
Nenhum limiter adicional nem normalização por repetição. Método não é LUFS.
Ganhos/parâmetros em render-provenance.json, fora da pasta cega.
True peak estimado separadamente na bancada por polyphase4x, sem oversampling
no DSP. Gabarito answer-key.json separado: não abrir antes de avaliar.

Preencha preferência/equivalência/rejeição e confiança nas26 avaliações
(13 cenas×2 formatos), exporte JSON e só então revele o gabarito.
Teste automatizado de browser não é audição humana. SHA-256 dos WAVs no
manifesto; sha256.json cobre os demais arquivos. Nenhuma candidata promovida.
Hardware permanece PENDING_PHYSICAL.
