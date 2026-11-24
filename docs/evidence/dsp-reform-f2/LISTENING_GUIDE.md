# Audição F2

Extraia o pacote, abra um terminal na pasta `listening` e execute:

```
python -m http.server 8765 --bind 127.0.0.1
```

Abra http://127.0.0.1:8765/ no Chrome/Edge. Selecione a cena e RAW/MATCHED.
Reproduza e use X/Y/Z ou teclas 1/2/3 para trocar no mesmo instante da música.
Registre preferência (ou equivalência/rejeição), confiança e comentários.
As respostas são salvas no navegador; exporte o JSON antes de revelar o gabarito.
A revelação é liberada após preencher as 24 avaliações (12 cenas × 2 formatos).
O arquivo `answer-key.json` é separado; não o abra antes de avaliar.

Todas as oito fontes são **sintéticas determinísticas**; baixo é uma simulação,
não uma gravação DI. Cada cena tem 6 s de material + 10 s de cauda, estéreo
44.1 kHz, PCM24, feedback 50/75/85%, quatro modos. Casos 95/100% e caudas60s
ficam nas medições de estresse, evitando centenas de WAVs longos.

RAW não tem normalização nem pós-processamento. MATCHED é **RMS estéreo** na
janela comum 0–6 s, não LUFS: ganho constante por arquivo relativo à C0 e trim
comum para pico amostrado ≤0.98. Ganhos em `render-provenance.json` fora da
pasta cega. Nenhuma repetição é normalizada separadamente; nenhum limiter é
adicionado. Pico amostrado não é medição de true-peak reconstruído.

Observe corpo 80–300Hz, definição dos ataques, acúmulo/ringing, evolução do
brilho, acordes/notas graves e sustain. Maior volume ou energia não estabelece
qualidade. Pause entre blocos de avaliação; não há vencedora automática.
Este teste é escuta offline; hardware ESP32-S3 permanece PENDING_PHYSICAL.
