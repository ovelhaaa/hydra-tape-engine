# Phase 7.4C — Clickless Runtime Switching

## Mechanism

Mode transitions occur exclusively in the **read-head modulation displacement layer** (`modL`, `modR` in samples) inside `TapeCore::Impl::computeUnifiedMotion()` ([dsp/core/tape_core.cpp](../../dsp/core/tape_core.cpp)).

1. **Block-Edge Mode Commit**: `motionState.setTargetMode(currentParams.motionMode)` is invoked in `TapeCore::Impl::updateFilters()` at block boundaries.
2. **Controlled 150 ms Crossfade**: When `activeMode` changes, `transitionPos` ramps from `0.0` to `1.0` over `0.150f * sampleRate` samples using a cubic smoothstep curve `s = t * t * (3.0f - 2.0f * t)`.
3. **Single-Step Guard**: When transitioning between modes that share sub-generators (e.g., `M1 <-> M3` or `M2 <-> M3`), `stepM1`, `stepM2`, and `stepM3` are each advanced at most once per sample.
4. **Rapid Consecutive Switches**: If a new mode is selected while `transitioning` is already `true`, `pendingSnapHold` captures `holdOffset = lastOut - modPrev` on the first sample of the new transition so the output displacement remains continuous with zero sample jump.
5. **Stereo Coherence**: Both L and R channels share the exact same `transitionPos` and `s` curve.

## Verification

- `tests/core/phase74c_unified_motion_test.cpp`: Verified `M0 -> M1 -> M2 -> M3 -> M0` and 10 ms rapid switches under `feedback = 85%`, Multi-Head (`7`), Reverse, and Spring enabled (`maxJump < 0.25`, finite output, delay tail preserved).
- `host/web/tests/webUnifiedMotion.test.mjs`: Verified live WASM AudioWorklet switching under `feedback = 85%` (`maxJump < 0.25`).
