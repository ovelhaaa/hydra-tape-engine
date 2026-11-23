# Phase 7.4B — Objective Modulation Analysis (Calibrated)

## 1. Methodology & Signal Isolation

### Mixing-Chain Isolation
In `dsp/core/tape_core.cpp`, the stereo output is:
`output = outputLimiter((input * dryGain) + tapeOutput)`, where `dryGain = 1.0 - textureGain`, `textureGain = dryWet * 0.01`, `delayGain = delayWet * 0.01`, and `tapeSig = (textureSig * textureGain) + (delaySig * delayGain)`.

The provisional Phase 7.4B probe used `dryWet=0, delayWet=100`, which left `dryGain=1.0` and mixed the unmodulated dry 1 kHz carrier directly with the delayed 1 kHz carrier, creating phase-cancellation interference that inflated apparent pitch deviation (notably on M0).

The calibrated probe isolates a single delayed 1 kHz sine carrier (`-9.1 dBFS` / `0.35` peak) using existing API controls:
- `delayActive=1`, `delayTimeMs=300`, `feedback=0`, `activeHeads=4` (head 3 only)
- `dryWet=100` (`dryGain=0.0`, `textureGain=1.0`), `delayWet=0` (`delayGain=0.0`)
- `drive=20` (unity record gain `1.0`), `noise=0`, `dropoutSeverity=0`, `headBumpAmount=0`, `azimuthError=0`, `tone=50`
- Because `delayActive=1`, `modBaseSamples` remains `smoothedDelaySamples` (`14,400` samples @ 48 kHz), while `textureSig` reads a single isolated tap at `200.0 + mod` samples (verified non-clamping since peak excursion stays within `±155` samples).

### Calibrated Quadrature Demodulator
- **Filter:** 2-stage cascaded 1st-order IIR lowpass (`fc = 35 Hz`, `-12 dB/oct` rolloff) on `I` and `Q` to suppress `2 * fc = 2000 Hz` carrier ripple.
- **Warm-up:** `0.60 s` delay settling + `0.25 s` I/Q filter warm-up before any phase sample is recorded, eliminating initialization transients.
- **Analysis Window:** `8.35 s` (`8,192` decimated samples at `1000 Hz` demodulation rate for 48 kHz; `1002.27 Hz` for 44.1 kHz).
- **Spectral Analysis:** Hann-windowed DFT (`N = 8,192`, `Δf = 0.122 Hz`, effective window `8.19 s` @ 48 kHz / `8.17 s` @ 44.1 kHz) after DC mean removal. Wow band: `0.1–3.0 Hz`; Flutter band: `3.0–25.0 Hz`.
- **Calibration Controls (`scripts/test_phase74b_demodulator.mjs`):**
  - Unmodulated 1 kHz sine: `0.00000 cents` RMS @ 48 kHz, `0.00172 cents` RMS @ 44.1 kHz.
  - 0.8 Hz FM (10 cents peak, 7.07 cents RMS): `7.06 cents` RMS @ `0.85 Hz`.
  - 6.0 Hz FM (8 cents peak, 5.66 cents RMS): `5.51 cents` RMS @ `5.98 Hz`.
  - +5 Hz static offset (+8.634 cents): `8.635 cents` mean, `< 0.02 cents` AC RMS.
  - -15 cents transient sag: `14.60 cents` peak recovered.

## 2. Calibrated Modulation Measurements (WASM, 48 kHz & 44.1 kHz)

Test regimes:
- **Minimal:** `wowDepth=0`, `flutterDepth=0`, `tapeAge=0` (`wowRate=0.8 Hz`, `flutterRate=6.0 Hz`)
- **Subtle:** `wowDepth=15`, `flutterDepth=15`, `tapeAge=40`
- **Expressive:** `wowDepth=50`, `flutterDepth=45`, `tapeAge=70`

| Variant | Regime / SR | RMS (cents) | Max (cents) | P50 / P90 / P99 (cents) | Max Exc (ms) | Wow 0.1–3 Hz (cents) | Flutter 3–25 Hz (cents) | Events (Max Dur) | L/R Corr |
|---|---|---:|---:|---|---:|---:|---:|---|---:|
| **M0** | Minimal @ 48k | 0.01 | 0.03 | 0.01 / 0.02 / 0.02 | 0.001 | 0.00 | 0.01 | 0 (0 ms) | 0.913 |
| **M0** | Subtle @ 48k | 0.10 | 0.37 | 0.07 / 0.17 / 0.26 | 0.004 | 0.01 | 0.08 | 0 (0 ms) | -0.006 |
| **M0** | Expressive @ 48k | 0.30 | 1.09 | 0.20 / 0.50 / 0.79 | 0.012 | 0.05 | 0.24 | 0 (0 ms) | -0.014 |
| **M0** | Expressive @ 44.1k | 0.32 | 1.15 | 0.21 / 0.53 / 0.86 | 0.014 | 0.05 | 0.26 | 0 (0 ms) | -0.018 |
| **M1** | Minimal @ 48k | 1.68 | 3.43 | 1.33 / 2.79 / 3.41 | 1.990 | 1.91 | 0.01 | 0 (0 ms) | 1.000 |
| **M1** | Subtle @ 48k | 4.33 | 9.73 | 3.26 / 6.73 / 9.70 | 4.344 | 5.05 | 0.03 | 0 (0 ms) | 1.000 |
| **M1** | Expressive @ 48k | 8.66 | 24.11 | 3.15 / 14.16 / 23.94 | 6.228 | 10.40 | 1.20 | 1 (426 ms) | 0.997 |
| **M1** | Expressive @ 44.1k | 8.96 | 23.74 | 4.25 / 15.29 / 23.56 | 6.463 | 11.13 | 1.20 | 1 (311 ms) | 0.997 |
| **M2** | Minimal @ 48k | 0.75 | 2.21 | 0.52 / 1.23 / 1.81 | 0.162 | 0.24 | 0.71 | 4 (9 ms) | 0.805 |
| **M2** | Subtle @ 48k | 2.71 | 7.99 | 1.89 / 4.46 / 6.57 | 0.470 | 0.69 | 2.60 | 7 (14 ms) | 0.795 |
| **M2** | Expressive @ 48k | 6.68 | 20.09 | 4.64 / 11.01 / 16.27 | 1.172 | 1.73 | 6.40 | 7 (14 ms) | 0.795 |
| **M2** | Expressive @ 44.1k | 6.57 | 21.42 | 4.83 / 10.33 / 16.55 | 1.268 | 1.95 | 6.14 | 9 (17 ms) | 0.798 |
| **M3** | Minimal @ 48k | 1.52 | 4.20 | 1.05 / 2.57 / 3.69 | 1.303 | 1.13 | 0.55 | 3 (27 ms) | 0.975 |
| **M3** | Subtle @ 48k | 4.30 | 13.42 | 2.73 / 7.23 / 10.96 | 3.182 | 3.01 | 2.01 | 5 (36 ms) | 0.957 |
| **M3** | Expressive @ 48k | 9.44 | 33.71 | 4.78 / 16.71 / 28.65 | 5.600 | 5.90 | 4.19 | 7 (91 ms) | 0.956 |
| **M3** | Expressive @ 44.1k | 9.78 | 39.11 | 5.55 / 16.70 / 28.02 | 5.878 | 6.05 | 5.10 | 6 (73 ms) | 0.951 |

## 3. Why Provisional Measurements Changed
The provisional report showed `M0=2.59`, `M1=12.67`, `M2=8.57`, `M3=13.55` cents RMS in the expressive regime. With signal isolation and demodulator warm-up:
- **M0** dropped from `2.59` to **`0.30 cents`** because the dry+wet 1 kHz carrier cancellation and filter-start transient were removed. M0's actual pitch modulation at 300 ms delay is very subtle (`0.30 cents` RMS) and uncorrelated L/R (`-0.014`) due to independent L/R PRNG seeds.
- **M1, M2, M3** settled to **`8.66`, `6.68`, `9.44 cents`** RMS respectively, with consistent scaling between 44.1 kHz and 48 kHz (`< 4%` difference).

## 4. Control Semantics Audit (`scripts/test_phase74b_control_semantics.mjs`)
1. **Does Depth = 0 mean Modulation = 0?**
   - **M0:** Nearly zero (`0.011 cents` RMS at `wowDepth=0, flutterDepth=0, tapeAge=0`), leaving only `scrapeDepthScale` and a `0.03`/`0.01`-sample `residual` sine.
   - **M1 / M2 / M3:** **No.** `OrganicMotionCoeffs::update()` includes non-zero constant floors (`motorVelScale = 0.0020f + ...`, `flutterVelScale = 0.0010f + ...`, `eventVelScale = 0.0014f + ...`), yielding `2.01 cents` (M1), `0.76 cents` (M2), and `1.71 cents` (M3) even when all depth/age controls are `0`.
2. **Is there intentional residual modulation in Classic C?**
   - Yes (`residual` + `scrapeDepthScale` in `tape_core.cpp`), producing ~`0.01 cents` RMS.
3. **Do new engines preserve expected semantics?**
   - Monotonic scaling with `wowDepth`, `flutterDepth`, and `tapeAge` is preserved, but the non-zero floor at `Depth=0` breaks zero-depth silence expectations.
4. **Do Rate controls work properly?**
   - Yes: increasing `flutterRate` from `3.5 Hz` to `12.0 Hz` shifts M2's spectral peak from `3.50 Hz` to `11.65 Hz` (and M3 from `3.50 Hz` to `11.85 Hz`).
5. **Is modulation stable under Depth/Rate automation?**
   - Yes: 4-second continuous sweeps pass `checkAutomationNoGlitch` at 44.1 kHz and 48 kHz with zero non-finite samples.

**Proposed minimal fix (if zero-depth = zero-modulation is desired post-audition):**
Remove the constant floor terms in `OrganicMotionCoeffs::update()` (`motorVelScale = 0.0250f * wowDepthNorm`, `diffVelScale = 0.0033f * wowDepthNorm`, `flutterVelScale = 0.0190f * flutterDepthNorm`, `m2WowVelScale = 0.0065f * wowDepthNorm`, `eventVelScale = 0.0082f * (0.55f * wowDepthNorm + 0.45f * ageNorm)`), preserving `scrapeDepthScale` for mechanical floor parity with M0. Not applied yet per Phase 7.4B.1 DSP preservation rules.

## 5. Limitations
- The `dryWet=100, delayWet=0` isolation measures the `200 + mod` texture tap rather than the `14,400 + mod` delay tap; both share the exact same `mod` signal when `delayActive=1`, and peak excursion (`<= 6.5 ms` / `±155` samples) does not clamp `200 + mod`, though larger depths (> 60) could approach the `2.0`-sample lower bound.
- The `35 Hz` 2-pole demodulator LPF attenuates modulation components above ~`15–20 Hz` (e.g., `-0.25 dB` at `6 Hz`, `-6 dB` at `35 Hz`).
