# Phase 6.3 — Reverse real-time optimization, regression recovery and deadline hardening

Date: 2026-11-19. PR: [#54](https://github.com/ovelhaaa/hydra-tape-engine/pull/54). Branch:
`feature/musical-validation-phase6`; destination: `main`. Audited HEAD:
`c7c807a5bc182fdadd8b7d0067f8b4e9b5af10bf`. Approved DSP baseline: `5e3d2bf`.

**Candidate human audition: PASS — 10/10 (hash-verified `7431a6d` package). Technical
classification: BLOCKED — REGRESSION (issue #48 and complete-engine criteria). No merge.**
The three carrier-dependent Native tests pass, the full Native suite is 15/15, the physical
Reverse runs with zero overruns, and the 82 ms parameter-update stall is gone. The approved
phase-averaged bias is untouched and the before/after audio is bit-exact. Residual release
blockers that predate this work remain explicitly listed below (issue #48, four tail-growth
criteria and the 44.1 kHz parameter-transition criterion). See the
[human audition record](PHASE6_REVERSE_OPTIMIZATION_HUMAN_AUDITION.md).

## Baseline and failing tests

The Phase 6.2 candidate passed the isolated spectral and transfer criteria but failed
three Native tests because their assertions measured the removed triangular carrier, and
failed four complete-engine tail criteria plus one 44.1 kHz transition criterion. None of
those thresholds, goldens or scenarios were relaxed.

| Test | Old failure | Cause | Recovery |
|---|---|---|---|
| `core_warp_test` | `peak > 1e-6` at line 713 | Lower bound was satisfied by the old bias carrier (peak 2.8e-3) instead of by the 6e-7 probe | Now asserts `peak < 1e-5` (defect cannot return) and adds a real 220 Hz burst that must survive block edges/reset |
| `core_reverse_test` | `energyPpR > 0.001` at 1 s | Reverse V2 returns a frame-zero impulse at 2×hop, not at 1×hop; the old carrier supplied the missing energy | Test runs 6×hop and checks the actual L echo, the first ping-pong crossing and the real return near 4×hop |
| `core_transition_characterization` | `reverse marker missing` | The test searched at stimulus times; Reverse maps `t0` to `2*T − t0` | Observation windows are derived from the reverse window; adds a negative control without Reverse |

New equivalence test: `testBlockBoundaryEquivalence` compares the block-collected
boundaries with the per-sample predicate over 4 BPMs × 9 subdivisions × 400 blocks,
including a tempo change. It passes exactly.

## Profiling

Diagnostic firmware `env:esp32-s3-stageprofile` compiles `HYDRA_DSP_STAGE_PROFILE`;
production never defines it. The profiler uses `esp_cpu_get_cycle_count`, has no
allocation, lock or per-sample log, and is runtime-disabled outside a capture. Raw:
`artifacts/bias-antialiasing/reverse-profile-{before,after}.json`; capture script:
`scripts/capture_reverse_profile.py`.

Physical ESP32-S3, 240 MHz, 44.1 kHz, stereo 128 frames, 1000 blocks / 256 warmup:

| Stage (µs/block) | Delay before | Reverse 1/4 before | Delay after | Reverse 1/4 after |
|---|---:|---:|---:|---:|
| modulation/noise | 742.6 | 742.2 | 740.2 | 740.2 |
| reverse control | 25.7 | **1039.8** | 30.5 | **78.3** |
| record filters | 44.4 | 44.5 | 42.4 | 42.3 |
| conventional reads | 284.1 | 135.1 | 275.7 | 133.4 |
| reverse reads | — | 163.2 | — | 160.6 |
| playback filters | 112.5 | 112.5 | 115.7 | 115.7 |
| feedback | 254.0 | 254.1 | 262.0 | 262.0 |
| magnetic write | 175.7 | 174.9 | 176.3 | 175.3 |
| limiter | 53.7 | 54.4 | 53.0 | 53.0 |
| transport | 22.0 | 22.0 | 21.9 | 22.0 |

The Reverse-only cost was **reverse control**, not the reverse read or the magnetic stage.
`isSubdivisionBoundary` evaluated the double-precision musical position twice per sample;
the Xtensa LX7 has no double FPU, so each sample paid software double arithmetic. The
same cost did not appear in Delay because the boundary check is skipped there.

## Optimization 1 — block-collected subdivision boundaries

`processStereoBlock` collects the boundaries for the block once through the existing
zero-allocation `getSubdivisionBoundariesInBlock` and consumes offsets in the frame loop.
The solver is reconciled against the authoritative per-sample predicate at floating-point
ties and deduplicated, so window starts are sample-exact (verified by the new equivalence
test). No Reverse algorithm, crossfade shape or window length changed.

Result: reverse control 1039.8 → 78.3 µs/block; Reverse 1/4 total DSP from 2804.9 µs
(baseline physical mean) / 2940 µs P99 to **1948.7 µs mean / 2114 µs P99**.

## Optimization 2 — delay activation without a 3 MB wipe

`updateParams` cleared both 8 s PSRAM buffers (≈3 MB) whenever delay went inactive→active,
inside the audio task. That is the measured ~82 ms `MaxParamUpdUs`. The buffers are
zero-initialized at allocation and written on every frame even while delay is inactive, so
the wipe was redundant for the first activation.

The clear is replaced by a logical epoch: `framesSinceDelayEnable` marks the activation
instant, and reads older than the epoch return silence until naturally overwritten. The
first activation keeps the old semantics (no mask; zeroed buffers). Re-activations mask
pre-epoch samples and substitute the oldest valid sample for the interpolator's left
neighbour, so no old content leaks and no interpolation crosses the epoch.

Result: `MaxParamUpdUs` **82237 → 549 µs** (−99.3%). `core_regression` still proves that
re-enabling the delay with silence produces silence. An explicit `hydra_dsp_reset()` still
performs a destructive 3 MB wipe by design; it is not part of routine parameter automation
and must be scheduled outside the audio callback (residual limitation).

## Physical benchmark

Production firmware (`env:esp32-s3-devkitc-1`, profiling disabled), 44.1 kHz, 128 frames,
10 000 measured blocks after 256 warmup. Budget 2902.49 µs.

| Scenario | Mean µs | P95 | P99 | Peak | Overruns |
|---|---:|---:|---:|---:|---:|
| Conventional delay | 1866.7 | 1903 | 1913 | 2166 | 0/10000 |
| Reverse 1/4 | 1932.8 | 1977 | 2095 | 2246 | 0/10000 |
| Reverse 1/8 | 1932.8 | 1978 | 2031 | 2224 | 0/10000 |
| Reverse 1/16 | 1941.6 | 1979 | 2111 | 2263 | 0/10000 |
| Reverse feedback 95% | 1939.7 | 1978 | 2113 | 2250 | 0/10000 |
| Multi-Head | 2147.4 | 2181 | 2193 | 2441 | 0/10000 |
| Reverse, BPM 137 | 1933.9 | 1977 | 2085 | 2211 | 0/10000 |
| Reverse, BPM 90 | 1935.2 | 1977 | 2057 | 2236 | 0/10000 |
| Warp + spring stress | 1991.3 | 2037 | 2049 | 2263 | 0/10000 |

Five-minute sustain, Reverse 1/4: 97 241 blocks, **0 overruns**, 0 I2S write failures,
heap 121 528 bytes stable, PSRAM 1 663 048 bytes stable, audio-task stack high-water
mark 27 284 bytes, peak 2808 µs. The preferred P99<70% target (2032 µs) is **not** met by
Reverse 1/4, 1/16 and feedback 95% (2095–2113 µs, 72.2–72.8%) or Multi-Head (2193 µs,
75.6%); Reverse 1/8 reaches 2031 µs. The mandatory gate is met with zero overruns and
639–691 µs peak headroom (461 µs for Multi-Head). This is recorded, not hidden.

## Audio preservation

The optimization is bit-exact. `scripts/package_reverse_ab.py` builds the approved Phase 6.2
DSP from commit `5e3d2bf` and the optimized DSP from the working tree with the same compiler,
renders ten pairs (Reverse 1/4, 1/8, 1/16, high feedback, BPM change, MIDI clock, Multi-Head,
delay, Warp, combined stress) and compares them: **max_abs = 0, RMSE = 0, bit_exact = true**
for all ten. Package: `artifacts/listening-lab/phase6-reverse-optimization/` (20 WAVs, 10 CSVs,
manifest, player); archive `artifacts/hydra-phase6-reverse-ab.zip`. The responsible
reviewer exported a review JSON for this exact commit: **10/10 scenarios PASS**, all 30
WAV/CSV hashes verified; per-criterion dictionaries were empty, so no individual criterion
is inferred. See the [human audition record](PHASE6_REVERSE_OPTIMIZATION_HUMAN_AUDITION.md).
The Phase 6.2 listening package is preserved unchanged.

## Validation

| Gate | Result |
|---|---|
| Native CTest | **15/15**, including the three recovered tests and the new equivalence test |
| WASM functional CTest | 13/13 functional/golden/ABI tests pass; the two comparison tests are configured explicitly (see next row) |
| Native/WASM, unchanged thresholds | Ubuntu CI: max_abs 3.2217e-5 / RMSE 1.317145864513e-5. Windows local: max_abs 7.659e-6 / RMSE 2.473e-6. Both are **identical to Phase 6.2**; no new divergence. Issue #48 remains |
| ESP32-S3 build | SUCCESS, production and profile envs |
| ABI C 1.7 | symbol, layout, ID, preset and legacy-consumer tests pass |
| GTC / Tap Tempo / MIDI Clock | tests pass |
| Bias | phase-averaged implementation untouched (same source hash) |
| Goldens | all Native and WASM goldens pass unchanged |
| Human audition | **PASS 10/10** for the hash-verified `7431a6d` package; no per-criterion scores supplied |

CI (`.github/workflows/reverse-realtime.yml`) runs Native 15/15, the functional WASM suite,
both firmware builds, and the ten-pair A/B render, and uploads the artifact. It is
**intentionally red** at the explicit issue #48 comparison gate (`max_abs 1e-5`,
`RMSE 1e-6`, unchanged): the artifact is published before that gate. A green functional
run must not be read as issue #48 being fixed. Verified run
[44650706120](https://github.com/ovelhaaa/hydra-tape-engine/actions/runs/44650706120),
artifact [14590009952](https://github.com/ovelhaaa/hydra-tape-engine/actions/runs/44650706120/artifacts/14590009952)
(`hydra-phase6-reverse-ab`, 33 514 230 bytes, 10 pairs bit-exact).

## Residual limitations and explicit blockers

1. Issue #48 strict Native/WASM numerical equivalence is unchanged and remains open.
2. Complete-engine criteria remain 22/27 from Phase 6.2: four feedback/Multi-Head tail
   growth observations and the 44.1 kHz parameter-event step 0.05122 > 0.03. The DSP is
   bit-exact with Phase 6.2, so these are unchanged, not new; they remain explicit blockers.
3. Preferred P99<70% is not reached for Reverse/Multi-Head (see table).
4. Explicit `hydra_dsp_reset()` still performs a destructive 3 MB PSRAM wipe; callers must
   not invoke it from the audio callback.
5. Physical MIDI-clock-driven Reverse was not benchmarked (no external clock source on the
   bench); the A/B render includes MIDI clock pulses, and the Native MIDI-clock and routing
   tests pass.
6. Allocation absence is a source audit plus stable heap/PSRAM endpoints, not an
   instrumented per-block allocation counter. Stack is instrumented.

## Reproduce

```sh
cmake -S . -B build-native -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-native -j
ctest --test-dir build-native --output-on-failure
python scripts/capture_reverse_profile.py --port <PORT> --output artifacts/bias-antialiasing/reverse-profile.json
python scripts/benchmark_reverse_hardware.py --port <PORT> --output artifacts/bias-antialiasing/reverse-bench.json
python scripts/package_reverse_ab.py
```

The profile command requires `env:esp32-s3-stageprofile`; the benchmark requires the
production firmware. Raw serial captures accompany every physical number in
`artifacts/bias-antialiasing/reverse-bench-final.json`.
