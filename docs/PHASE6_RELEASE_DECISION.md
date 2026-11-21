# Phase 6.4.1 — Release Decision Record

Date: 2026-11-20. Repository `ovelhaaa/hydra-tape-engine`, existing
[PR #54](https://github.com/ovelhaaa/hydra-tape-engine/pull/54),
`feature/musical-validation-phase6` → `main`. No merge performed.

## Classification

**BLOCKED — FINAL BIAS/QA CI REVALIDATION PENDING.** Human listening and
physical runtime remain pending. The earlier Phase 6.4 statement that all automatic
gates passed was incorrect: it extrapolated Windows observations to Linux CI.

Baseline HEAD `eaa233ff32e169a2ff720d4bdace3d636c10eb7a`, with no remote commits
after it at session start. [Baseline evidence](evidence/phase641-baseline.json)
records critical-source hashes and all initial failed workflows.

## Gate matrix

| Gate | Result | Evidence |
|---|---|---|
| Native | PASS locally; final Linux pending | clean GCC 14.2, CTest 16/16 including LUT integrity, ABI and unchanged goldens |
| WASM | PASS locally; final Linux pending | clean Emscripten, CTest 16/16, unchanged goldens |
| Native/WASM Strict | PASS official, both Ubuntu versions | [Core 44841311343](https://github.com/ovelhaaa/hydra-tape-engine/actions/runs/44841311343), five scenarios max_abs=RMSE=0; FMA contraction cause demonstrated |
| Bias Anti-Aliasing | PASS locally; final Linux pending | six rebuilt variants, spectral/transfer 49/49; recovered whine diagnostic and plots |
| Reverse | PASS locally; final Linux pending | frozen Phase 6.3 optimization comparison 10/10 bit-exact; complete current reverse CTests |
| Feedback | PASS locally; final Linux pending | full-engine 27/27; six 60 s cases; pre-limiter internal windows stable, max growth +0.0125211 dB |
| Transitions | PASS locally; final Linux pending | five parameters, 44.1/48 kHz max 0.01797/0.01420 <= 0.03 |
| Web Demo | PASS locally; final Linux pending | clean web build, host 40/40; [Web 44841311426](https://github.com/ovelhaaa/hydra-tape-engine/actions/runs/44841311426) passed with real Chrome |
| AudioWorklet Reset | PASS locally; final Linux pending | real browser + clean WASM A–E, artifacts/phase641/reset-clean/result.json |
| Listening Lab | PASS locally; final publication pending | 10 pairs, 20 stereo PCM24 WAVs, 10 CSVs; real Chrome player test PASS |
| ESP32-S3 Build | PASS locally | production RAM 26304 B, flash 627743 B, firmware SHA-256 ed68bc89e0f9bf508c2a181680aca80fec99602e6605e588b023d3e2872ddabd |
| ESP32-S3 Runtime | PENDING | PHYSICAL_RUNTIME_PENDING; console unresponsive after RTS reset |
| Human Listening | PENDING | PENDING_HUMAN_AUDITION; prior signoff does not transfer |

## Corrections and preserved behavior

No production DSP algorithm, phase-averaged bias, Reverse V2, GTC, Tap Tempo,
MIDI Clock or C ABI 1.7 was changed. Thresholds and goldens remain unchanged.
The official comparator now verifies input/LUT bit equality independently of
DSP tolerances and runs all five scenarios. Native build-directory caches were
removed; checkout/compiler/libc/source/binary/WASM/flags are recorded.

Historical bias models explicitly restore their historical LUT and initialization.
Modern averaged diagnostics compile exact production source. The old six-second
engine report retains its four decay observations (23/27); only these named
observations are superseded by the separately required 60-second stability
measurement. Other original gates must pass. Reports never label those old
observations as passed.

Reset requests are serialized and acknowledgements correlated. Exceptions/timeouts
show errors and retain suspension; stopped audio stays stopped. Real browser tests
confirm reset/ack delivery while suspended; no graph reconstruction was needed.

## Listening delivery

The final package is generated reproducibly by `scripts/package_phase6_final.py`
under `artifacts/listening-lab/phase6-final-review/`, with SHA-256, manifest,
reference/build commits, parameters, seeds, events, telemetry, player and guide.
The feedback pair lasts 60 seconds. The local ZIP digest is
`325328e2b9a6d38006328bfdd2be2f5d499244e93bd309b60df20b347ea44ffb`;
this is not the final CI artifact digest. CI validates files and exercises all ten
pairs in real Chrome before publishing. A verified intermediate-code [artifact](https://github.com/ovelhaaa/hydra-tape-engine/actions/runs/44841311259/artifacts/14681560845) exists. Final QA adds the ten-pair guide; the latest release-head artifact will be linked after successful validation.

PCM24 units: one signed normalized LSB is 2^-23 = 1.1920929e-7.
A difference of 3e-5 is approximately 252 LSB, or -90.46 dBFS peak error.
The prior description as a fraction of one LSB was wrong. Small normalized
amplitude differences are not evidence of sub-LSB identity or human approval.

## Physical follow-up

COM15, VID 303A:1001, serial/MAC B8:F8:62:E3:E4:A4 was identified using esptool:
ESP32-S3 rev0.2, 8 MB PSRAM, USB Serial/JTAG. RTS hard reset succeeded at the
programmer level; firmware help/banner still produced zero serial bytes.
No current audio loop, deadline, overrun, I2S, heap, PSRAM, stack or benchmark
measurement is claimed. Prior Phase 6.3 measurements are historical only.

Manual next step: disconnect/reconnect USB power with BOOT released for normal
boot, pulse EN if accessible, identify the reenumerated port and verify firmware
banner/help before running benchmarks. Repeat delay, Reverse 1/4, 1/8, 1/16,
95% feedback, Multi-Head and combined stress. Require zero overruns/I2S faults,
stable heap/PSRAM, adequate stack and no deadline regression. Physical access
is not blocking CI repair. External MIDI hardware remains unavailable.

## Reproduction

Use the checked-in Core, Bias, Reverse, Listening and Web workflows; their
artifacts retain reports, hashes and build metadata. Local CTest on this Windows
host requires TMP/TEMP pointing at an accessible workspace directory; using the
host temporary directory caused file-open failures, resolved by selecting
`artifacts/phase641/tmp`. Tests were rerun, not disabled.

See [numerical report](PHASE6_NUMERICAL_EQUIVALENCE.md),
[bias report](PHASE6_BIAS_ANTIALIASING_REPORT.md),
[feedback report](PHASE6_FEEDBACK_STABILITY.md),
[transition report](PHASE6_TRANSITION_ANALYSIS.md), and
[reset contract](PHASE6_RESET_CONTRACT.md).

## Final QA corrections awaiting release-head CI

Core, Reverse, Web and Listening workflows passed on corrected code `60a9da6`.
Bias run `44841311254` failed only at its browser gate: the expanded player test
incorrectly demanded automation in the static bias-ablation package. It now
validates real timeline markers for static packages and actual automation where
present; the complete sixty-scenario Chrome test passed locally. No audio scenario
or test was removed. Whine is now a required standalone CI diagnostic; internal
feedback probes share the full-engine input and must match output bits. The final
package now has its own ten-pair guide. These changes require the new CI run.
