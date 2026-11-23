# Phase 7.4C — Listening Package & Acceptance Metrics

Generated via [scripts/generate_phase74c_listening.mjs](../../scripts/generate_phase74c_listening.mjs) from the unified `build-web/web/hydra_dsp.wasm` binary into `artifacts/phase7-4c/`:

- `m0_classic.wav` — RMS `0.149361`, Peak `0.561363`, Stereo Diff RMS `0.001758`
- `m1_motor_drift.wav` — RMS `0.145742`, Peak `0.554650`, Stereo Diff RMS `0.010476`, RMS diff vs M0 `0.155535`
- `m2_capstan_flutter.wav` — RMS `0.141633`, Peak `0.555396`, Stereo Diff RMS `0.015153`, RMS diff vs M0 `0.096751`
- `m3_worn_transport.wav` — RMS `0.138084`, Peak `0.496033`, Stereo Diff RMS `0.014846`, RMS diff vs M0 `0.189374`
- `runtime_switch_high_feedback.wav` (`M0 -> M1 -> M2 -> M3 -> M0` at 85% feedback) — RMS `0.171507`, Peak `0.590899`, Stereo Diff RMS `0.027651`

## Acceptance Status

- [x] M0 preserves Classic C behavior (`webClassicCAudioProcessing.test.mjs` baseline hashes `f047707776c5019c` / `1fd53bdbe2d3f12e`).
- [x] M1, M2, M3 preserve distinct modulation characters and nominal gain.
- [x] Runtime switching works without resetting delay or clicking under high feedback.
- [x] Web UI, AudioWorklet, Presets, Offline WAV export, Native tests, and Playwright browser tests pass.
- [ ] Physical ESP32-S3 hardware benchmark & human listening sign-off (pending authorized flash & hardware session).
