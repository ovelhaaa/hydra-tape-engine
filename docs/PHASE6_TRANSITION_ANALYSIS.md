# Phase 6.4 — Parameter transition analysis at 44.1 kHz

Date: 2026-11-20. Phase 6.2 recorded a parameter-event step of **0.05122 at 44.1 kHz**
against a limit of **0.03**, while 48 kHz passed at 0.02540. This document identifies the
exact event, the mechanism, and the fix.

**Result: 44.1 kHz 0.05122 → 0.01441, 48 kHz 0.02540 → 0.01286. Both rates pass
`step <= 0.03`. Limit unchanged; scenarios unchanged; three goldens unchanged.**

## 1. The existing measurement

From `scripts/validate_bias_engine.py`:

```
stimulus  = continuous 110 Hz sine at 0.02, 6 s
parameters = Phase 6.2 baseline (delay 125 ms, feedback 35, dryWet 100)
events    = t=2 s drive 25 -> 100
            t=3 s tape speed 50 -> 0
            t=4 s tape age 50 -> 100
gate      = max |diff(y)| over y[center-256 : center+512] for each event second
limit     = 0.03
```

The stimulus slope at 110 Hz and 0.02 amplitude is 3.1e-4 per sample at 44.1 kHz, so any
reading near 0.05 is a real discontinuity, not a waveform slope.

## 2. Attribution

`scripts/phase64_transition_analysis.py` runs the gate and each event in isolation:

| Rate | Event | Isolated step | Isolated at boundary |
|---|---|---:|---:|
| 44100 | drive 25→100 | 0.00834 | 0.00000 |
| 44100 | tape speed 50→0 | 0.00991 | 0.00111 |
| 44100 | tape age 50→100 | 0.00095 | 0.00091 |
| 48000 | drive 25→100 | 0.01286 | 0.00000 |
| 48000 | tape speed 50→0 | 0.00509 | 0.00369 |
| 48000 | tape age 50→100 | 0.00447 | 0.00343 |

No single event reaches 0.03. With all three applied, the 44.1 kHz maximum is 0.05122,
i.e. an interaction: by t=3 s the drive is already 100, so the signal entering the tape
stage is much larger when the tape-speed event lands.

## 3. Locating the discontinuity

The largest step at 44.1 kHz is at **frame 132224**, which is the start of the 128-frame
block containing the 3.000 s event (3.000 × 44100 = 132300). Event application is
block-aligned, as documented. Sample context, left channel:

```
... +0.2230  +0.2210  +0.2190  +0.2169  +0.2148 | +0.1636 | +0.2114  +0.2084  +0.2037 ...
                                                  ^ frame 132224
```

The waveform is smooth before and after; a **single sample** drops by 0.051 and returns.
This is not a level step, it is a one-sample discontinuity — a click.

At 48 kHz the same event is at frame 144000 (3.000 × 48000, already block-aligned) and
shows the same single-sample dip, `-0.0898 | -0.0748 | -0.1002`.

## 4. Which stage

The instrumented DSP build was run with the same stimulus and events, capturing internal
stages for a window around the event:

| Stage | Max curvature | Frame |
|---|---:|---:|
| input | 0.0 | — |
| conditioned input | 0.0 | — |
| pre-filter playback (`tapeSig`) | 7e-05 | 132423 |
| feedback source | 7e-05 | 132423 |
| feedback | 1e-05 | 132425 |
| record signal | 2e-05 | 132425 |
| magnetic write | 6e-05 | 132223 |
| **playback output (`tape_out`)** | **0.04952** | **132224** |
| final output | 0.04952 | 132224 |

The signal is smooth up to the pre-filter playback node and discontinuous after the
playback filter chain. The magnetic stage, the delay read, the feedback path and the
record path are all clean.

## 5. Controlled confirmation

The playback chain is `reproHeadBump` (peak, `bumpFreq = 60 + speed*140`),
`tapeRolloff` (high shelf at `2*cut`) and `gapLossLPF` (lowpass at `cut`), where
`cut = max(400, (6000 + speed*10000) * ageFactor)` and `ageFactor` depends on tape age and
tone. All three are re-derived in `updateFilters()` when a parameter is committed.

A diagnostic build was made from the same source with **one** change: the tape-speed
modifier feeding those coefficients frozen at its previous value. Everything else,
including the magnetic stage and the event handling, was untouched.

| Build | Max curvature at frame 132224 |
|---|---:|
| production | 0.04952 |
| tape-speed coefficient dependence frozen | **0.00004** |

The discontinuity disappears. The mechanism is confirmed: **the coefficients are replaced
in one step while the biquad state is retained**. The retained state is inconsistent with
the new coefficients for one sample, which is exactly the observed single-sample impulse.
Tape age and tone act through the same `cut`, and head bump through `bumpFreq`.

## 6. Correction

`TapeCore::Impl` now keeps the four values that drive the playback filters
(tape speed, tape age, tone, head-bump gain) in target/smoothed pairs, and
`updatePlaybackFilters()` is separated from `updateFilters()`.

* The **first** configuration, before any audio has been produced, snaps to the target, so
  offline renders, goldens and the approved A/B baselines are unchanged.
* Any **later** change ramps toward the target per block with a 20 ms time constant, and
  only when a target actually differs. Steady state therefore costs nothing.
* Only the three playback biquads (and their right-channel twins) are re-derived during a
  ramp. The magnetic mapping, the delay, the feedback path, Reverse V2, the GTC, tap tempo
  and MIDI clock are untouched.

Properties required by the phase brief:

* global BPM unchanged — no transport code touched;
* MIDI clock unchanged — no MIDI code touched;
* transport is not reinitialised — no reset, no epoch change;
* delay tails are not cleared — no buffer is touched;
* preset semantics unchanged — the same parameter values reach the same target
  coefficients;
* no perceptible control latency — the filter morph is 20 ms and settles in ~100 ms;
* ESP32 cost — during a ramp, three biquad coefficient sets per channel are recomputed
  once per block; steady state adds one comparison per block. The physical firmware build
  is unchanged in size class (flash 627 743 B, 3.7 %).

A 5 ms drive-gain ramp was prototyped and **reverted**: it did not improve the gate
(44.1 kHz stayed at 0.01441) and it broke `core_regression`'s silence guarantee, because
the still-ramping drive amplified the input-filter tail of a just-disabled delay for a few
milliseconds. The proven minimal fix is the playback-coefficient ramp alone.

## 7. Result

| Rate | Before | After | Limit | Result |
|---|---:|---:|---:|---|
| 44100 | 0.05122 | **0.01441** | 0.03 | PASS |
| 48000 | 0.02540 | **0.01286** | 0.03 | PASS |

After the fix, isolated events at 44.1 kHz measure 0.00834 (drive), 0.00280 (tape speed),
0.00252 (tape age); at 48 kHz 0.01286 (drive), 0.00160 (tape speed), 0.00171 (tape age).
The remaining 48 kHz maximum is the drive event, whose record-amp gain
(`drive * 0.05`, i.e. 1.25 → 5.0) still changes in one step. It is below the gate and is
recorded here as a known, bounded residual; it is not required for the gate and is not
changed in this phase.

The goldens were **not** regenerated. `test_golden_param_automation` automates only
dry/wet, feedback and delay time, none of which feed the playback filters, so its output is
bit-identical and all three golden tests pass unchanged.

## 8. Audio impact

The correction changes the audio only while tape speed, tape age, tone or head bump are
automated during playback. Rendering the same material with the approved build (`7431a6d`)
and the candidate:

| Second | Max abs | RMS |
|---|---:|---:|
| 0 | 3.99e-05 | 6.19e-06 |
| 1 (event) | 1.88e-02 | 1.29e-03 |
| 2 (event) | 4.49e-02 | 2.83e-03 |
| 3 | 5.35e-05 | 1.12e-05 |
| 4 | 4.05e-05 | 8.77e-06 |
| 5 | 1.78e-05 | 4.05e-06 |

The difference is confined to the two automation seconds and returns to the LUT-only level
(~1e-5) afterwards. The old build clicks at the event; the new build morphs the playback
bandwidth over 20 ms. That is an audible, intended change, so it is part of the Phase 6.4
A/B package and requires human listening.

## 9. Reproduction

```
python scripts/phase64_transition_analysis.py
```

Artifacts: `artifacts/phase64/transitions/transitions.json`. The instrumented builds and
controlled experiment live under `artifacts/phase64/probe/` and are not part of the engine.

## Phase 6.4.1 five-parameter CI gate (2026-11-20)

The earlier reproduction covered drive, tape speed and tape age. The same script
now also automates tone (100 to 0 at 5 s) and head bump (0 to 100 at 6 s), with
an 8-second signal, both sequentially and in isolation at 44.1/48 kHz. It writes
the report before failing on a nonfinite step or any step above **0.03**. Parameter
IDs, values and production DSP are unchanged.

Local Windows replay yielded maxima **0.01797** at 44.1 kHz and **0.01420** at
48 kHz, both at the sequential tone event. Isolated tone steps were
0.00371/0.00308 and isolated head-bump steps 0.00098/0.00088. The previous
three-event 0.01441/0.01286 measurements remain historical results of their
original event schedule, not the maxima of this expanded schedule.

`Phase 6.3 Reverse Real-Time` runs this check once alongside the full-engine and
60-second feedback diagnostics; reports are published under
`ci-reports/reverse/transitions/`. Final Linux status belongs to the Release
Decision Record, not to the local numbers above.
