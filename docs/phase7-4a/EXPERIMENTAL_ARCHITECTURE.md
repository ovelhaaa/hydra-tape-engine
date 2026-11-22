# Phase 7.4A — Experimental Architecture (C0, C1, C2, C3)

## 1. Compile-Time Isolation & Insertion Point

All Phase 7.4A experiments are gated by the compile-time macro `HYDRA_PHASE74_REPEAT_AGING_MODE` (default `0`):

- `0` (**C0 — Classic C Baseline**): Existing Classic C (`HYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING=1`) with zero extra state, zero extra operations, and bit-identical output.
- `1` (**C1 — Dynamic Spectral Wear**): Requires `HYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING=1`.
- `2` (**C2 — Transient Softening**): Requires `HYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING=1`.
- `3` (**C3 — Combined Aging**): Requires `HYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING=1`.

### Signal Chain Position

```text
Delay Read Heads (delaySig, reverse-blended)
       │
       ├──► Monitor Path (unaffected: textureSig + delaySig -> dropout -> azimuth -> hiss -> playback EQ -> outputLimiter)
       │
       └──► Classic C Feedback Path (!warpActive)
              │
              ▼
        colorTapeFeedback (classicDropout -> classicAzimuth -> classicRolloff -> classicReproBump -> classicGapLoss + 0.20*hiss)
              │
              ▼
        [Optional Phase 7.4A Aging: C1 / C2 / C3]   <-- Exclusive to feedback path, separate L/R state
              │
              ▼
        Ping-Pong Swap -> feedbackFrom (300Hz HPF -> drive -> bump -> gapLPF -> phaseAP -> 2-stage sat -> soft-knee comp)
              │
              ▼
        writeCh (DCBlocker -> TapeMagnetics -> Delay Buffer)
```

- **Direct input & monitor output** are completely untouched.
- **Repeat #1** (read from delay buffer before recirculation) retains full original definition.
- **Repeats #2+** accumulate the aging behavior through continuous recirculation, remaining causally consistent under Multi-Head, Ping-Pong, and Reverse without per-sample age metadata.

---

## 2. C1 — Dynamic Spectral Wear

**Hypotheses tested:** H1 (signal-dependent spectral evolution), H3 (dynamics × aging interaction).

Per-channel (`L`/`R`) state:
- `prevX`: 1-sample memory for lightweight HF proxy `|x - prevX|`.
- `envSlow`: slow RMS/abs envelope follower (attack 15 ms, release 180 ms).
- `hfEnv`: HF activity envelope follower (attack 8 ms, release 120 ms).
- `wearLPF`: `OnePoleLP` with dynamically modulated cutoff.

Operation:
1. `wearNorm = clamp(1.4 * envSlow + 1.1 * hfEnv, 0, 1)`.
2. `ageHeadroom = 0.35 + 0.65 * (1.0 - 0.6 * tapeAgeNorm)` (prevents excessive darkening when static `classicGapLoss` is already low at high `tapeAge`).
3. `fc = maxCut - (maxCut - minCut) * (wearNorm * 0.78 * ageHeadroom)`, where `maxCut = clamp(11000 + 3000 * tapeSpeedNorm, 4000, 0.45 * fs)` and `minCut = 2400 + 600 * tapeSpeedNorm`.
4. `wearLPF.setCutoffFastInv(maxFc, invSampleRate, fc)` (rational approximation `x / (1 + x)` using `fastRecip`, zero `exp`/`sin`/`cos` per sample).
5. `out = wearLPF.process(x)`.

---

## 3. C2 — Transient Softening

**Hypotheses tested:** H2 (progressive transient softening), H3 (dynamics × aging interaction).

Per-channel (`L`/`R`) state:
- `fastEnv`: fast envelope follower (attack 1 ms, release 12 ms).
- `slowEnv`: slow envelope follower (attack 22 ms, release 110 ms).
- `transSmooth`: smoothed normalized transient excess (attack 1.5 ms, release 35 ms).
- `softLPF`: `OnePoleLP` for attack-edge HF rounding.
- `smearAP`: `AllpassFilter` (`coeff = 0.38f`) for phase/crest dispersion during attacks.

Operation:
1. `transient = max(0, fastEnv - 1.15 * slowEnv)`.
2. `transNorm = clamp(transient * fastRecip(slowEnv + 0.03), 0, 1)` (0.03 floor prevents tape hiss from triggering transient softening).
3. `transSmooth` tracks `transNorm`.
4. `fc = 14000 - (14000 - (1400 + 600 * tapeSpeedNorm)) * transSmooth`.
5. `y = softLPF.process(x)`.
6. `ap = smearAP.process(y)`.
7. `out = y + (0.30 * transSmooth) * (ap - y)`.

Sustained notes (`transSmooth ≈ 0`) pass through near-transparently (`fc ≈ 14 kHz`, 0% smear); sharp attacks (`transSmooth -> 1`) receive both upper-harmonic rounding and allpass crest dispersion.

---

## 4. C3 — Combined Aging

Evaluated only after C1 and C2 are independently validated. Chains C2 (transient softening) into C1 (dynamic spectral wear) with moderated depths to avoid over-darkening.
