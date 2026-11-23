# Phase 7.4C — Listening Package & Acceptance Metrics

Generated via [scripts/generate_phase74c_listening.mjs](../../scripts/generate_phase74c_listening.mjs) and [scripts/verify_phase74c_equivalence.py](../../scripts/verify_phase74c_equivalence.py) from the unified `build-web/web/hydra_dsp.wasm` binary into `artifacts/phase7-4c/`:

- `audio/classic.wav` — RMS `0.149361`, Peak `0.561363`, Stereo Diff RMS `0.001758`
- `audio/motor.wav` — RMS `0.145741`, Peak `0.554650`, Stereo Diff RMS `0.010476`, RMS diff vs Classic `0.155535`
- `audio/capstan.wav` — RMS `0.141674`, Peak `0.555397`, Stereo Diff RMS `0.015197`, RMS diff vs Classic `0.096774`
- `audio/worn.wav` — RMS `0.138119`, Peak `0.496047`, Stereo Diff RMS `0.014870`, RMS diff vs Classic `0.189382`
- `audio/runtime-switch.wav` (`M0 -> M1 -> M2 -> M3 -> M0` at 85% feedback) — RMS `0.171507`, Peak `0.590899`, Stereo Diff RMS `0.027651`
- `parity/m{1,2,3}-{reference,unified}.wav` — Bit-exact (`rms_diff = 0.0`, `correlation = 1.0`) vs Phase 7.4B (`origin/experiment/phase7-4b-organic-motion`) at nominal settings (`Motion Amount = 100`, `Stereo Motion = 100`, advanced = `50`)

## Acceptance Status

- [x] `M0_BIT_EXACT_PASS`: M0 is bit-exact (`max_abs_diff = 0.0`) against frozen Classic C (`origin/feat/phase7-3-classic-c-web-default`) across all 8 scenarios.
- [x] `M1_M2_M3_PARITY_PASS`: M1, M2, and M3 are bit-exact against Phase 7.4B at nominal settings.
- [x] Runtime switching works without resetting delay, position/velocity spikes, or clicking under high feedback.
- [x] Web UI, AudioWorklet, Presets, Offline WAV export, Native tests, Issue #48 Native/WASM equivalence, and Playwright browser tests pass.
- [x] Physical ESP32-S3 hardware benchmark audited (`COM15`, zero overruns; see [REALTIME_FEASIBILITY.md](REALTIME_FEASIBILITY.md)).
- [ ] Human listening sign-off (pending review of `hydra-phase74c-unified-motion-listening` artifact).
