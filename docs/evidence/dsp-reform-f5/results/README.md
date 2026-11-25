# F5 — Resultados versionados

Estes JSON são cópias dos artefatos de `artifacts/dsp-reform-f5/` (diretório ignorado
pelo Git). Os WAVs de escuta (66 arquivos, ~156 MB) não são versionados; o pacote é
recriado por `run.py render && run.py package` e publicado pelo workflow
`dsp-reform-f5.yml`. Hashes de todos os arquivos do pacote: `../render-manifest.json`.

| Arquivo | Conteúdo |
|---|---|
| `build.json` | HEAD, flags, SHA-256 das bibliotecas e fontes |
| `F5-A-reference.json` | F5=0 vs manifesto F3-A (10 cenários) |
| `STATIC_PRESERVATION.json` | B/C vs A sem automação de tempo |
| `INSTRUMENTATION_EQUIVALENCE.json` | hook vs build sem hook (39 cenas) |
| `BLOCK_SIZE_CONTRACT.json` | 37/64/128/256 no mesmo frame |
| `TIMING_MEASUREMENTS.json` | manual, ms, Tap, MIDI, modos, controles |
| `DRIFT_manual_60s*.json` | transporte e alvo após 60 s |
| `LONG_TAIL_CHECK.json` | caudas FB95 de 60 s |
| `REVERSE_VALIDATION.json` | 9 divisões, 120→90 |
| `FEEDBACK_STABILITY_v*.json` | gates FB95/multi-head/Reverse, 60 s |
| `listening-manifest.json`, `answer-key.json` | cegueira e comandos do pacote |

`build.json.head` é o commit usado para compilar/renderizar; o commit de documentação
posterior não altera DSP nem áudio. Regenere com `run.py build` se o código mudar.
