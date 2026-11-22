# Fase 7.2E.6 — Ensaios Sustentados na Placa Física (ESP32-S3)

Firmware avaliado: produção `esp32-s3-classic-c` (`a1894876ccbad094b51648eba59209032c79936f`, `FeedbackRouting=PHASE72_CLASSIC_C`), `COM15`, `44.100 Hz`, bloco `128 frames`, orçamento `2.902,49 µs`.

## 1. Ensaio Sustentado Reverse FB85 (300,0 s)

Configuração: `rev 1`, `revmode 0`, `revdiv 0`, `fbk 85`, `tpa 80`, `ftd 25`, `wwd 25`, `src 1`, `gen 1`, `vol 10`.  
Log bruto: [sustained_5min_reverse.txt](file:///c:/Users/devx/Documents/PlatformIO/Projects/hydra/docs/evidence/phase72e-realtime/optimized-c/sustained_5min_reverse.txt)

- **Blocos processados:** `103.439`
- **Avg DSP Block Time:** `2.022 µs`
- **Peak DSP Block Time:** `2.536 µs`
- **Full Work Peak:** `2.556 µs` (**margem de `346,49 µs`**)
- **DSP Budget Overruns:** `0`
- **Full Work Overruns:** `0`
- **I2S TX Write Failures:** `0`
- **Memória:** `Internal Free Heap = 116.912 bytes` (constante), `SPIRAM Free = 1.663.048 bytes` (constante), `Audio Stack Min Free = 27.268 bytes`
- **AudioHealth (início/meio/fim):** `NonZeroWords=256`, `SaturatedWords=0`, `TxBytes=1024`, `TxError=0`

## 2. Ensaio Sustentado Multi-Head (300,2 s)

Configuração: `hds 7`, `ppg 1`, `fbk 65`, `src 1`, `gen 1`, `vol 10`.  
Log bruto: [sustained_5min_multihead.txt](file:///c:/Users/devx/Documents/PlatformIO/Projects/hydra/docs/evidence/phase72e-realtime/optimized-c/sustained_5min_multihead.txt)

- **Blocos processados:** `103.496`
- **Avg DSP Block Time:** `2.190 µs`
- **Peak DSP Block Time:** `2.635 µs`
- **Full Work Peak:** `2.644 µs` (**margem de `258,49 µs`**)
- **DSP Budget Overruns:** `0`
- **Full Work Overruns:** `0`
- **I2S TX Write Failures:** `0`
- **Memória:** `Internal Free Heap = 116.912 bytes` (constante), `SPIRAM Free = 1.663.048 bytes` (constante), `Audio Stack Min Free = 27.268 bytes`
- **AudioHealth (início/meio/fim):** `NonZeroWords=256`, `SaturatedWords=0`, `TxBytes=1024`, `TxError=0`
