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
The controlled build with `-ffp-contract=off` then produced zero output and state differences
in every scenario on that same Ubuntu 26.04 machine. See the confirmed cause below.
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


## Confirmed root cause and contained build correction

[Control run 44840803894](https://github.com/ovelhaaa/hydra-tape-engine/actions/runs/44840803894)
compiles the same source twice: default flags versus `-ffp-contract=off`.
The Ubuntu 26.04 native default defines `__FMA__` and `__AVX2__`; the downloaded
Release executable contains 544 FMA-family instructions. Scalar WASM performs
separate multiply and add rounding. GCC contracts expressions without requiring
`-ffast-math`, changing biquad coefficients, modulation and feedback state bits.
The first setup differences include HighShelf coefficients; conditioned input differs
at frame 2 on the right channel, magnetic state at frame 3, and output at frame 202.
Equal sin/cos probes, equal input/LUT and the GCC14/15 control exclude the prior
libm/stimulus explanation for this particular failure.

| Scenario | Default native max_abs | Default RMSE | Contraction off max_abs / RMSE |
|---|---:|---:|---|
| baseline_glue | 4.4615e-5 | 1.613509259565e-5 | 0 / 0 |
| short_slap_bright | 1.359e-5 | 6.05834890511e-6 | 0 / 0 |
| feedback_edge | 7.1668e-5 | 2.78475064727e-5 | 0 / 0 |
| saturated_dark | 2.00093e-4 | 6.05443659255e-5 | 0 / 0 |
| modulated_space | 4.9889e-5 | 2.18850018498e-5 | 0 / 0 |

All instrumented filter/per-channel state probes also match under contraction off,
and instrumentation preserves Release output hashes in each backend, all scenarios.
[Measured control, source/binary hashes and toolchains](evidence/phase641-contraction-control.json).

`CMakeLists.txt` explicitly applies `-ffp-contract=off` to C++ targets under GNU/Clang,
including separately compiled web/trace/ABI targets. The production DSP source and
LUT are unchanged from the audited HEAD. PlatformIO ESP32 flags are untouched.
This restores the already-approved separate-rounding behavior rather than adding
an effect or changing parameter semantics. No fast-math, threshold or golden change.
Both official workflows use the same CMake targets, Emscripten 3.1.62, Release,
48 kHz, 32-frame blocks, 2048 frames and five scenarios. The official matrix keeps
Ubuntu 24.04 and 26.04, so pinning an older image does not conceal the failure.

## Reproduction and acceptance

Clean Native/WASM build directories, configure Release with the checked-in CMake,
then run `tests/core/compare_native_wasm.py --native <runner> --wasm-js <runner.js>`.
`verify_numerical_inputs.py` emits bit counts, first mismatch, ULP and SHA-256;
`diagnose_equivalence.py --current` reproduces the uncontrolled compiler default,
and `--extra-flags=-ffp-contract=off` reproduces the arithmetic control. The official
workflow asserts five matching state traces in the controlled build and archives
both experiments, compile commands, target defaults, libc and executable hashes.

Final official-head gate result is recorded in the Release Decision Record. Local
success or the passing control alone is not substituted for that gate.

## PCM24 impact and listening

Phase 6.4.1 changes no DSP source. Phase 6.4's canonical LUT and filter smoothing
changed the prior approved audio, so the final A/B compares the approved Phase 6.3
commit `7431a6db05abfde97838721cd089f94c7fed5047` against the current commit.
Typical static differences around 2.6–6.3e-5 equal roughly 218–528 PCM24 LSB;
3e-5 is about 252 LSB (-90.46 dBFS peak error), never a fraction of one LSB.
The automated test/package result does not establish human listening approval.
