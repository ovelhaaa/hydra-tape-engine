# Phase 6.1 — Guia de escuta

Use fones e monitores conhecidos, em ambiente silencioso. Desative melhorias do sistema operacional e confirme o roteamento estéreo. Comece com volume moderado: cenários 04, 09, 19 e 20 têm feedback elevado ou cadeias densas. O player começa em 35% de ganho de monitoração, sem autoplay. O mesmo ganho se aplica a A e B; não há normalização nem compensação de loudness.

## Preparação e pacote

Baixe o artifact `hydra-phase6-listening-lab` do workflow Listening Lab na PR #54, extraia o ZIP e leia `reports/validation-summary.md`. Se houver um ZIP interno, extraia-o também. O artifact tem retenção de 30 dias; após expirar, execute novamente o workflow. É necessário acesso ao repositório para baixar artifacts.

Na raiz extraída, execute `python -m http.server 8000 --bind 127.0.0.1` e abra `http://localhost:8000/player/`. Clique em **Open extracted package** e selecione a pasta que contém `manifest.json` e os WAVs/CSVs. A interface não busca artifacts remotos. Os arquivos permanecem no computador. Alternativamente, sirva a raiz do repositório e abra `/tools/phase6-listening-lab/`.

## Audição

Siga a ordem 01–20. Use Play/Pause, Stop e Restart. A/B conserva o ponto de reprodução: dois buffers partem no mesmo instante do AudioContext, com transição linear complementar de 8 ms. A interface não aplica efeitos. A conversão de sample rate do navegador é a mesma para A/B. Waveforms mostram o canal esquerdo; ouça ambos os canais.

Clique na forma de onda ou no progresso para navegar. Amplie com Zoom; use a barra horizontal para percorrer a forma de onda. Shift-arrastar seleciona trecho; os campos Start/End permitem ajuste preciso. Ative Loop selection para repeti-lo. Previous/Next event e os botões de eventos navegam pelos timestamps reais. Mudanças observadas na telemetria têm resolução de quatro blocos (~10,67 ms a 48 kHz). Controles programados são aplicados no início do bloco que contém o evento; MIDI/tap conservam timestamps em amostras. Marcadores não são provas de qualidade.

## O que ouvir

| Cenário | Foco |
|---|---|
| 01 Single Head Quarter | Clareza da pulsação e repetição de semínima |
| 02 Dotted Eighth | Balanço pontuado e transientes da bateria |
| 03 Multi-Head Stereo | Relação rítmica entre cabeças e imagem estéreo |
| 04 High Feedback Sustained | Contenção, saturação e continuidade após silenciar entrada |
| 05 Reverse Quarter | Fronteiras das janelas e clareza reversa |
| 06 Reverse Eighth | Janelas curtas sem estalos |
| 07 Reverse Triplet | Subdivisão tercinada e pulsação |
| 08 Reverse Tempo Transition | Continuidade ao mudar BPM |
| 09 Reverse High Feedback | Swell, estabilidade e ausência de runaway |
| 10 Internal BPM Steps | Degraus de BPM reais (não rampa contínua) |
| 11 Tap Tempo | Aquisição gradual do andamento |
| 12 MIDI Stable | Estabilidade após aquisição |
| 13 MIDI Jitter | Rejeição de variações e estabilidade percebida |
| 14 Clock Loss/Recovery | Holdover, perda e recuperação sem interrupções |
| 15 Stop/Continue | Caudas preservadas durante controle do transporte |
| 16 Source Switching | Transições Internal/MIDI/Internal |
| 17 Rapid Parameters | Mudanças rápidas sem interrupções involuntárias |
| 18 Activation/Bypass | Rampas de ativação e continuidade |
| 19 Long Tails | Decaimento e preservação após 1,2 s |
| 20 Multi-Effect | Densidade, estabilidade e separação estéreo |

Wow/flutter, ruído de fita, saturação, ecos reversos e mudanças de pitch durante automação podem ser intencionais. Estalos isolados, muting inesperado, deriva sustentada, caudas cortadas e runaway exigem investigação. Compare o trecho com A para distinguir transientes da fonte. Os trechos são curtos (4–6 s); não comprovam estabilidade prolongada.

## Registro e decisão

Para cada cenário, preencha os critérios aplicáveis, comentários e notas com timestamp, descrição e severidade. Use N/A para critérios sem aplicação. Estados: NOT REVIEWED, PASS, NEEDS ATTENTION e FAIL. Não existe decisão automática. Export reviews gera JSON independente do armazenamento local. Import reviews reabre avaliações do mesmo commit; entradas importadas substituem as decisões dos cenários correspondentes, preservando as demais. Faça backup antes de importar.

Picos, RMS, true peak estimado, correlação e diagnósticos de fronteiras orientam a investigação; não substituem audição. Métricas no manifesto foram calculadas sobre float antes da quantização PCM24; o escritor limita a [-1,1] para exportar WAV. O campo de clipping deve ser lido antes de interpretar o arquivo limitado. Não há garantia de identidade bit a bit entre toolchains; os hashes verificam o pacote específico. Native/WASM estrito continua separado, com thresholds inalterados e issue #48 aberta.

Aprovação musical exige decisão humana explícita nos 20 cenários, critérios aplicáveis preenchidos, nenhum FAIL ou problema bloqueante pendente e revisão das notas de atenção. Uma falha reproduzível deve incluir commit, cenário, versão A/B, timestamp, monitoração e JSON exportado. A Fase 6.1 pode ficar READY FOR HUMAN LISTENING com divergência histórica estrita explicitamente aberta; READY FOR MERGE REVIEW depende da decisão do responsável sobre todos os gates. Browser real de DSP e runtime físico ESP32-S3 não são comprovados pelo player offline.
