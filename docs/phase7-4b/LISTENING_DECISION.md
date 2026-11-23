# Phase 7.4B — Listening Decision (Pending Human Audition)

## Status
**PENDING HUMAN A/B AUDITION**

No variant (M0, M1, M2, or M3) is declared a winner by automated metrics.

## Phase 7.4B.1 Verification Readiness
- **WASM C API Contract:** Fixed (`hydra_dsp_create` 3-arg out-pointer + `hydra_dsp_prepare` stereo channels `2`) and gated by `scripts/test_phase74b_wasm_api.mjs`.
- **Demodulator & Isolation:** Calibrated and verified against positive/negative FM controls (`scripts/test_phase74b_demodulator.mjs`).
- **Blind A/B Player:** Fixed slot-to-gain synchronization when toggling Blind Mode before or during playback (`scripts/test_phase74b_player.mjs`).

## Audition Procedure
1. Download the GitHub Actions artifact `hydra-phase74b-organic-motion-listening` from the `Phase 7.4B Organic Tape Motion Listening & Validation` workflow run.
2. Extract the archive and serve locally:
   ```powershell
   python -m http.server 8080
   ```
3. Open `http://localhost:8080/comparisons/index.html` and evaluate all 10 scenarios using both direct A/B switching (`M0 | M1 | M2 | M3`) and **Blind A/B Mode**.

## Perceptual Evaluation Criteria
- Is the difference clearly audible compared to M0 (Classic C)?
- Does the motion feel natural and physically plausible?
- Does it enrich sustained chords and clean guitar notes?
- Does it avoid excessive detuning?
- Does it improve perceived depth and stereo dimensionality without collapsing center focus?
- Does it preserve pick attack definition?
- Does it remain musical under high feedback (`07-single-head-high-fb`) and Reverse (`10-reverse-feedback`)?

## Next Step
After human listening approval, decide whether any 7.4B motion variant merits ESP32-S3 hardware profiling (`NOT_EXECUTED` so far) and/or combination with 7.4A (PR #59) in a future phase.
