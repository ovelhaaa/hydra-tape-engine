# Phase 7.4C — Real-Time Feasibility (WASM & ESP32-S3)

## WebAssembly (48 kHz / 128 frames, 2.667 ms budget)

Measured in `host/web/tests/webClassicCAudioProcessing.test.mjs` against `build-web/web/hydra_dsp.wasm`:
- Mean block time: `0.1145 ms`
- P95 block time: `0.1701 ms`

## ESP32-S3 Firmware (`platformio.ini`, `src/main.cpp`)

- Production environment `esp32-s3-devkitc-1` remains configured for `M0` (`motionMode = 0`) by default.
- Experimental environment `esp32-s3-phase74c-unified` inherits all production flags (`-DBOARD_HAS_PSRAM`, `-O2`, `-ffast-math`, `-fno-finite-math-only`, `-DHYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING=1`).
- Serial CLI commands added in [src/main.cpp](../../src/main.cpp) via lock-free `HydraDspEsp32Adapter::postSetParameter()` SPSC queue: `motion 0|1|2|3`, `motion amt <0..100>`, `motion stereo <0..100>`, `motion`.

## Physical ESP32-S3 Benchmark (`COM15`, 44.1 kHz / 128 frames, 2,902.49 us budget)

- **Firmware**: `esp32-s3-phase74c-unified` (`.pio/build/esp32-s3-phase74c-unified/firmware.bin`)
- **SHA256**: `59CBB93FCE0BD6482217F777E58C59C1C2D4D3A5E9146360B108C1B8EFEAE9EA` (verified against local `firmware.bin` and `benchmark_results.json`)
- **Artifacts**: `artifacts/phase7-4c/physical/benchmark_results.json`, `artifacts/phase7-4c/physical/serial_log.txt` (local build/hardware artifacts; `artifacts/` and `.pio/` are gitignored and not committed to git)

| Mode | Scenario | Mean (us) | P99 (us) | Peak (us) | Budget Mean | Overruns |
|---|---|---:|---:|---:|---:|---:|
| M0 | `1_standard_single_head` | 1553.14 | 1614 | 1829 | 53.51% | 0 |
| M0 | `2_multihead_spring` | 1785.21 | 1844 | 2007 | 61.51% | 0 |
| M0 | `3_multihead_reverse_spring` | 1793.64 | 1858 | 2067 | 61.80% | 0 |
| M1 | `1_standard_single_head` | 1509.03 | 1641 | 1821 | 51.99% | 0 |
| M1 | `2_multihead_spring` | 1787.43 | 1881 | 2135 | 61.58% | 0 |
| M1 | `3_multihead_reverse_spring` | 1792.77 | 1902 | 2099 | 61.77% | 0 |
| M2 | `1_standard_single_head` | 1595.85 | 1763 | 1955 | 54.98% | 0 |
| M2 | `2_multihead_spring` | 1816.69 | 1949 | 2207 | 62.59% | 0 |
| M2 | `3_multihead_reverse_spring` | 1820.67 | 1946 | 2226 | 62.73% | 0 |
| M3 | `1_standard_single_head` | 1714.07 | 1834 | 2063 | 59.06% | 0 |
| M3 | `2_multihead_spring` | 2017.91 | 2129 | 2340 | 69.52% | 0 |
| M3 | `3_multihead_reverse_spring` | 1973.82 | 2129 | 2331 | 68.00% | 0 |
| `0->1->2->3->0` + Auto | `3_multihead_reverse_spring` | 1897.52 | 1953 | 2239 | 65.38% | 0 |
