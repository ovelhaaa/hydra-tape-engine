# Phase 7.4A.1 — Spectral & Objective Analysis (`C0 × C1 × C2 × C3`)

**Source Manifest:** [`artifacts/phase7-4a/manifest.json`](file:///c:/Users/devx/Documents/PlatformIO/Projects/hydra/artifacts/phase7-4a/manifest.json)  
**Generator:** [`scripts/generate_phase74a_listening.mjs`](file:///c:/Users/devx/Documents/PlatformIO/Projects/hydra/scripts/generate_phase74a_listening.mjs)  
**Format:** 48,000 Hz, 16-bit PCM stereo WAV, unity gain (no independent normalization).

---

## 1. Cross-Scenario Summary (Post-Refinement Parallel Dry/Wet Mix)

| Scenario | Variant | RMS | Peak | Crest (dB) | Centroid (Hz) | H/L Ratio (dB) | Tail RMS (dB) | Non-Finite |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| `01_single_head_moderate_fb` | C0 | 0.037535 | 0.600354 | 24.08 | 572.2 | 4.75 | -86.34 | 0 |
| `01_single_head_moderate_fb` | C1 | 0.037531 | 0.600354 | 24.08 | 571.4 | 4.71 | -86.40 | 0 |
| `01_single_head_moderate_fb` | C2 | 0.037400 | 0.600354 | 24.11 | 562.9 | 4.52 | -86.95 | 0 |
| `01_single_head_moderate_fb` | C3 | 0.037511 | 0.600354 | 24.08 | 568.4 | 4.57 | -86.58 | 0 |
| `02_single_head_high_fb` | C0 | 0.064915 | 0.825646 | 22.09 | 596.7 | 9.03 | -42.40 | 0 |
| `02_single_head_high_fb` | C1 | 0.064837 | 0.825440 | 22.10 | 594.2 | 8.89 | -42.61 | 0 |
| `02_single_head_high_fb` | C2 | 0.064384 | 0.826927 | 22.17 | 583.6 | 8.36 | -43.45 | 0 |
| `02_single_head_high_fb` | C3 | 0.064685 | 0.826171 | 22.13 | 590.1 | 8.67 | -43.07 | 0 |
| `03_multi_head_three` | C0 | 0.042450 | 0.604840 | 23.08 | 715.0 | 9.28 | -80.87 | 0 |
| `03_multi_head_three` | C1 | 0.042447 | 0.604840 | 23.08 | 714.6 | 9.26 | -80.98 | 0 |
| `03_multi_head_three` | C2 | 0.042403 | 0.604840 | 23.08 | 712.8 | 9.16 | -81.45 | 0 |
| `03_multi_head_three` | C3 | 0.042436 | 0.604840 | 23.08 | 713.5 | 9.20 | -81.16 | 0 |
| `04_high_tape_age` | C0 | 0.184931 | 0.786550 | 12.57 | 312.3 | -55.08 | -36.93 | 0 |
| `04_high_tape_age` | C1 | 0.185051 | 0.787020 | 12.57 | 312.4 | -55.09 | -36.92 | 0 |
| `04_high_tape_age` | C2 | 0.184937 | 0.786543 | 12.57 | 312.3 | -55.07 | -36.93 | 0 |
| `04_high_tape_age` | C3 | 0.185017 | 0.786864 | 12.57 | 312.4 | -55.08 | -36.92 | 0 |
| `05_guitar_pluck_transients` | C0 | 0.056633 | 0.618043 | 20.76 | 720.5 | 9.37 | -54.81 | 0 |
| `05_guitar_pluck_transients` | C1 | 0.056624 | 0.617978 | 20.76 | 719.4 | 9.32 | -54.91 | 0 |
| `05_guitar_pluck_transients` | C2 | 0.056573 | 0.623945 | 20.85 | 713.1 | 9.17 | -55.46 | 0 |
| `05_guitar_pluck_transients` | C3 | 0.056629 | 0.619144 | 20.78 | 716.8 | 9.25 | -55.12 | 0 |
| `06_sustained_chords` | C0 | 0.136868 | 0.623813 | 13.18 | 308.4 | -53.76 | -39.24 | 0 |
| `06_sustained_chords` | C1 | 0.136932 | 0.624281 | 13.18 | 308.3 | -53.81 | -39.25 | 0 |
| `06_sustained_chords` | C2 | 0.136878 | 0.623805 | 13.17 | 308.4 | -53.82 | -39.25 | 0 |
| `06_sustained_chords` | C3 | 0.136918 | 0.624115 | 13.18 | 308.3 | -53.82 | -39.25 | 0 |
| `07_reverse_feedback` | C0 | 0.055881 | 0.619549 | 20.90 | 718.6 | 9.26 | -59.37 | 0 |
| `07_reverse_feedback` | C1 | 0.055903 | 0.619549 | 20.89 | 717.8 | 9.22 | -59.43 | 0 |
| `07_reverse_feedback` | C2 | 0.056071 | 0.619549 | 20.87 | 716.0 | 9.13 | -59.53 | 0 |
| `07_reverse_feedback` | C3 | 0.055998 | 0.619549 | 20.88 | 716.6 | 9.16 | -59.50 | 0 |
| `08_ping_pong` | C0 | 0.042841 | 0.604846 | 23.00 | 711.5 | 9.13 | -80.51 | 0 |
| `08_ping_pong` | C1 | 0.042848 | 0.604846 | 22.99 | 711.2 | 9.12 | -80.55 | 0 |
| `08_ping_pong` | C2 | 0.042856 | 0.604846 | 22.99 | 709.4 | 9.08 | -80.83 | 0 |
| `08_ping_pong` | C3 | 0.042855 | 0.604846 | 22.99 | 710.1 | 9.09 | -80.63 | 0 |
| `09_silence_after_stimulus` | C0 | 0.039961 | 0.615250 | 23.75 | 580.6 | 7.13 | -56.33 | 0 |
| `09_silence_after_stimulus` | C1 | 0.039942 | 0.615251 | 23.75 | 580.1 | 7.12 | -56.33 | 0 |
| `09_silence_after_stimulus` | C2 | 0.039561 | 0.615251 | 23.84 | 574.8 | 6.85 | -56.34 | 0 |
| `09_silence_after_stimulus` | C3 | 0.039850 | 0.615255 | 23.77 | 578.5 | 7.08 | -56.34 | 0 |
| `10_control_automation` | C0 | 0.092092 | 0.540466 | 15.37 | 361.3 | -21.61 | -42.61 | 0 |
| `10_control_automation` | C1 | 0.092099 | 0.540272 | 15.37 | 361.2 | -21.63 | -42.63 | 0 |
| `10_control_automation` | C2 | 0.092084 | 0.541049 | 15.38 | 360.9 | -21.62 | -42.61 | 0 |
| `10_control_automation` | C3 | 0.092099 | 0.540721 | 15.37 | 361.1 | -21.63 | -42.62 | 0 |

---

## 2. Single-Head Per-Echo Spectral Evolution (`E0–E4`)

### `01_single_head_moderate_fb` (320 ms delay, feedback = 55)

| Variant | E0 (Dry + Init) | E1 | E2 | E3 | E4 |
|---|---|---|---|---|---|
| **C0** | 507.3 Hz / 15.44 dB | 608.1 Hz / 14.06 dB | 611.1 Hz / 14.88 dB | 647.4 Hz / 14.99 dB | 692.2 Hz / 15.86 dB |
| **C1** | 507.3 Hz / 15.44 dB | 607.0 Hz / 14.06 dB | 609.5 Hz / 14.90 dB | 644.8 Hz / 14.98 dB | 688.6 Hz / 15.87 dB |
| **C2** | 507.3 Hz / 15.44 dB | 595.1 Hz / 14.01 dB | 585.9 Hz / 15.36 dB | 614.4 Hz / 14.65 dB | 649.2 Hz / 16.03 dB |
| **C3** | 507.3 Hz / 15.44 dB | 602.5 Hz / 14.03 dB | 602.8 Hz / 15.03 dB | 635.1 Hz / 14.88 dB | 675.2 Hz / 15.93 dB |

### `02_single_head_high_fb` (280 ms delay, feedback = 85)

| Variant | E0 (Dry + Init) | E1 | E2 | E3 | E4 |
|---|---|---|---|---|---|
| **C0** | 525.8 Hz / 14.29 dB | 599.3 Hz / 12.89 dB | 604.0 Hz / 12.25 dB | 624.0 Hz / 13.86 dB | 656.2 Hz / 13.10 dB |
| **C1** | 525.8 Hz / 14.29 dB | 599.0 Hz / 12.89 dB | 601.0 Hz / 12.25 dB | 618.5 Hz / 13.82 dB | 647.6 Hz / 13.05 dB |
| **C2** | 525.8 Hz / 14.29 dB | 596.9 Hz / 13.02 dB | 581.9 Hz / 12.36 dB | 594.9 Hz / 13.56 dB | 614.9 Hz / 12.71 dB |
| **C3** | 525.8 Hz / 14.29 dB | 598.5 Hz / 12.92 dB | 595.2 Hz / 12.32 dB | 610.3 Hz / 13.77 dB | 634.9 Hz / 12.93 dB |

---

## 3. Before vs. After Interpretation (Permanent Filtering vs. True Dynamic Modulation)

1. **How much of the prior C1/C2 difference was static LPF coloration?**
   - In `01_single_head_moderate_fb` (single pluck, moderate feedback), the prior C1 E4 centroid drop (`-19.9 Hz` vs C0) reduced to `-3.6 Hz` (`692.2 → 688.6 Hz`), showing that ~82% of the prior C1 shift on low-energy single plucks came from the always-in-series 11–14 kHz LPF rather than dynamic wear.
   - In the same scenario, C2's E4 centroid drop (`-81.4 Hz` prior vs `-43.0 Hz` post-fix) shows ~47% of the prior shift was static LPF coloration, while **53% (`-43.0 Hz`) is genuine transient-triggered attack softening** that accumulates across repeats.
2. **Sustained Content & High Tape Age Neutrality:**
   - On `06_sustained_chords` and `04_high_tape_age`, C2 matches C0 centroid identically (`308.4 Hz` and `312.3 Hz`), confirming that sustained harmonic material returns to transparency after the initial note onset.
   - C1 also remains within `0.1 Hz` of C0 on sustained chords and high tape age (`ageHeadroom` + `amount` scaling).
3. **High-Feedback Accumulation & C3 Balance:**
   - In `02_single_head_high_fb`, progressive HF attenuation is preserved cleanly: C0 (`656.2 Hz` @ E4, `9.03 dB` H/L) → C1 (`647.6 Hz`, `8.89 dB`) → C3 (`634.9 Hz`, `8.67 dB`) → C2 (`614.9 Hz`, `8.36 dB`), without over-darkening the tail.
4. **Safety & Noise:**
   - Zero non-finite samples across all 40 renders; `09_silence_after_stimulus` tail RMS remains `-56.33` to `-56.34 dB` across all variants.
