# Phase 7.4A — DSP Audit (`TapeCore` & Classic C Signal Chain)

**Branch:** `experiment/phase7-4a-repeat-aging` (based on `feat/phase7-3-classic-c-web-default` @ `e815117`)  
**Audited Files:**
- [`dsp/core/tape_core.cpp`](file:///c:/Users/devx/Documents/PlatformIO/Projects/hydra/dsp/core/tape_core.cpp)
- [`dsp/core/tape_core.hpp`](file:///c:/Users/devx/Documents/PlatformIO/Projects/hydra/dsp/core/tape_core.hpp)
- [`dsp/core/feedback_routing_experiment.hpp`](file:///c:/Users/devx/Documents/PlatformIO/Projects/hydra/dsp/core/feedback_routing_experiment.hpp)
- [`dsp/core/tape_integrity.hpp`](file:///c:/Users/devx/Documents/PlatformIO/Projects/hydra/dsp/core/tape_integrity.hpp)

---

## 1. Real Per-Sample Signal Order (Classic C, `HYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING=1`)

For each stereo frame in `TapeCore::Impl::processStereoBlock`:

1. **Modulation & Noise Generation (per frame)**
   - `smoothedDelaySamples` approaches `targetDelay` (`delaySmoothAlpha`, 200 ms).
   - `mechanicalMod()` computes `modL`, `modR` from wow (leaky random walk + LP + cubic term), flutter (HP+LP noise + `flutterLPF`), `mechNoiseL/R` scrape, and residual sine (`wowPhase`, `flutterPhase`).
   - `azimuthPhase` (0.2 Hz triangle) updates `smoothedAzCoeff` and sets `azimuthFilter{,R}` and `classicAzimuth{L,R}` (when `azimuthErrorNorm > 0.0001f`).
   - `DropoutGenerator` updates `common` and per-channel `local[0/1]` to yield `dropoutL`, `dropoutR` (`ampGain`, `hfLoss`).
   - Input envelope follower (`inEnvL`, `inEnvR`: `abs(in)` with 2 ms attack / 80 ms release) ducks `TapeNoiseGenerator` (`hissL`, `hissR` = 3-pole pink noise → 130 Hz HPF → 9 kHz LPF → +3 dB @ 2.4 kHz shelf → `fastSatRational`), lifted slightly by dropout and scrape.

2. **Input Conditioning (`readCh`)**
   - `conditionedInput = inputLPF(inputHPF(in))`
     - Default: 20 Hz HPF / 20 kHz LPF (`Q = 0.707`).
     - `guitarFocus`: 150 Hz HPF / 5 kHz LPF (`Q = 0.707`).

3. **Delay Buffer Read (`readCh`)**
   - `textureSig`: read at `200 + mod` samples (when `textureGain > 0` and `!warpActive`).
   - `delaySig`:
     - If `warpActive`: `warpSig` (2-tap or 1-tap granular pitch/time warp).
     - Else if `legacyReverse`: `readTapeReverse()`.
     - Else if `reverseBlend < 1`: weighted sum of active heads (`mask & 1`: `0.33 * delay`, gain `1.0`; `mask & 2`: `0.66 * delay`, gain `0.75`; `mask & 4`: `1.0 * delay`, gain `0.55`), normalized by `invHeadGainSum`.
     - If `reverseBlend > 0` and `!warpActive`: crossfade toward `readReverseWindow()`.

4. **Monitor Path (`readCh`)**
   - `tapeSig = textureSig * textureGain + delaySig * delayGain`
   - Optional `dropoutLPF` (`OnePoleLP` at `dropoutBaseCutoff * (0.35 + 0.65 * drop.hfLoss)`) + `*= drop.ampGain`
   - Optional `azimuthFilter` (`AllpassFilter`)
   - `tapeSig += hiss`
   - `frame.tapeOutput = gapLossLPF(reproHeadBump(tapeRolloff(tapeSig)))`

5. **Classic C Feedback Source Conditioning (`colorTapeFeedback`, when `!warpActive`)**
   - Starts from `frame.feedbackSource = delaySig` (unscaled by `delayGain`, no `textureSig`).
   - `classicDropout{L,R}` (`OnePoleLP` + `drop.ampGain`) when dropout active.
   - `classicAzimuth{L,R}` (`AllpassFilter`) when `useAzimuth`.
   - `classicRolloff{L,R}` (HighShelf -12 dB @ `2 * cut`, `Q = 0.5`).
   - `classicReproBump{L,R}` (Peak @ `60 + 140 * pbSpeed` Hz, `Q = 0.9`, gain `0..6` dB).
   - `classicGapLoss{L,R}` (Lowpass @ `cut`, `Q = 0.707`).
   - `classicTapeFeedbackSource(playback, hiss)` adds `0.20f * hiss`.
   - **[Phase 7.4A Insertion Point]** Optional experimental aging sits immediately after `classicTapeFeedbackSource` (or right before `feedbackFrom`), *before* Ping-Pong swap and `feedbackFrom`, exclusively when `!warpActive`.

6. **Ping-Pong Routing**
   - `fbSourceL = pingPong ? frameR.feedbackSource : frameL.feedbackSource`
   - `fbSourceR = pingPong ? frameL.feedbackSource : frameR.feedbackSource`

7. **Feedback Electronics (`feedbackFrom`, when `!warpActive`)**
   - `feedbackHPF`: 2nd-order HPF @ 300 Hz, `Q = 0.5`.
   - `feedbackDrive = 1.0 + feedbackGain * 0.30 * (1 - reverseBlend) + clamp(inEnv * 0.18, 0, 0.22) + tapeAgeNorm * 0.10`.
   - `feedbackHeadBump`: Peak @ `60 + 140 * speedMod` Hz, `Q = 0.8`, gain `0..2` dB.
   - `feedbackGapLoss`: `OnePoleLP` @ `max(1600, (9000 + speedMod * 7000) * (1 - ageMod * 0.65))` Hz.
   - `feedbackPhase`: `AllpassFilter`, coeff `0.3 + ageMod * 0.4`.
   - Pre-emphasis (`feedbackPreEmph`) → two-stage `fastSatRational` (`*1.10`, `*0.82`, `*1.08`) → de-emphasis (`feedbackDeEmph`).
   - `*= feedbackGain` (capped at `0.88` for normal, `0.83 * 0.95` for reverse).
   - `feedbackCompressor`: `detail::continuousFeedbackSoftKnee` (threshold 0.6, knee [0.5, 0.7], ratio 1.5:1).
   - `*= delayEnableRamp`.

8. **Record Amplifier & Magnetic Write (`writeCh`)**
   - `freshInput = conditionedInput * driveGain` (`drive * 0.05`).
   - `recSig = clamp(dcBlocker(freshInput + feedbackInput), -4, 4)`.
   - `TapeMagnetics::process(recSig, allowBias)`:
     - `xb = x * drive` (`0.9..1.25`)
     - `h = xb + coercivity * m` (`coercivity`: `0.07..0.17`)
     - 2-point Gauss-Legendre bias-averaged `FastTanhLUT`
     - `m = remanence * m + (1 - remanence) * y` (`remanence`: `0.970..0.994`)
     - Output `satNorm * (0.82 * y + 0.18 * m)` written to `delayLine[writeHead]` (unless `freeze`).

9. **Output Mix, Limiter & Aux**
   - `outputLimiter(in * dryGain + frame.tapeOutput)` (soft knee above 0.9, clamped to `±0.99`).
   - Optional `reverseSmear` (4x `DelayAllpass`), optional `spring` reverb (3..6x `DelayAllpass` + `springLPF`), `writeHead` advance & guard mirror.

---

## 2. Redundancy & Darkness Risk Analysis

1. **Already-present static HF rolloff in Classic C feedback:**
   - Every recirculation already passes through:
     - `classicRolloff` (-12 dB high shelf @ `2 * cut`)
     - `classicGapLoss` (2nd-order LPF @ `cut`, down to 400 Hz at high `tapeAge`!)
     - `feedbackGapLoss` (1-pole LPF @ 1.6–16 kHz)
   - **Risk:** Adding another aggressive static low-pass in C1/C2 would make high `tapeAge` or high-feedback tails overly dark/muddy.
   - **Design implication:** C1's dynamic spectral wear must be **bounded, subtle, and signal-dependent** (e.g., modulating a gentle 1-pole LPF or dynamic shelf between a conservative floor like 2.2–2.8 kHz and ~12 kHz, or scaling wear down when `tapeAge` is already high, or acting primarily when feedback energy is high/sustained).

2. **Already-present LF bump in Classic C feedback:**
   - `classicReproBump` (up to +6 dB) + `feedbackHeadBump` (up to +2 dB) both sit inside the loop, partially tamed by `feedbackHPF` (300 Hz, Q=0.5).
   - **Design implication:** Neither C1 nor C2 should add any low-frequency boost.

3. **Already-present dynamic elements:**
   - `inEnvL/R` (input envelope) already modulates `feedbackDrive` (+0..0.22) and ducks `hiss`.
   - `TapeMagnetics` already provides hysteresis memory (`m`).
   - **Missing mechanisms (H1, H2, H3):**
     - **H1 (Signal-dependent spectral wear):** Current feedback filters (`classicRolloff`, `classicGapLoss`, `feedbackGapLoss`) are *static* wrt the recirculated audio signal's own energy/spectrum.
     - **H2 (Progressive transient softening):** Current feedback has no transient detector or slew/attack-dependent HF/transient softening; `feedbackCompressor` is memoryless sample-by-sample soft-knee waveshaping.
     - **H3 (Dynamics × aging interaction):** Sustained chords vs. sharp plucked attacks currently experience the exact same static filter cutoffs in the feedback path.
