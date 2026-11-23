# Phase 7.4B — Mechanical Model & Variant Architecture

## 1. Compile-Time Selection (`HYDRA_PHASE74B_MOTION_MODE`)
- `0` — **M0: Classic C Motion Baseline** (bit-exact preservation of PR #58 Classic C).
- `1` — **M1: Motor Drift & Inertia**
- `2` — **M2: Capstan Flutter & Mechanical Texture**
- `3` — **M3: Worn Transport / Unstable Machine**

All experimental logic lives in `dsp/core/organic_motion_experiment.hpp` and is excluded when `HYDRA_PHASE74B_MOTION_MODE == 0`. No public ABI or `TapeParams` fields were modified.

## 2. Conceptual Shift: Velocity-Error Integration vs. Direct Position Modulation
In M1, M2, and M3, mechanical irregularities are modeled as **dimensionless fractional tape speed errors** $\epsilon[n] = \Delta v[n] / v_0$, integrated into read-head sample displacement via a leaky integrator:
$$\Delta d[n] = \alpha_{\text{leak}} \cdot \Delta d[n-1] + \epsilon[n]$$

Key properties:
- **Bounded drift:** $\alpha_{\text{leak}} = e^{-1 / (\tau_{\text{leak}} f_s)}$ restores average delay time to nominal without unbounded drift.
- **$C^1$-smooth trajectory:** Displacements cannot jump discontinuously.
- **Delay-time & sample-rate independence:** Pitch deviation in cents ($\Delta c \approx -1731.23 \cdot \epsilon$) is governed by speed error rather than delay length, and displacement in seconds ($\Delta d / f_s$) is invariant between 44.1 kHz and 48 kHz.
- **Safety bounds:** Soft damping near `0.18 * modBaseSamples` and hard clamp at `±0.22 * modBaseSamples`.

## 3. Variant Designs

### M1 — Motor Drift & Inertia
- **2nd-order damped motor inertia loop:** Band-limited torque noise (`torqueLP`) + ultra-slow thermal/reel-pack wander (`driftWander`) drive motor acceleration (`motorAcc`) with restoring spring toward nominal displacement (`-0.50 * dispNorm`) and viscous damping (`-0.75 * motorAcc`), integrated into `motorVel` and `motorDisp`.
- **Stereo coherence:** Common-mode motor displacement + `14%` heavily damped differential tape weave (`diffDisp`).
- **Controls:** `wowRate` scales torque/inertia bandwidth; `wowDepth` scales velocity excursion.

### M2 — Capstan Flutter & Mechanical Texture
- **Quasi-periodic capstan & pinch-roller oscillators:** Two incommensurate frequencies (`flutterRate` and `flutterRate * 1.6180339`), each frequency-modulated by a slow random walk (`freqJitter`) using `fastSin` (zero libm trig calls).
- **Band-limited velocity noise:** White noise band-passed around `0.45 * flutterRate` to `2.6 * flutterRate` (`bpHpState`, `bpLpState`), normalized for sample-rate and rate-independent RMS.
- **Stereo coherence:** Common-mode capstan/roller velocity + `18%` phase-offset bearing differential.

### M3 — Worn Transport / Unstable Machine
- **Foundation:** Combines M1 motor inertia drift and M2 quasi-periodic capstan flutter.
- **Transient transport perturbations:** Deterministic LCG event trigger (rate scaled by `tapeAge` and `wowDepth`, with a `0.38 s` cooldown) injects occasional speed-sag steps (`sagEnv`, 75% drag / 25% tension snap) coupled into a **2nd-order damped tension oscillator** ($\zeta = 0.34$, $f_n \approx 2.6\text{–}4.0\text{ Hz}$) for natural, non-repetitive recovery.
- **Stereo coherence:** Shared transport sag + `16%` differential tension wobble.
