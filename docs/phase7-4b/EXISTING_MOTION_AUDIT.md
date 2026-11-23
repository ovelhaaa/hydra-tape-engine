# Phase 7.4B — Existing Motion Audit (`dsp/core/tape_core.cpp`)

## 1. Scope
Audit of the mechanical tape motion subsystem in `dsp/core/tape_core.cpp` (PR #58 Classic C baseline, commit `d2d36dd`).

## 2. Inspected Components
- `MechanicalState` (`dsp/core/tape_core.cpp:282-286`)
- `mechanicalMod()` (`dsp/core/tape_core.cpp:287-304`)
- `wowDrift`, `wowState`, `flutterJitter`, `flutterHpState`
- `mechNoiseL` / `mechNoiseR` (`include/hydra_mech_noise.hpp`)
- `wowRate`, `wowDepth`, `flutterRate`, `flutterDepth` (`TapeCore::Impl::updateFilters()`)
- `smoothedDelaySamples`, `readTapeAt()`, `readHistory()`, `hermite4()`
- Multi-Head scaling (`activeHeadMask`, `mod * 0.33f / 0.66f / 1.0f`)
- Stereo L/R modulation (`mechStateL`, `mechStateR`)
- Reverse (`readReverseWindow`, `readTapeReverse`) and Warp (`warpActive`)

## 3. What Is Currently Modeled
1. **Wow (Direct Position Random Walk):**
   - `wowDrift = wowLeakCoeff * wowDrift + wowStepCoeff * randSignedLCG(wowSeed)` clamped to `[-0.9, 0.9]`.
   - 1st-order lowpass `wowState += wowLpAlpha * (wowDrift - wowState)` followed by soft cubic saturation `wowState - 0.16 * wowState^3`.
   - Scaled directly into sample displacement by `wowDepthScale * modBaseSamples` (`0.00025 * wowDepth * modBaseSamples`).
2. **Flutter (Band-Limited White Noise):**
   - White LCG noise passed through 1st-order HP (`flutterHpAlpha`), 1st-order LP (`flutterLpAlpha`), and a 2nd-order Biquad LPF (`flutterLPF`, cutoff `clamp(flutterRate * 1.6, 4, 12)` Hz).
   - Scaled directly into sample displacement by `flutterDepthScale * modBaseSamples`.
3. **Scrape Noise (`MechNoise`):**
   - Dual 1-pole LP white noise (`mechNoiseL`, `mechNoiseR`), scaled by `scrapeDepthScale * modBaseSamples`.
4. **Periodic Residual:**
   - Fixed-amplitude sum `0.03 * fast_sin(wowPhase) + 0.01 * fast_sin(flutterPhase + 0.31)` added in samples regardless of depth.
5. **Interpolation & Safety:**
   - 4-point Hermite (`hermite4`) with `TAPE_BUFFER_GUARD = 4` and clamp `[-0.25 * modBaseSamples, +0.25 * modBaseSamples]`.
   - Reverse window clamps `mod` to `±reverseWindows.modulationLimit()`. Warp does not use `modL`/`modR`.

## 4. Phenomena Absent or Structurally Limited
1. **Direct Position Modulation vs. Velocity Integration:**
   - Scaling displacement directly by `smoothedDelaySamples` causes longer delays to exhibit proportionally larger pitch excursions for the same modulation rate, whereas physical tape speed error $\Delta v / v_0$ produces pitch deviation proportional to speed error and displacement as its bounded integral.
2. **No Motor Inertia / Restoring Torque Dynamics:**
   - `wowDrift` is a 1st-order leaky random walk into a 1st-order LP; there is no 2nd-order acceleration/inertia coupling or restoring oscillation around nominal speed.
3. **No Quasi-Periodic Capstan / Roller Structure:**
   - `residual` is a tiny fixed-frequency sine (`0.03` samples), while `flutter` is pure filtered white noise without unstable-frequency capstan or pinch-roller harmonics.
4. **Independent L/R Random Streams:**
   - `mechStateL` and `mechStateR` use independent LCG seeds (`0x13579BDFu` vs `0x2468ACE1u`, etc.), so L/R wow/flutter are largely uncorrelated rather than sharing a common physical tape transport with small differential weave.
5. **No Occasional Non-Repetitive Transport Transients:**
   - No rare speed sags, tension hiccups, or damped recovery oscillations.
