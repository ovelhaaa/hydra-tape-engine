# Hydra Tape Engine — Phase 7.2A feedback audit and controlled experiment

**Reference:** `main` merge SHA `24977b67e1ab8441080a29d9e5d40114c966ac50` (approved Phase 7.1).
**Decision:** An isolated alternative exists **only** under a compile-time flag, no production activation and no automatic merge.

## 1. Current signal graph (approved sound)

Within `TapeCore::processStereoBlock`, each channel has:

```text
conditioned live input ------------------------------------------+
                                                                     |
PSRAM tape reads -> head sum -> forward/reverse crossfade            |
    |                                                                |
    +-> texture read 200 samples -> monitor texture gain             |
    |                                                                |
    +-> delay heads -> monitor delay gain                             |
            |                                                        |
    sum -> dropout loss -> azimuth -> add hiss                        |
            |                                                        |
            +-> playback shelf/bump/gap loss -> monitoring -> limiter |
            |                                                        |
            +-> second forward/reverse blend -> feedback HPF         |
                -> head bump -> feedback LPF -> allpass -> two         |
                rational saturators -> gain -> soft-knee compressor   |
                -> record amp/DC block -> magnetics -> PSRAM tape <--+
```

The feedback source currently uses the exact expression:

```cpp
frame.feedbackSource = tapeSig + reverseBlend * (reverseSig - tapeSig);
```

Here `tapeSig` already contains `textureSig * textureGain + delaySig * delayGain`, dropout/azimuth processing and newly injected hiss; **`delaySig` already contains a forward/reverse blend** when Reverse V2 is active.

### Structural consequences, NOT yet a claim about perceived quality

1. Changing `dryWet` / `delayWet` (monitor gains) changes feedback source magnitude or character.
2. Hiss and monitoring dropout/azimuth are allowed to recirculate into subsequent tape writes.
3. Reverse's first blending stage is embedded in `delaySig`, but the feedback source blends a second time as Reverse fades in/out.
4. The separate Warp route uses `warpFb` and is outside this experiment.
5. Physical tape recirculation still passes through the existing feedback filters, dual rational saturation, compressor and magnetic write; the behavior is **not** a linear loop.

None of these properties alone establishes that the existing sound is bad; Phase 7.1 was approved.

## 2. Experiment B: raw read-head source

Only when `HYDRA_PHASE72_EXPERIMENTAL_FEEDBACK_ROUTING=1` is explicitly defined
during a separate test build, the conventional/Reverse feedback source becomes:

```cpp
frame.feedbackSource = experimental::independentTapeFeedbackSource(delaySig);
```

Implications:

- The loop input is independent of texture-monitor and delay-monitor levels.
- This source does **not** include the hiss injected at the playback monitor or the monitor's dropout and azimuth processing.
- The Reverse blend happens once, upstream, via the existing `delaySig` computation.
- The feedback HPF, feedback head bump, feedback gap loss, feedback phase allpass, nonlinear stages, record gain, magnetics, output limiter and user feedback parameter are unchanged.
- Warp remains routed from `warpFb`.
- This is a deliberate and potentially **large** sonic change; it may lower or increase recirculating level and change degradation on high feedback. Do not describe it as inherently superior.

## 3. Production guarantees and possible caveats

- Normal DSP builds do not define the macro and compile the **same old expression**.
- No C API changes, new parameter IDs, preset changes, runtime branches, new per-sample allocations or new DSP effects.
- The A/B generator compiles approved main A, candidate with experiment OFF A0 and candidate with experiment ON B using the same compiler, `-ffp-contract=off`, inputs and deterministic seeds.
- A vs A0 must be **float bit-exact for every scenario**; otherwise packaging fails.
- A vs B is intentionally not bit-exact and requires listening. Warp A vs B must remain bit-exact.
- A/B packaging aborts for NaN, Inf or limiter ceiling samples, rather than silently normalizing or clipping unsafe WAVs.
- Default Native/WASM equivalence, existing goldens, feedback stability and Reverse real-time gate must remain green.
- The experimental mode is not a physical firmware release candidate. CPU/stack and physical audio must be measured **if** the mode is ever selected for production.

## 4. Evidence to collect

Ten deterministically paired scenes; the last is a 60-second feedback tail.
The JSON records float max_abs/RMSE, output peaks and one-second RMS, spectral
centroid and energy above 3 kHz. These are **descriptive output statistics**,
not measurements of internal read-head signal or formal perception scores.

For deeper root-cause work, a later PR can add safe stage probes (not
per-sample JSON/logging in the real-time callback) for recording amplifier,
feedback source, feedback after nonlinear stages and tape write.

## 5. Musical decision

Prioritise scenarios 03 (monitor delay), 04 (texture monitor), 05 (hiss),
06 (high feedback), 08 (Reverse entry/exit), 09 (Warp unchanged) and
10 (long feedback).

A candidate should not be approved on mathematical decoupling alone. Listen
for rhythmic articulation, evolving saturation, repeat warmth, noise character,
self-oscillation and whether repeat musical decay remains engaging.

**No automatic merge.** If A remains musically preferable, reject experiment B
or develop a more conservative hybrid mixing policy; document that decision
and retain Phase 7.1 as the release sound.
