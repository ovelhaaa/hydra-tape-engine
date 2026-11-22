# Phase 7.2C — Classic Tape Feedback (experimental)

## Reference and decision status

Frozen **approved A**: main Phase 7.1 merge
`24977b67e1ab8441080a29d9e5d40114c966ac50`.

Variant **B**: independent *uncolored* read-head source.
Variant **C**: independent *degraded* playback-head source.
No change to the approved main DSP build. Both alternate variants
are compile-time-only binaries for listening, **not release candidates**.

## 1. Physical architecture hypothesis

For ordinary echo and Reverse, the original signal `delaySig` is already a
weighted reading of the virtual tape heads, with the Reverse crossfade
performed once. It is not affected by monitor output controls. In variant C,
this head reading passes through dedicated, private playback processing:

```text
  PSRAM tape history
      |
  read/weight playback heads + single Reverse blend
      |
  C playback path [separate filter states from monitor path]
      +-- dropout amplitude & HF loss (same dropout frame)
      +-- azimuth allpass (same physical coefficient, independent state)
      +-- -12 dB high-shelf rolloff (same speed/age/tone curve)
      +-- playback head bump (same amount/frequency)
      +-- gap loss LPF (same curve)
      +-- small freshly generated tape hiss fraction (0.20)
      |
  EXISTING feedback HPF -> head bump -> lowpass -> phase allpass
      -> two rational saturation stages -> feedback gain -> soft-knee
      |
  EXISTING record amplifier -> DC block -> magnetic saturation
      |
  PSRAM tape write
```

The monitor chain keeps its exact original filters, gain, hiss, dropout,
output limiter and mix; the C chain maintains **independent IIR states**.
Sharing the same monitor playback biquad would advance its filter state twice
per sample and contaminate the approved output.

The playback EQ coefficients update from the same smoothed 20ms tape
parameters at the same block boundary. Azimuth coefficients follow the
existing per-sample modulation. A full reset and delay reactivation reset
the C-only IIR states. The denormal flushing path includes the new states.

### Why this is closer to classic tape echo topology

- The echo-volume (monitor output) control does not set the feedback source.
- The repeated recording receives a *reproduced* analog-like tape signal,
  not an ideal DSP buffer read.
- The existing magnetic nonlinearity acts again on every recorded repetition.
- Dropout loss, speed/age/tone EQ and a controlled hiss contribution can
  accumulate across passes, without depending on dry/wet gains.
- Forward/Reverse selection does not double-crossfade inside feedback.
- The Warp feedback path remains **unchanged**, on purpose.

### Deliberate limitations

This is a musically motivated modeling topology, **not** a calibrated
Roland RE-201 or Echoplex electrical schematic. In particular:
- A 20% re-recorded hiss coefficient is a fixed experimental hypothesis,
  not a measurement of noise in either vintage unit.
- The playback chain uses existing Hydra filters and their design laws,
  not measurements from original tape heads.
- Feeding a separate C filter path costs an additional **six biquads,
  two one-poles and two allpasses** for stereo, plus filter coefficient
  work and associated state. Benchmark the ESP32-S3 actual audio callback
  deadline before attempting deployment with C enabled.
- Feedback remains compressed/saturated by Hydra's existing digital
  circuit; head gain normalization, reverse control and magnetics are
  intentionally untouched.
- Noise and dropout sound are subject to listening judgment; no automatic
  numerical metric can infer musical preference.

## 2. Isolation and regression plan

Production builds have neither `HYDRA_PHASE72_EXPERIMENTAL_FEEDBACK_ROUTING`
nor `HYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING` defined. Variant C requires
explicit `-DHYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING=1`.
Both macros enabled simultaneously are a **compile error**.

The CI builds a full C motor smoke binary, runs the B/C scalar transfer
contract, and renders both A/B and A/C sets at the same input/sample rate,
seed, parameter settings, and timing. The new source file contains
no host-facing parameter or ABI additions.

For every scenario:
- A == candidate with experimental flags OFF must be **float bit-exact**;
- A vs B and A vs C can differ, and must be evaluated with actual audio;
- For Warp, A == B == C must remain **bit-exact**;
- The approved A WAV hash must match exactly across A/B and A/C ZIPs;
- Nonfinite samples and the output limiter ceiling **fail packaging**;
  no normalization or automatic peak limiting is allowed.

The Listening Lab currently supports pairwise audio only. Do not claim a
three-track synchronized player; use the matching A/B and A/C packages and
the guide in `PHASE7_2_CLASSIC_LISTENING_GUIDE.md`.

## 3. Release gates

1. Native core, goldens, strict Native/WASM, Reverse, Bias, Web Demo and
   experimental C smoke and paired listening CI are green at the exact HEAD.
2. Human review compares A/B/C for all ten scenes, especially the
   60-second tail and noise after input silence.
3. If C is preferred, prepare separate physical runtime and
   real-time budget verification on ESP32-S3, and re-evaluate hiss blend,
   feedback topology and memory use.
4. Only with explicit musical and physical approvals should a later
   PR promote a selected routing variant to the production engine.

**No automatic merge.**
