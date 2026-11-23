# Phase 7.4B — Listening Decision (Approved)

## Status
**APPROVED BY HUMAN A/B AUDITION**

All three experimental motion variants (**M1 — Motor Drift**, **M2 — Capstan Flutter**, and **M3 — Worn Transport**) were evaluated via the Phase 7.4B.1 A/B Lab (`comparisons/index.html`) across the 10 scenarios and **all three were judged to sound good and surpass M0 (Classic C)**.

## Phase 7.4B.1 Verification Readiness
- **WASM C API Contract:** Fixed (`hydra_dsp_create` 3-arg out-pointer + `hydra_dsp_prepare` stereo channels `2`) and gated by `scripts/test_phase74b_wasm_api.mjs`.
- **Demodulator & Isolation:** Calibrated and verified against positive/negative FM controls (`scripts/test_phase74b_demodulator.mjs`).
- **Blind A/B Player:** Fixed slot-to-gain synchronization when toggling Blind Mode before or during playback (`scripts/test_phase74b_player.mjs`).
- **Physical ESP32-S3 Profiling:** Completed on hardware (`COM15`, 240 MHz, 44.1 kHz, 128-frame blocks). All four variants (`M0`, `M1`, `M2`, `M3`) achieved **0 overruns** across standard, Multi-Head + Spring, and Multi-Head + Reverse + Spring loads (see `docs/phase7-4b/REALTIME_FEASIBILITY.md`).

## Next Step
Decide which candidate (or combination with Phase 7.4A Repeat Aging in PR #59) to promote in the next integration phase, and whether to remove the constant `Depth=0` floor in `OrganicMotionCoeffs::update()` documented in `docs/phase7-4b/MODULATION_ANALYSIS.md`.
