# Phase 7.4A — Listening Decision & Comparison Guide

## Status: Awaiting Human Audition

Per Phase 7.4A decision rules, **no winner is declared from objective metrics alone**. Final approval depends on human listening of the rendered WAVs.

---

## How to Audition

1. Serve the repository root locally (or open the artifacts folder via a local HTTP server, e.g. `npx serve .` or `python -m http.server 8000`).
2. Open `artifacts/phase7-4a/comparisons/index.html` in a browser.
3. Switch seamlessly between **C0 (Classic C Baseline)**, **C1 (Dynamic Spectral Wear)**, **C2 (Transient Softening)**, **C3 (Combined Aging)**, and **Dry** across all 10 scenarios.

---

## Preliminary Technical Assessment

- **C0 (Classic C Baseline):** Preserved bit-exact. Remains the musical reference.
- **C1 (Dynamic Spectral Wear):** Very subtle, controlled HF wear (`-20 Hz` centroid shift by E4 in Single-Head, `-0.5 dB` H/L shift at high feedback). Safe against muddiness on high `tapeAge` and sustained chords.
- **C2 (Transient Softening):** Most distinct evolution on plucked transients (`-81 Hz` centroid by E4 in moderate feedback, `-1.25 dB` H/L shift in high feedback) while leaving E0 untouched and sustained chords transparent.
- **C3 (Combined Aging):** Intermediate behavior (`0.75x` depth each), smoothly bridging C1 and C2 without over-darkening.

**Next Step:** Human A/B listening on `01_single_head_moderate_fb`, `02_single_head_high_fb`, `03_multi_head_three`, `05_guitar_pluck_transients`, and `06_sustained_chords` to decide whether C1, C2, or C3 warrants promotion or tuning.
