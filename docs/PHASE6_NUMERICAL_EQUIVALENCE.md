# Phase 6.4.1 numerical verification — current evidence

The historical Phase 6.4 account below describes Windows-local observations only;
its PASS statements do not establish current official Linux acceptance.

Initial local/remote HEAD was `eaa233ff32e169a2ff720d4bdace3d636c10eb7a`, with no
later commits. [Baseline hashes and CI failures](evidence/phase641-baseline.json).
Clean [official reproduction](https://github.com/ovelhaaa/hydra-tape-engine/actions/runs/44839840770)
passed Ubuntu 24.04 and failed Ubuntu 26.04 with exactly `max_abs=4.4615e-5`,
`RMSE=1.613509259565e-5`. Source, input bits and compiled LUT match.
A second clean run found identical GCC 14 and 15 failures on Ubuntu 26.04/glibc 2.43;
filter coefficients and arithmetic state differ although their libm return probes match.
The next control disables floating-point contraction to test the Ubuntu CPU target hypothesis.
Cache and test-input differences are not the cause of this failure.

| float32 LE data | SHA-256 in Native and WASM | Different values / max ULP |
|---|---|---|
| Stereo input, 4096 values | `e629e0248b9833c0f280f3771a07b95047e89d9efb3e9afddc06d3209ea559dc` | 0 / 0 |
| LUT, 1025 values | `d1724b91fed053e18aa3674b7862e8322b9ec9efbddab337fbddc030192238e5` | 0 / 0 |

The official comparator rejects bit differences before applying unchanged DSP
thresholds (1e-5 / 1e-6) and now runs all five scenarios. LUT CTest independently
verifies the hash, 1025 entries, endpoints, known points, symmetry, monotonicity,
80-digit Decimal regeneration and existing interpolation. No LUT format changed.
Diagnostic copies report states without modifying production DSP; instrumented and
Release outputs have identical hashes per backend. Source/binary/WASM/checkout hashes,
compiler/libc/Emscripten versions, CMake caches and compile commands are artifacts.

Local clean GCC 14.2 UCRT Native and WASM suites pass 16/16, strict five scenarios
max_abs=0 / RMSE=0. Double-sin is bit-identical on measured platforms, not a general
libm guarantee; any future input difference fails and requires a canonical fixture.

PCM24 correction: 3e-5 normalized amplitude equals approximately 252 LSB, not a
fraction of an LSB. One LSB is 2^-23. Final human audition remains pending.

## Historical Phase 6.4 measurements (not current Linux acceptance)

# Phase 6.4 — Native/WASM numerical equivalence (issue #48)

Date: 2026-11-20. Branch `feature/musical-validation-phase6`, PR [#54](https://github.com/ovelhaaa/hydra-tape-engine/pull/54).
Audited HEAD at the start of the phase: `4bfe60a70f7d7dae28b6eff0d4b3d748eeeff722`.
Approved baseline for comparison: `5e3d2bfa4ee8c5e6e765f7e11b12c6ffe87063c6`; last approved A/B package:
`7431a6db05abfde97838721cd089f94c7fed5047`.

Gate under investigation (unchanged): `max_abs <= 1e-5` and `RMSE <= 1e-6` on the
`tests/core/render_trace.cpp` stimulus at 48 kHz, 32-frame blocks, 2048 frames,
five scenarios, stereo.

**Historical Windows-local result only:** `max_abs=0`, `RMSE=0`. This did not establish
Linux CI acceptance, as demonstrated by the Phase 6.4.1 reproduction above.

## 1. Reported observations

| Environment | max_abs | RMSE | Status |
|---|---:|---:|---|
| Linux CI, phase-average candidate (reported in the Phase 6.3 material, not re-run here) | 3.221700000000e-05 | 1.317145864513e-05 | FAIL |
| Windows local, audited HEAD `4bfe60a`, rebuilt here | 7.659000000021e-06 | 2.473002165970e-06 | FAIL |
| Windows local, after the two fixes below | **0.0** | **0.0** | **PASS** |

The Windows figure was reproduced exactly from the audited HEAD before any change, which
confirmed the starting point. The Linux and Windows magnitudes are not equivalent, and
the difference between environments is itself part of the finding: the divergence is
driven by *which platform's libm produced a value that was then rounded to float*, so the
magnitude depends on the host libc.

## 2. Audit of the comparator

The comparator itself (`tests/core/compare_native_wasm.py`) is sound: it runs both
runners, rejects non-finite frames, requires equal length, accumulates `max_abs` and
RMSE over both channels, and writes JSON/TXT/CSV/WAV evidence. The failure was not in the
comparison arithmetic. It was in the two inputs to the comparison.

Audit of the harness found one defect:

* **The stimulus was generated independently on each platform.** `render_trace.cpp`
  computed the input with `std::sin` in `float`. `std::sin` is a platform libm call.
  A direct bit-level probe of the exact expression showed that **3 of the 2048 samples
  differ by exactly 1 ULP between the Windows UCRT and Emscripten/musl** — at sample
  indices **18, 599 and 1365**. The first Native/WASM output divergence was at
  **sample 18**, the same index. The comparator was therefore not feeding the two builds
  identical audio.

Everything else in the audit was correct: same sample rate (48 kHz), block size (32),
frame count (2048), parameter order and values, initial state, seeds (none used by this
stimulus), file format, channel handling, and Release flags (`-O3 -DNDEBUG -std=c++17`;
verified in `build-native/build.ninja` and `build-wasm/build.ninja`, no `-ffast-math`).
Native and WASM *were* executing the same algorithm.

## 3. First point of divergence, module by module

Final comparison of WAVs cannot localise a cause, so the DSP was instrumented offline.
An instrumented copy of `tape_core.cpp` (generated under `artifacts/`, never committed
into the DSP) stores eight already-computed values per sample: input, conditioned input,
playback output, feedback source, feedback, record signal, magnetic write, final output.
The instrumentation only stores values; it does not change arithmetic or state. A second
pass added the pre-filter playback signal, texture read and delay read.

With the deterministic stimulus applied to both builds:

| Stage | First divergent sample | Max abs (2048 frames) |
|---|---:|---:|
| input | none | 0 |
| conditioned input (HPF+LPF) | none | 0 |
| **magnetic write** | **1** | 3.3e-07 |
| playback output (delay read + EQ) | 201 | 7.18e-06 |
| feedback source | 201 | 1.69e-07 |
| feedback | 201 | 2.63e-07 |
| record signal | 224 | 2.38e-07 |
| final output | 207 | 7.18e-06 |

The first divergence in the whole engine is the **magnetic write at sample 1**. Input and
input conditioning are bit-identical, so the difference is created inside the magnetic
stage. Everything downstream (delay, feedback, output) merely propagates and amplifies it.

## 4. The responsible operation

`FastTanhLUT::init()` filled its 1025-entry table with `std::tanh(x)` at construction.
`x` is promoted to `double`, so the value is "double tanh rounded to float". A bit-level
probe of exactly those 1025 arguments showed **84 of 1025 entries differ by exactly 1 ULP**
between the Windows UCRT and Emscripten/musl tables. glibc also differs (see §6).

That is the whole mechanism. The magnetic stage is a feedback system
(`m = remanence * m + (1 - remanence) * y`, `feedback` up to 95 %), so a 1-ULP difference in
a table constant is amplified over 2048 samples to the observed ~7e-6.

The remaining transcendental calls in the DSP were checked and are *not* per-sample:
`pow`, `sin`, `cos`, `exp`, `sqrt` appear only in filter coefficient setup and in
`updateSampleRateConstants`. A probe of representative arguments found **0 differences**
between the two libms for those, and the conditioned-input stage is bit-identical, which
independently confirms it. `fast_sin`, `fastSatRational`, `fastRecip`, the Hermite
interpolators and every delay/feedback operation are plain IEEE arithmetic and were
already deterministic.

## 5. Correction applied

Two contained changes, one test-side and one DSP-side, each removing a platform libm value
that was being rounded to `float`:

1. **Stimulus (`tests/core/render_trace.cpp`).** The input is now evaluated in `double` and
   quantised once to `float`, so both runners receive identical bits. This is a test-harness
   correction, not a DSP change. With only this fix the gate still failed
   (`max_abs = 7.183e-06`, `RMSE = 2.099e-06`), which is what identified the second cause.

2. **Magnetic table (`dsp/core/tape_core.cpp`, `dsp/core/fast_tanh_table.inc`).** The
   runtime `std::tanh` table is replaced by 1025 committed `constexpr` float constants,
   generated by `scripts/generate_fast_tanh_table.py` with 60-digit decimal arithmetic and
   rounded exactly once to float32. The mapping is unchanged: same `[-4, 4]` domain, same
   1024 intervals, same clamp, same linear interpolation, same coercivity, remanence,
   normalization and bias gain ramp. Only the constants become host-independent, and the
   1025 construction-time `tanh` calls disappear. On ESP32 the table now lives in flash
   (`.rodata`) instead of 4 KiB of RAM.

The canonical table differs from the Windows table in 48 entries and from the Emscripten
table in 108 entries, always by 1 ULP. Those 1-ULP differences are what the fix removes;
they are also why the fix changes the rendered audio by a small amount (§8).

## 6. Why the Linux result was larger

The Linux CI figure (3.22e-05) is 4× the Windows figure (7.66e-06) for the same source.
That is consistent with what was measured here: glibc's `tanh` is *closer to correctly
rounded* than the Windows UCRT's, so on glibc more table entries land on the other side of
the float rounding boundary, and the perturbation is larger. The canonical table was
verified to differ from the Windows table in 48 entries and from the Emscripten table in
108; the Linux count was not measurable in this environment and is not claimed.

This environment difference is exactly why "Linux vs Windows are different" cannot be
treated as a tolerance question. It is a property of the host libm, and the fix removes the
host libm from the audio path.

## 7. Acceptance

| Check | Value | Limit | Result |
|---|---:|---:|---|
| `max_abs` | 0.0 | 1e-5 | PASS |
| `RMSE` | 0.0 | 1e-6 | PASS |
| Native 15/15 | 15 passed | 15 | PASS |
| WASM 15/15 | 15 passed | 15 | PASS |

`ctest --test-dir build-native` → 100 %, 15/15. `ctest --test-dir build-wasm` → 100 %,
15/15, including `native_vs_wasm_equivalence` and `native_wasm_ab_review_package`. The
three golden fixtures were **not** regenerated and still pass unchanged, because a 1-ULP
table change is far inside their 1e-5 / 1e-6 tolerance.

## 8. Audio impact and what it obliges

The DSP change is not inaudible in the bit sense. Rendering the same 6-second sources with
the approved Phase 6.3 build (`7431a6d`) and the Phase 6.4 candidate gives:

| Scenario | float max_abs | float RMSE | PCM24 LSB max | PCM24 samples changed |
|---|---:|---:|---:|---:|
| Clean guitar | 2.96e-05 | 4.20e-06 | 248 | 563 780 / 576 000 |
| Driven guitar | 6.33e-05 | 1.09e-05 | 532 | 571 225 / 576 000 |
| Conventional delay | 3.43e-05 | 4.72e-06 | 288 | 563 439 / 576 000 |
| Reverse 1/4 | 2.71e-05 | 4.45e-06 | 228 | 561 630 / 576 000 |
| Reverse 1/8 | 2.65e-05 | 4.22e-06 | 222 | 560 346 / 576 000 |
| Reverse high feedback | 3.23e-05 | 4.91e-06 | 271 | 564 277 / 576 000 |
| Multi-Head | 2.69e-05 | 2.15e-06 | 226 | 547 264 / 576 000 |
| Tape speed transition | 4.49e-02 | 1.27e-03 | 376 257 | 569 763 / 576 000 |
| BPM change | 2.61e-05 | 4.08e-06 | 219 | 563 992 / 576 000 |
| Long feedback | 2.71e-05 | 4.67e-06 | 228 | 564 710 / 576 000 |

The four pairs with a max around 2.6–6.3e-05 (peak −84 dBFS, approximately 218–528 PCM24 LSB; 1 LSB = 2^-23 normalized amplitude) are the LUT canonicalisation. The large "Tape speed transition" delta is the
separate transition fix ([transition analysis](PHASE6_TRANSITION_ANALYSIS.md)) and is
confined to the two automation seconds: the steady-state delta returns to ~1e-5.

Because the approved audio changes, the fix **requires** a new A/B package and a new human
listening decision; the Phase 6.3 approval does not carry over. Package:
`artifacts/listening-lab/phase6-final-review/` (suite `Hydra Phase 6.4 Final A/B`, 10
ordered pairs, hashes and telemetry, `PENDING_HUMAN_AUDITION`).

## 9. Residual risk

* The canonical table is generated data. `scripts/generate_fast_tanh_table.py` reproduces it
  byte-for-byte; the `.inc` is committed so a build never depends on Python.
* A future change to the magnetic mapping must regenerate the table deliberately; the
  generator and this document are the reference.
* The comparator now depends on a stimulus whose bits are platform-independent. Any new
  equivalence stimulus must use the same discipline (evaluate in `double`, quantise once),
  a bitwise input gate must verify the result; double precision alone is not a portable guarantee.
* No tolerance was relaxed, no scenario was removed and no golden was rewritten.
