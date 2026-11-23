# Phase 7.4B — Listening Decision (Approved)

## Status
**APPROVED BY HUMAN A/B AUDITION**

**User listening decision:** All three experimental motion variants (**M1 — Motor Drift**, **M2 — Capstan Flutter**, and **M3 — Worn Transport**) were described as sounding very good and are accepted as viable musical options. No specific ranking against M0, or per-scenario human scores, was provided. **M0 remains the reference/production default.**

## Phase 7.4B.1 Verification Readiness
- **WASM C API Contract:** Fixed (`hydra_dsp_create` 3-arg out-pointer + `hydra_dsp_prepare` stereo channels `2`) and gated by `scripts/test_phase74b_wasm_api.mjs`.
- **Demodulator & Isolation:** Calibrated and verified against positive/negative FM controls (`scripts/test_phase74b_demodulator.mjs`).
- **Blind A/B Player:** Fixed slot-to-gain synchronization when toggling Blind Mode before or during playback (`scripts/test_phase74b_player.mjs`).
- **Physical ESP32-S3 Profiling:** An earlier COM15 run reported zero overruns, but its firmware build flags were not archived or verified. That result is now **PRELIMINARY** pending a new run with explicit production-flag inheritance and raw telemetry evidence. **No new flashing was performed for this correction.** See `docs/phase7-4b/REALTIME_FEASIBILITY.md`.

## Next Step
Keep M1/M2/M3 available as musical candidates. First rerun and verify physical timing on correctly compiled firmware. Then prototype a runtime-selectable motion engine and Web UI (Phase 7.4C) without promoting the experimental modes automatically. Evaluate the constant `Depth=0` modulation floor in `OrganicMotionCoeffs::update()` as an explicit control-contract decision.
