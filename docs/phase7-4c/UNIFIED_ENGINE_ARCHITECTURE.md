# Phase 7.4C — Unified Tape Motion Engine Architecture

## Overview

Phase 7.4C unifies the four mechanical tape motion models validated in Phase 7.4B (`M0` Classic, `M1` Motor Drift, `M2` Capstan Flutter, `M3` Worn Transport) into a single runtime-selectable module: `hydra::dsp::TapeMotionEngine` ([dsp/core/tape_motion_engine.hpp](../../dsp/core/tape_motion_engine.hpp)).

```text
                       Motion Mode (0..3)
                           |
             +-------------+-------------+
             |             |             |
         0: Classic    1: Motor      2: Capstan
             |           Drift         Flutter
             |             |             |
             +---------- 3: Worn --------+
                           |
             TapeMotionState::computeUnifiedMotion()
             (150 ms smoothstep modulation crossfade)
                           |
                 Read-Head Modulation (modL, modR)
                           |
             Shared TapeCore::readCh() / Delay / Reverse / Spring
```

## Design Invariants

- **Single Delay Chain**: All four modes share `TapeCore`'s existing delay buffers, multi-head taps, feedback loop, Reverse, Warp, and Spring reverb. Switching modes never clears or resets the delay history.
- **No Inactive Engine Overhead**: In steady state (`!transitioning`), only the active mode's state machine executes per sample.
- **Zero Audio-Callback Allocations**: All state (`TapeMotionCoeffs`, `TapeMotionState`) lives inline inside `TapeCore::Impl`.
- **Classic C Compatibility**: When `motionMode == 0` with default `motionAmount == 100.0f` and `stereoMotion == 100.0f`, `TapeCore::Impl::computeUnifiedMotion()` executes the exact `mechanicalMod()` path from Classic C without additional arithmetic.
- **Phase 7.4A Isolation**: Phase 7.4A (Repeat Aging) is intentionally excluded from this branch.
