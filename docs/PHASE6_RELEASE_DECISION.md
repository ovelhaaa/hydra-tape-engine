# Phase 6.4 — Release Decision Record

Date: 2026-11-20. Repository `ovelhaaa/hydra-tape-engine`. PR
[#54](https://github.com/ovelhaaa/hydra-tape-engine/pull/54), branch
`feature/musical-validation-phase6`, destination `main`. **No merge performed or authorised.**

* Audited HEAD at the start of the phase: `4bfe60a70f7d7dae28b6eff0d4b3d748eeeff722`
* Approved DSP baseline: `5e3d2bfa4ee8c5e6e765f7e11b12c6ffe87063c6`
* Last human-approved A/B package: `7431a6db05abfde97838721cd089f94c7fed5047`
* Phase 6.4 head: the commit that contains this record (recorded in the PR and in
  `artifacts/listening-lab/phase6-final-review/manifest.json` as `reference_commit`).

## Classification

**READY FOR FINAL HUMAN LISTENING**

Every automated technical gate passes except one: the physical ESP32-S3 runtime was not
re-measured, because the board's USB console stopped responding after flashing (§6.1). That
is an access/measurement gap, not an observed regression, and it is an explicit merge
blocker. The other remaining gate is human audition of the audio produced after two
deliberate DSP changes. No result in this document is inferred: each row cites the command or
artifact that produced it.

## Gate matrix

| Gate | Result | Evidence | Pending |
|---|---|---|---|
| Native | PASS | `ctest --test-dir build-native` → 15/15 | — |
| WASM funcional | PASS | `ctest --test-dir build-wasm` → 15/15 | — |
| Native/WASM Strict | PASS | `max_abs = 0`, `RMSE = 0` (`tests/core/compare_native_wasm.py`) | — |
| Bias spectral | PASS | isolated 49/49 preserved from Phase 6.2; mapping untouched | not re-run in 6.4 (no baseline libraries rebuilt) |
| Reverse performance | **NOT RE-MEASURED** | firmware built and flashed, hash verified; the board's USB console did not respond, and a previously validated image behaved identically | re-measure after a physical power cycle |
| Feedback stability | PASS | 27/27 complete-engine gates (`scripts/phase64_validate_engine.py`) | — |
| Parameter transitions | PASS | 44.1 kHz 0.01441, 48 kHz 0.01286 vs limit 0.03 | — |
| Web Demo | PASS | `hydra_dsp_web` build; web host tests 40/40 | — |
| Listening Lab | PASS | `Hydra Phase 6.4 Final A/B`, 10 pairs validated, 35 050 624 bytes | archive upload on CI |
| ABI | PASS | `core_abi_symbols`, `core_abi_legacy_consumer`; API 1.7 unchanged | — |
| Human listening | **PENDING** | package published, `PENDING_HUMAN_AUDITION` | reviewer decision |

## 1. What changed

Three DSP/test changes and two host changes, all contained:

1. `dsp/core/fast_tanh_table.inc` (new) + `dsp/core/tape_core.cpp`: the magnetic `tanh`
   lookup table is 1025 committed canonical float constants instead of 1025 `std::tanh`
   calls at construction. Same mapping, same interpolation, same clamp; the only change is
   that the constants no longer depend on the host libm.
2. `tests/core/render_trace.cpp`: the comparator stimulus is evaluated in `double` and
   quantised once to `float`, so Native and WASM receive identical input bits.
3. `dsp/core/tape_core.cpp`: playback-filter coefficients (head bump, rolloff shelf,
   gap-loss lowpass) are ramped per block over 20 ms when tape speed, tape age, tone or head
   bump changes after audio has started. The initial configuration snaps, so offline renders
   and goldens are unchanged.
4. `host/web/main.js`: the reset button suspends the `AudioContext` before posting reset and
   resumes after the worklet acknowledgement, so the 6.18 MB wipe never runs against a live
   render deadline.
5. `scripts/`: `generate_fast_tanh_table.py`, `phase64_audio_delta.py`,
   `phase64_feedback_stability.py`, `phase64_transition_analysis.py`,
   `phase64_validate_engine.py`, `package_phase6_final_ab.py`; the Listening Lab accepts the
   new 10-pair suite.

Nothing else in the DSP changed. Reverse V2, the delay epoch, the GTC, tap tempo, MIDI
clock, the bias mappings, the C ABI version and the goldens are untouched.

## 2. Numerical equivalence (issue #48)

The divergence had two independent causes, both located by module instrumentation, both
platform libm values rounded to `float`: the comparator stimulus (3 of 2048 samples, the
first at exactly sample 18, which is where the output first diverged) and the magnetic
`tanh` table (84 of 1025 entries, 1 ULP, first divergence in the magnetic write at sample 1).
After both fixes Native and WASM are bit-exact at every stage of every scenario.

Full analysis: [PHASE6_NUMERICAL_EQUIVALENCE.md](PHASE6_NUMERICAL_EQUIVALENCE.md).

## 3. Feedback and tails

The four Phase 6.2 failures were 0.001–0.038 dB. Over 60 s all four cases reach a bounded,
spectrally stable limit cycle (slope ≤ 5e-5 dB/s, peaks constant, spectrum identical at 30 s
and 59 s, zero samples at the limiter ceiling, internal pre-limiter state stable to
±0.0125 dB over 50 s). `reverse` decays to the noise floor. The decay-only rule was replaced
by explicit stability gates; all seven scenarios are retained and the complete-engine suite
passes 27/27.

Full analysis: [PHASE6_FEEDBACK_STABILITY.md](PHASE6_FEEDBACK_STABILITY.md).

## 4. Parameter transitions

The 44.1 kHz failure was a one-sample impulse at the block where tape speed changed: the
playback biquads received new coefficients while retaining their state. Freezing the
tape-speed dependence removed the impulse (0.04952 → 0.00004), which proved the mechanism.
A 20 ms per-block ramp of the four driving values reduced 0.05122 → 0.01441 at 44.1 kHz and
0.02540 → 0.01286 at 48 kHz, against the unchanged 0.03 limit. Goldens pass unchanged.

Full analysis: [PHASE6_TRANSITION_ANALYSIS.md](PHASE6_TRANSITION_ANALYSIS.md).

## 5. Reset contract

`hydra_dsp_reset()` is destructive and synchronous (768 KB to 6.18 MB depending on
configuration). It is not reachable from the native or ESP32 audio callback. It was reachable
from the web AudioWorklet message handler, i.e. the rendering thread, which is a contract
violation; the host now suspends the context around it. The ABI and the destructive
semantics are unchanged.

Contract: [PHASE6_RESET_CONTRACT.md](PHASE6_RESET_CONTRACT.md).

## 6. Physical ESP32-S3

Firmware rebuilt and re-flashed after the DSP change:

| Item | Value |
|---|---|
| Environment | `esp32-s3-devkitc-1`, ESP32-S3 rev v0.2, 240 MHz, 16 MB flash |
| RAM | 26 304 B / 327 680 B (8.0 %) |
| Flash | 627 743 B / 16 777 216 B (3.7 %) |
| Upload | `platformio run -t upload --upload-port COM15` — SUCCESS, hash verified per segment |
| Board | ESP32-S3 on COM15, MAC `b8:f8:62:e3:e4:a4` |

### 6.1 Runtime re-measurement: not obtained

The board was re-flashed with the Phase 6.4 image (all four segments written and hash
verified; the app segment was written twice and verified). The serial CLI on the native USB
CDC (`Serial`, 115200, `ARDUINO_USB_CDC_ON_BOOT=1`) then produced **no output at all**: no
banner, no prompt, no response to `help` or `\n`, with DTR/RTS asserted and released, and
after `esptool` hard resets.

To separate a bad image from a board/console condition, the previously validated app image
at `artifacts/release-gate/pio-final/firmware.bin` was flashed to the same offset and probed
identically. It was **equally silent**. The Phase 6.4 firmware was then restored and
verified. The most likely explanation is that the board remained in download mode because
the RTS reset is not effective over this USB-Serial/JTAG setup; recovering it needs a
physical power cycle or reset button, which this session cannot perform.

**No physical runtime number is claimed for the Phase 6.4 firmware.** The Phase 6.3 baseline
(zero overruns on nine 10 000-block scenarios, zero overruns over five minutes, P99 Reverse
2.095–2.113 ms, P99 Multi-Head ≈ 2.193 ms, stable heap/PSRAM/stack) remains the last valid
physical measurement and applies to a firmware that differs in the tanh table and in the
transition ramp only. It must not be presented as a Phase 6.4 measurement.

## 7. Regression summary

| Suite | Result |
|---|---|
| Native CTest | 15/15 |
| WASM CTest | 15/15 |
| Native/WASM strict | PASS, bit-exact |
| Web host tests (`node --test host/web/tests/*.test.mjs`) | 40/40 |
| Web demo build (`hydra_dsp_web`) | SUCCESS, `hydra_dsp.wasm` 77 193 B |
| ESP32-S3 firmware build | SUCCESS |
| Complete-engine gates | 27/27 |
| Goldens | 3/3 pass, not regenerated |
| ABI C 1.7 | symbols and legacy consumer pass |

## 8. Audio change and human listening

The candidate audio differs from the approved `7431a6d` package. The tanh-table
canonicalisation is ≈ −84 dBFS (about a quarter of one 24-bit LSB) on static material; the
playback-filter ramp is an audible, intended change that only occurs while tape speed, tape
age, tone or head bump is automated during playback (max 0.045 during the transition
seconds, back to ~1e-5 afterwards).

Consequently the Phase 6.2 12/12 and Phase 6.3 10/10 approvals do **not** carry over. A new
package is published:

* `artifacts/listening-lab/phase6-final-review/`
* suite `Hydra Phase 6.4 Final A/B`, 10 ordered pairs, 20 PCM24 WAVs, 10 telemetry CSVs
* 35 050 624 bytes of audio/telemetry, all SHA-256 and frame counts verified
* archive `artifacts/hydra-phase6-final-ab.zip`
* `human_listening_signoff: PENDING_HUMAN_AUDITION`

The ten scenarios are clean guitar, driven guitar, conventional delay, Reverse 1/4,
Reverse 1/8, Reverse with high feedback, Multi-Head, tape-speed transition, BPM change and
long feedback. Same source, gain, seed, sample rate and events on both sides; A/B uses the
existing Listening Lab player with its 8 ms complementary gain crossfade.

## 9. Pending items before merge

1. **Human listening** of the Phase 6.4 package. No approval is inferred here.
2. **Physical runtime numbers** (§6.1) must be recorded and compared with the Phase 6.3
   baseline (zero overruns, P99 Reverse ≈ 2.095–2.113 ms, P99 Multi-Head ≈ 2.193 ms).
3. **External MIDI clock hardware** was not available; 90/120/140 BPM, Stop/Continue and
   clock loss/recovery were validated in simulation only
   (`scripts/render_midi_clock_review.py`, Native tests), not against a physical MIDI source.
   Not claimed as a physical pass.
4. **Browser verification of the suspend-reset-resume flow** is pending; the change is
   host-only and does not alter audio.
5. **CI** must run the updated `core-equivalence.yml` on the final commit; the Linux CI
   numbers in this phase were reproduced on Windows, not on Linux.

## 10. Explicit non-actions

No new effect. No change to the bias mappings, Reverse V2, the GTC, tap tempo or MIDI clock.
No dynamic allocation, lock or synchronous large-buffer clear added to any callback. No
threshold relaxed, no golden regenerated, no test disabled, no scenario removed. No merge.
