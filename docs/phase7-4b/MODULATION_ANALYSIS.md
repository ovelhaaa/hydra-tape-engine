# Phase 7.4B — Objective Modulation Analysis

## 1. Methodology
Instead of relying on RMS, peak, or spectral centroid alone, modulation behavior is measured directly via **quadrature demodulation of a 1000 Hz continuous sine carrier** (`delayActive=1`, `delayTimeMs=300`, `dryWet=0`, `delayWet=100`, `feedback=0`) at 48 kHz:
- Instantaneous phase $\phi(t)$ is extracted with a 35 Hz quadrature lowpass filter and unwrapped.
- Instantaneous pitch deviation in cents is computed as $\Delta c(t) = \frac{1200}{\ln 2} \cdot \frac{1}{2\pi f_c} \frac{d\phi}{dt}$.
- Full per-scenario metrics (wow band 0.1–3 Hz, flutter band 3–25 Hz, max excursion in ms, perturbation count & duration, L/R modulation correlation, stereo Mid/Side ratio, max sample delta, and high-feedback stability) are exported to `artifacts/phase7-4b/modulation-analysis.json` by `scripts/generate_phase74b_listening.mjs`.

## 2. Native Modulation Probe Observations (8 s, 48 kHz)

Two parameter regimes were measured natively on x86_64:
- **Subtle (`sc=0`):** `wowDepth=15`, `flutterDepth=15`, `tapeAge=40`
- **Expressive (`sc=1`):** `wowDepth=50`, `flutterDepth=45`, `tapeAge=70`

| Variant | Regime | RMS Pitch (cents) | L/R Mod Correlation | Character |
|---|---|---:|---:|---|
| **M0 (Classic C)** | Subtle (`sc=0`) | 2.53 | 0.998 | Direct position random walk + biquad flutter |
| **M0 (Classic C)** | Expressive (`sc=1`) | 2.59 | 0.990 | Minimal scaling on 300 ms carrier derivative |
| **M1 (Motor Drift)** | Subtle (`sc=0`) | 6.54 | 0.962 | Smooth inertia-coupled slow pitch drift |
| **M1 (Motor Drift)** | Expressive (`sc=1`) | 12.67 | 0.812 | Deeper correlated motor wander + subtle stereo weave |
| **M2 (Capstan Flutter)** | Subtle (`sc=0`) | 4.55 | 0.835 | Structured quasi-periodic capstan + band-limited noise |
| **M2 (Capstan Flutter)** | Expressive (`sc=1`) | 8.57 | 0.774 | Richer capstan/roller texture with bearing differential |
| **M3 (Unstable Transport)** | Subtle (`sc=0`) | 6.63 | 0.941 | Combined drift/flutter + rare damped sag events |
| **M3 (Unstable Transport)** | Expressive (`sc=1`) | 13.55 | 0.884 | Expressive transient speed sags with smooth recovery |

## 3. Key Observations
1. **Responsiveness to Depth:** In M1/M2/M3, velocity-error integration scales pitch deviation monotonically and predictably with `wowDepth` and `flutterDepth`, independent of delay time.
2. **Spatial Coherence:** L/R modulation correlation stays high (`0.77–0.96`), preserving center focus while adding organic stereo movement.
3. **Zero Discontinuities:** No NaN/Inf or sample-step glitches were observed across 44.1 kHz, 48 kHz, high feedback (`95%`), Multi-Head (`activeHeads=7`), Ping-Pong, Reverse, Warp, or parameter automation sweeps.
