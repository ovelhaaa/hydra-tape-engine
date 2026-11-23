# Phase 7.4C — Real-Time Feasibility (WASM & ESP32-S3)

## WebAssembly (48 kHz / 128 frames, 2.667 ms budget)

Measured in `host/web/tests/webClassicCAudioProcessing.test.mjs` against `build-web/web/hydra_dsp.wasm`:
- Mean block time: `0.1306 ms`
- P95 block time: `0.2098 ms`

## ESP32-S3 Firmware (`platformio.ini`, `src/main.cpp`)

- Production environment `esp32-s3-devkitc-1` remains configured for `M0` (`motionMode = 0`) by default.
- Experimental environment `esp32-s3-phase74c-unified` inherits all production flags (`-DBOARD_HAS_PSRAM`, `-O2`, `-ffast-math`, `-fno-finite-math-only`, `-DHYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING=1`) verified via `python -m platformio project config`.
- Serial CLI commands added in [src/main.cpp](../../src/main.cpp) via lock-free `HydraDspEsp32Adapter::postSetParameter()` SPSC queue: `motion 0|1|2|3`, `motion amt <0..100>`, `motion stereo <0..100>`, `motion`.
- **Hardware Note**: Local PlatformIO firmware compilation was blocked by Python 3.14 (`ERROR: Python version must be between 3.10 and 3.13`), and no hardware flashing was performed per project rules. Physical ESP32-S3 benchmark validation (`2,902.49 us` block deadline at 44.1 kHz / 128 frames) requires CI / authorized hardware run.
