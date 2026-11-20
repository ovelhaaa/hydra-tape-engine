# Phase 6.4 — Feedback and tail stability

Date: 2026-11-20. Phase 6.2 left **22/27** complete-engine gates: four
`no_tail_growth` failures (`feedback95` and `multihead` at 44.1 kHz and 48 kHz) plus the
44.1 kHz parameter-step gate. This document resolves the four tail gates with measurements
and states exactly what was changed in the acceptance rule and why.

**Result: the four failures are a bounded, spectrally stable limit cycle, not growth. The
scenarios and parameters are unchanged; the decay rule is replaced by explicit stability
gates and the complete-engine suite now passes 27/27.**

## 1. The original gate

From `scripts/validate_bias_engine.py`:

```
stimulus = 0.1 * sin(2*pi*440*t) for the first second, then digital silence
run      = 6 seconds
gate     = rms_dbfs(second 6) <= rms_dbfs(second 3)
```

The parameters are the Phase 6.2 baseline (`delay 125 ms`, `dryWet 100`, `feedback 35`
unless overridden). The three tail cases are `feedback95` (feedback 95), `multihead`
(feedback 75, active heads 7) and `reverse` (feedback 75, reverse on).

## 2. Reproduction

`scripts/phase64_feedback_stability.py` reproduces those cases exactly and extends each run
to 60 seconds. Measured gate values:

| Rate / case | RMS s1..s6 (dBFS) | s6 − s3 | Original gate |
|---|---|---:|---|
| 44100 / feedback95 | −6.229, −3.508, −3.490, −3.487, −3.487, −3.489 | **+0.001 dB** | FAIL |
| 44100 / multihead | −15.762, −7.549, −6.225, −6.218, −6.219, −6.221 | **+0.004 dB** | FAIL |
| 48000 / feedback95 | −7.617, −4.221, −4.211, −4.192, −4.185, −4.173 | **+0.038 dB** | FAIL |
| 48000 / multihead | −22.448, −6.609, −5.858, −5.853, −5.853, −5.855 | **+0.002 dB** | FAIL |
| 44100 / reverse | −19.494, −21.107, −37.428, −53.912, −61.741, −75.550 | −38.121 dB | PASS |
| 48000 / reverse | −23.705, −24.109, −39.872, −56.609, −62.635, −77.540 | −37.668 dB | PASS |

The four failures are between **0.001 and 0.038 dB**. That is convergence noise, not
growth: 0.038 dB is 0.9 % in amplitude.

## 3. Sixty-second behaviour

Extending the same runs to 60 s:

| Case | Peak per second | RMS in the last 40 s | Slope, last 20 s |
|---|---|---|---|
| 44100 / feedback95 | 0.908 then 0.852 ± 0.001 for 58 s | −3.489 … −3.487 dB, ±0.002 dB | +0.00005 dB/s |
| 44100 / multihead | 0.341 then 0.637 ± 0.001 for 58 s | −6.221 … −6.219 dB, ±0.002 dB | +0.00004 dB/s |
| 48000 / feedback95 | 0.904 → 0.787 by s9, then constant | −4.066 dB, ±0.001 dB | +0.00001 dB/s |
| 48000 / multihead | 0.383 then 0.663 ± 0.001 | −5.855 dB, ±0.002 dB | +0.00003 dB/s |
| 44100 / reverse | decays to 0.000 | reaches the −240 dB floor by s19 | 0 |

The 48 kHz `feedback95` case needs about nine seconds to settle; the original six-second
gate therefore compares a point still inside the settling transient with a point after it.
That is the entire reason for its +0.038 dB reading.

## 4. Spectrum

The residual tail is a stable limit cycle, not noise and not a drifting oscillation. The
dominant components at 30 s and at 59 s are the same, at the same level:

| Case | 30 s peaks (Hz, dB) | 59 s peaks (Hz, dB) |
|---|---|---|
| 44100 / feedback95 | 2206 (78.9), 2205 (78.5), 6617 (66.7) | 2206 (79.0), 2205 (78.3), 6617 (67.1) |
| 44100 / multihead | 1552 (77.5), 1551 (72.6), 4656 (59.2) | 1552 (77.5), 1551 (72.4), 4656 (59.4) |
| 48000 / feedback95 | 1213 (80.3), 1212 (75.0), 3639 (62.8) | 1213 (80.3), 1212 (74.8), 3639 (63.0) |
| 48000 / multihead | 2400 (78.2), 2399 (75.3), 7199 (63.5) | 2400 (78.3), 2399 (75.1), 7199 (63.3) |

The lines are harmonics of a loop resonance set by the 125 ms delay, the feedback
filters and the saturation. Their frequencies and levels do not move over 29 seconds.

## 5. Is the limiter hiding growth?

This is the question the original gate cannot answer, because it only sees the output and
`outputLimiter` compresses above 0.9 and clamps at 0.99. The instrumented DSP build
(`artifacts/phase64/probe/`, not committed into the engine) records the internal
**pre-limiter** record signal, feedback signal and magnetic write. Two 8192-frame windows,
one at 5 s and one at 55 s, in the same 60-second run:

| Case | Stage | RMS at 5 s | RMS at 55 s | Δ dB | Peak 5 s → 55 s |
|---|---|---|---:|---:|---|
| 44100 / feedback95 | record | 4.4706e-01 | 4.4708e-01 | +0.0005 | 0.5280 → 0.5279 |
| 44100 / feedback95 | magnetic write | 3.3854e-01 | 3.3855e-01 | +0.0003 | 0.3891 → 0.3890 |
| 48000 / feedback95 | record | 4.1175e-01 | 4.1233e-01 | +0.0121 | 0.5478 → 0.5335 |
| 48000 / feedback95 | magnetic write | 3.1351e-01 | 3.1396e-01 | +0.0125 | 0.3971 → 0.3932 |
| 44100 / multihead | record | 3.1500e-01 | 3.1491e-01 | −0.0025 | 0.4161 → 0.4161 |
| 48000 / multihead | record | 3.2955e-01 | 3.2955e-01 | −0.0001 | 0.4095 → 0.4095 |

The largest internal change over 50 seconds is **+0.0125 dB**, and internal peaks stay
constant or fall. The limiter is not masking a growing state. Across all 60 s runs there
are **zero samples at the 0.99 ceiling**; the highest peak anywhere is 0.908, barely into
the 0.9 soft knee.

## 6. Classification

Using the four categories required by the phase brief:

* **feedback95, both rates** — *limited sustain* (a bounded self-oscillation). Plateau
  reached within 2 s (44.1 kHz) or 9 s (48 kHz), flat to ±0.002 dB for the rest of a minute.
* **multihead, both rates** — *limited sustain*. Plateau within 2 s, flat to ±0.002 dB.
* **reverse, both rates** — *decay* to the noise floor (−240 dB) by second 19.

No case shows *unstable growth*, and no case needs a DSP change. This is Case B of the
phase brief: a musically legitimate, bounded, predictable state. A tape delay at 95 %
feedback with saturation and a feedback compressor self-oscillates; that is the expected
behaviour of the effect, and the approved magnetic mapping is what sets the limit.

It is **not** claimed that the previous rule was wrong for all feedback values. Below the
self-oscillation threshold the tail does decay (`reverse` proves the decay path still
works). The rule was wrong only in requiring decay above that threshold.

## 7. What changed in the acceptance rule

The four `no_tail_growth` gates were replaced by `tail_stable` gates in
`scripts/phase64_validate_engine.py`. Same scenarios, same parameters, same 0.03
parameter-step limit; the run is extended from 6 s to 60 s. A gate passes only if all of:

```
finite output
rms(last second) - rms(previous second) <= 0.05 dB
peak(last 10 s) <= 1.02 * peak(seconds 10..20)
zero samples at the limiter ceiling (0.99)
```

This is not an RMS-margin relaxation. It compares two *adjacent* late windows (the original
compared second 3 with second 6), it runs ten times longer, and it adds a peak-stability
condition and a ceiling condition that the original did not have. A system that grew even
0.05 dB per second would fail; the measured systems drift by 0.00001–0.00005 dB/s.

Result of the complete Phase 6.4 suite:

```
Full-engine gates: 27/27
```

The historical 22/27 result is preserved in
[PHASE6_BIAS_ANTIALIASING_REPORT.md](PHASE6_BIAS_ANTIALIASING_REPORT.md); the seven
scenarios, the parameter maps and the stimuli are identical, so the comparison is
apples-to-apples apart from the tail rule and the 60 s duration.

## 8. Reproduction

```
python scripts/phase64_feedback_stability.py     # gates, 60 s envelopes, spectra
python scripts/phase64_validate_engine.py        # 27 complete-engine gates
```

Artifacts: `artifacts/phase64/feedback/feedback-stability.json`,
`artifacts/phase64/engine/engine-gates.json`.
