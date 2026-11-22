# Fase 7.2E.5 — Automation Stress Test (ESP32-S3 Físico)

## Metodologia

Sequência reproduzível de **32 mudanças de parâmetros** em `3,86 s` (~120 ms entre comandos, cadência plausível de automação musical via CLI serial), exercitando:
- `fbk` (`55 -> 75 -> 95 -> 65 -> 85 -> 95 -> 55`)
- `hds` (`4 -> 7 -> 3 -> 7 -> 4`) e `ppg` (`0 -> 1 -> 0`)
- `tpa` (`45 -> 80 -> 90 -> 45 -> 85`), `tps` (`50 -> 80 -> 25 -> 50 -> 70`), `ton` (`50 -> 20 -> 90 -> 50`), `hbp` (`30 -> 80 -> 30`)
- `dly` (`500 -> 250 -> 750 -> 500`)
- `rev` (`0 -> 1 -> 0`) com `revmode 0`, `revdiv 0` e `fbk 85/95` em Multi-Head (`hds 7`, `ppg 1`)

## Comparação Antes × Depois da Otimização H6 (com `dltrace`)

| Execução | MaxParamUpd (µs) | Peak DSP (µs) | Full Work Peak (µs) | Margem (µs) | Overruns |
|---|---:|---:|---:|---:|---:|
| Antes H6 (`f027471-dirty`, `dltrace`) | 462 | 2.635 | 2.886 | 16,49 | 0 |
| Depois H6 (`a189487`, `dltrace`) | **186** | 2.504 | 2.634 | **268,49** | 0 |

## Resultado no Firmware de Produção (`esp32-s3-classic-c`, commit `a1894876`)

Log bruto: [automation_stress.txt](file:///c:/Users/devx/Documents/PlatformIO/Projects/hydra/docs/evidence/phase72e-realtime/optimized-c/automation_stress.txt)

- **Blocos processados:** `1.544`
- **Avg DSP Block Time:** `2.074 µs`
- **Peak DSP Block Time:** `2.517 µs`
- **Full Work Peak:** `2.560 µs` (**margem de `342,49 µs`**)
- **DSP Budget Overruns:** `0`
- **Full Work Overruns:** `0`
- **I2S TX Write Failures:** `0`
- **AudioHealth:** `NonZeroWords=256`, `SaturatedWords=0`, `TxBytes=1024`, `TxError=0`
- **Memória:** `Internal Free Heap = 116.912 bytes`, `SPIRAM Free = 1.663.048 bytes`, `Audio Stack Min Free = 27.268 bytes`
