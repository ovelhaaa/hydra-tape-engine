# Phase 7.4A — Spectral & Objective Analysis (`C0 × C1 × C2 × C3`)

**Source Manifest:** [`artifacts/phase7-4a/manifest.json`](file:///c:/Users/devx/Documents/PlatformIO/Projects/hydra/artifacts/phase7-4a/manifest.json)  
**Generator:** [`scripts/generate_phase74a_listening.mjs`](file:///c:/Users/devx/Documents/PlatformIO/Projects/hydra/scripts/generate_phase74a_listening.mjs)  
**Format:** 48,000 Hz, 16-bit PCM stereo WAV, unity gain (no independent normalization).

---

## 1. Cross-Scenario Summary

| Scenario | Variant | RMS | Peak | Crest (dB) | Centroid (Hz) | H/L Ratio (dB) | Tail RMS (dB) | Non-Finite |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| `01_single_head_moderate_fb` | C0 | 0.037535 | 0.600354 | 24.08 | 572.2 | 4.75 | -86.34 | 0 |
| `01_single_head_moderate_fb` | C1 | 0.037510 | 0.600354 | 24.09 | 569.3 | 4.63 | -87.26 | 0 |
| `01_single_head_moderate_fb` | C2 | 0.037322 | 0.600354 | 24.13 | 561.1 | 4.53 | -88.41 | 0 |
| `01_single_head_moderate_fb` | C3 | 0.037441 | 0.600354 | 24.10 | 564.1 | 4.53 | -88.31 | 0 |
| `02_single_head_high_fb` | C0 | 0.064915 | 0.825646 | 22.09 | 596.7 | 9.03 | -42.40 | 0 |
| `02_single_head_high_fb` | C1 | 0.064529 | 0.828354 | 22.17 | 589.5 | 8.52 | -43.63 | 0 |
| `02_single_head_high_fb` | C2 | 0.063055 | 0.830258 | 22.39 | 577.0 | 7.78 | -45.86 | 0 |
| `02_single_head_high_fb` | C3 | 0.063813 | 0.833399 | 22.32 | 581.2 | 7.98 | -45.36 | 0 |
| `03_multi_head_three` | C0 | 0.042450 | 0.604840 | 23.08 | 715.0 | 9.28 | -80.87 | 0 |
| `03_multi_head_three` | C1 | 0.042428 | 0.604840 | 23.08 | 712.1 | 9.15 | -83.47 | 0 |
| `03_multi_head_three` | C2 | 0.042459 | 0.604840 | 23.07 | 709.8 | 9.03 | -84.69 | 0 |
| `03_multi_head_three` | C3 | 0.042438 | 0.604840 | 23.08 | 709.4 | 9.00 | -86.17 | 0 |
| `04_high_tape_age` | C0 | 0.184931 | 0.786550 | 12.57 | 312.3 | -55.08 | -36.93 | 0 |
| `04_high_tape_age` | C1 | 0.185302 | 0.788008 | 12.57 | 312.6 | -55.10 | -36.92 | 0 |
| `04_high_tape_age` | C2 | 0.185277 | 0.787391 | 12.57 | 312.6 | -55.08 | -36.92 | 0 |
| `04_high_tape_age` | C3 | 0.185614 | 0.788739 | 12.57 | 312.9 | -55.10 | -36.91 | 0 |
| `05_guitar_pluck_transients` | C0 | 0.056633 | 0.618043 | 20.76 | 720.5 | 9.37 | -54.81 | 0 |
| `05_guitar_pluck_transients` | C1 | 0.056627 | 0.613696 | 20.70 | 717.7 | 9.28 | -55.24 | 0 |
| `05_guitar_pluck_transients` | C2 | 0.056626 | 0.630478 | 20.93 | 711.7 | 9.10 | -56.62 | 0 |
| `05_guitar_pluck_transients` | C3 | 0.056693 | 0.617619 | 20.74 | 714.1 | 9.16 | -55.86 | 0 |
| `06_sustained_chords` | C0 | 0.136868 | 0.623813 | 13.18 | 308.4 | -53.76 | -39.24 | 0 |
| `06_sustained_chords` | C1 | 0.137073 | 0.625219 | 13.18 | 308.1 | -53.87 | -39.28 | 0 |
| `06_sustained_chords` | C2 | 0.137068 | 0.625058 | 13.18 | 308.2 | -53.94 | -39.28 | 0 |
| `06_sustained_chords` | C3 | 0.137256 | 0.626266 | 13.18 | 308.0 | -53.95 | -39.31 | 0 |
| `07_reverse_feedback` | C0 | 0.055881 | 0.619549 | 20.90 | 718.6 | 9.26 | -59.37 | 0 |
| `07_reverse_feedback` | C1 | 0.056018 | 0.619549 | 20.88 | 716.0 | 9.12 | -59.62 | 0 |
| `07_reverse_feedback` | C2 | 0.056851 | 0.625552 | 20.83 | 710.2 | 8.85 | -60.63 | 0 |
| `07_reverse_feedback` | C3 | 0.056432 | 0.619549 | 20.81 | 712.4 | 8.89 | -60.01 | 0 |
| `08_ping_pong` | C0 | 0.042841 | 0.604846 | 23.00 | 711.5 | 9.13 | -80.51 | 0 |
| `08_ping_pong` | C1 | 0.042892 | 0.604846 | 22.99 | 710.1 | 9.12 | -80.83 | 0 |
| `08_ping_pong` | C2 | 0.042932 | 0.604846 | 22.98 | 708.5 | 9.10 | -81.27 | 0 |
| `08_ping_pong` | C3 | 0.042976 | 0.604846 | 22.97 | 708.7 | 9.13 | -81.14 | 0 |
| `09_silence_after_stimulus` | C0 | 0.039961 | 0.615250 | 23.75 | 580.6 | 7.13 | -56.33 | 0 |
| `09_silence_after_stimulus` | C1 | 0.039851 | 0.615250 | 23.77 | 579.2 | 7.09 | -56.34 | 0 |
| `09_silence_after_stimulus` | C2 | 0.039342 | 0.615256 | 23.88 | 574.1 | 6.74 | -56.35 | 0 |
| `09_silence_after_stimulus` | C3 | 0.039611 | 0.615250 | 23.82 | 576.6 | 6.94 | -56.36 | 0 |
| `10_control_automation` | C0 | 0.092092 | 0.540466 | 15.37 | 361.3 | -21.61 | -42.61 | 0 |
| `10_control_automation` | C1 | 0.092136 | 0.539119 | 15.35 | 360.7 | -21.66 | -42.73 | 0 |
| `10_control_automation` | C2 | 0.092166 | 0.540588 | 15.37 | 360.7 | -21.78 | -42.72 | 0 |
| `10_control_automation` | C3 | 0.092186 | 0.538136 | 15.32 | 360.3 | -21.72 | -42.84 | 0 |

---

## 2. Single-Head Per-Echo Spectral Evolution

### `01_single_head_moderate_fb` (320 ms delay, feedback = 55)

| Variant | E0 (Dry + Init) | E1 | E2 | E3 | E4 |
|---|---|---|---|---|---|
| **C0** | 507.3 Hz / 15.44 dB | 608.1 Hz / 14.06 dB | 611.1 Hz / 14.88 dB | 647.4 Hz / 14.99 dB | 692.2 Hz / 15.86 dB |
| **C1** | 507.3 Hz / 15.44 dB | 603.9 Hz / 14.02 dB | 605.4 Hz / 14.80 dB | 635.7 Hz / 14.93 dB | 672.3 Hz / 15.80 dB |
| **C2** | 507.3 Hz / 15.44 dB | 594.2 Hz / 13.97 dB | 577.7 Hz / 15.23 dB | 587.6 Hz / 14.72 dB | 610.8 Hz / 15.89 dB |
| **C3** | 507.3 Hz / 15.44 dB | 597.1 Hz / 13.94 dB | 592.3 Hz / 14.82 dB | 609.8 Hz / 14.71 dB | 633.4 Hz / 15.90 dB |

### `02_single_head_high_fb` (280 ms delay, feedback = 85)

| Variant | E0 (Dry + Init) | E1 | E2 | E3 | E4 |
|---|---|---|---|---|---|
| **C0** | 525.8 Hz / 14.29 dB | 599.3 Hz / 12.89 dB | 604.0 Hz / 12.25 dB | 624.0 Hz / 13.86 dB | 656.2 Hz / 13.10 dB |
| **C1** | 525.8 Hz / 14.29 dB | 599.0 Hz / 12.94 dB | 598.1 Hz / 12.12 dB | 611.7 Hz / 13.90 dB | 634.4 Hz / 13.10 dB |
| **C2** | 525.8 Hz / 14.29 dB | 596.8 Hz / 13.07 dB | 584.2 Hz / 12.48 dB | 585.3 Hz / 13.56 dB | 591.7 Hz / 13.06 dB |
| **C3** | 525.8 Hz / 14.29 dB | 598.0 Hz / 13.05 dB | 590.9 Hz / 12.14 dB | 596.9 Hz / 13.85 dB | 607.5 Hz / 12.93 dB |

---

## 3. Key Observations

1. **Initial Echo Integrity (E0):** Across all Single-Head scenarios, E0 is identical in centroid and crest factor across C0, C1, C2, and C3, confirming the direct signal and first delay pass are unaffected.
2. **H1 & H2 Verification:** In C0, `feedbackHPF` (300 Hz) + `TapeMagnetics` harmonics cause the centroid of later repeats to drift slightly upward (`608 → 692 Hz` in scenario 01). C1 counters this gently (`672 Hz`), while C2's transient-selective softening keeps repeats measurably warmer (`611 Hz` in 01, `592 Hz` in 02).
3. **H3 Material Dependence:** On sustained chords (`06_sustained_chords`) and high tape age (`04_high_tape_age`), C1/C2/C3 centroids stay within `±0.6 Hz` of C0 — proving they do not over-darken sustained harmonic material or already-aged tape.
4. **Safety & Noise:** Zero non-finite samples, DC offset within `±1.1e-3` (matching C0), and `09_silence_after_stimulus` tail noise stays at `-56.33` to `-56.36 dB` across all variants.
