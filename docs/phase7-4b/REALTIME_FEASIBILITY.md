# Phase 7.4B — ESP32-S3 Real-Time Feasibility Assessment

## 1. Target Hardware Budget
- **MCU:** ESP32-S3 @ 240 MHz
- **Audio Format:** 44,100 Hz stereo, 128-frame blocks
- **Hard Deadline:** 2,902.49 µs per block

## 2. Audio-Thread Safety Guarantees
All three experimental variants (M1, M2, M3) in `dsp/core/organic_motion_experiment.hpp` strictly satisfy real-time constraints:
- **Zero heap allocations** in `processStereoBlock()` or `computeStereoMod()`.
- **Zero locks, mutexes, or I/O** in the DSP path.
- **Zero `libm` trig/exp/pow calls per sample:** Only `float` arithmetic, LCG PRNGs, and polynomial `fastSin()`. All filter/leak/omega coefficients are precomputed in `OrganicMotionCoeffs::update()` during `updateFilters()`.
- **Single-precision `float` only:** No `double` operations in the per-sample loop.
- **Sample-rate independence:** Verified at both 44,100 Hz and 48,000 Hz.

## 3. Native Host Benchmark (x86_64, 44.1 kHz, 128 frames, Multi-Head `activeHeads=7` + Spring)
Measured in `phase74b_organic_motion_test` (2,000 blocks):

| Variant | Host x86_64 (µs / 128-frame block) | Relative to M0 |
|---|---:|---:|
| **M0 (Classic C)** | 33.511 µs | 1.00× |
| **M1 (Motor Drift)** | 33.252 µs | 0.99× |
| **M2 (Capstan Flutter)** | 32.426 µs | 0.97× |
| **M3 (Unstable Transport)** | 34.626 µs | 1.03× |

*Note:* M1 and M2 replace M0's two per-channel `BiquadFilter` calls (`flutterLPF`, `flutterLPFR`) and dual `mechanicalMod()` calls with a single shared common-mode + differential step, resulting in slightly lower host CPU cost than M0. M3 adds only a lightweight cooldown/event check and a 2nd-order damped oscillator (+3.3% on host).

## 4. Historical ESP32-S3 Benchmark — Preliminary (Build Flags Not Verified)

The following values were reported from the earlier hardware run on COM15 (240 MHz, 44.1 kHz, 128 frames, 1,000 measured blocks plus 100 warm-up). They remain useful historical observations but **are not qualified comparisons of M0–M3**. The original `scripts/bench_phase74b_esp32.py` set `PLATFORMIO_BUILD_FLAGS` for each mode, which may replace the inherited production flags. The old result has no checked-in verbose build logs or serial evidence to establish the effective `HYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING=1`, optimization, and PSRAM flags for each binary.

The script has now been corrected to select dedicated `esp32-s3-phase74b-m0` through `m3` environments, inheriting production `build_flags` from `platformio.ini`. It validates effective compiler flags against clean verbose build logs, captures firmware hashes and raw serial logs, and defaults to a **non-flashing configuration check**. A new physical run is required for performance qualification; the workflow/CI does not execute a physical benchmark.

To inspect configuration without touching hardware: `python scripts/bench_phase74b_esp32.py`.
To run a new, explicitly authorized physical test on the attached ESP32-S3: `python scripts/bench_phase74b_esp32.py --flash --port COM15`. This performs multiple uploads and restores M0 at the end only if the full experiment completes successfully.

### Scenario 1: Standard Single-Head (`hds=4`, `dly=340 ms`, `fbk=50%`, `spr=0`, `rev=0`)
| Variant | Mean (µs) | Budget Mean (%) | P99 (µs) | Budget P99 (%) | Peak (µs) | Overruns |
|---|---:|---:|---:|---:|---:|---:|
| **M0 (Classic C)** | 1558.38 | 53.69% | 1697 | 58.47% | 1867 | 0 |
| **M1 (Motor Drift)** | 1479.70 | 50.98% | 1626 | 56.02% | 1888 | 0 |
| **M2 (Capstan Flutter)** | 1525.40 | 52.55% | 1686 | 58.09% | 1891 | 0 |
| **M3 (Unstable Transport)** | 1653.90 | 56.98% | 1806 | 62.22% | 2039 | 0 |

### Scenario 2: Multi-Head + Spring (`hds=7`, `dly=340 ms`, `fbk=65%`, `spr=1`, `rev=0`)
| Variant | Mean (µs) | Budget Mean (%) | P99 (µs) | Budget P99 (%) | Peak (µs) | Overruns |
|---|---:|---:|---:|---:|---:|---:|
| **M0 (Classic C)** | 1838.43 | 63.34% | 1947 | 67.08% | 2165 | 0 |
| **M1 (Motor Drift)** | 1744.29 | 60.10% | 1865 | 64.26% | 2019 | 0 |
| **M2 (Capstan Flutter)** | 1797.47 | 61.93% | 1929 | 66.46% | 2070 | 0 |
| **M3 (Unstable Transport)** | 1923.20 | 66.26% | 2051 | 70.66% | 2312 | 0 |

### Scenario 3: Multi-Head + Reverse + Spring (`hds=7`, `dly=360 ms`, `fbk=75%`, `spr=1`, `rev=1`)
| Variant | Mean (µs) | Budget Mean (%) | P99 (µs) | Budget P99 (%) | Peak (µs) | Overruns |
|---|---:|---:|---:|---:|---:|---:|
| **M0 (Classic C)** | 1813.22 | 62.47% | 1938 | 66.77% | 2139 | 0 |
| **M1 (Motor Drift)** | 1712.50 | 59.00% | 1809 | 62.33% | 1954 | 0 |
| **M2 (Capstan Flutter)** | 1759.95 | 60.64% | 1860 | 64.08% | 2007 | 0 |
| **M3 (Unstable Transport)** | 1886.79 | 65.01% | 1994 | 68.70% | 2144 | 0 |

## 5. Qualification Status — Rebenchmark Required

- **DSP architecture / Native / WASM:** M0 bit-exact and the experimental M1/M2/M3 safety and audio checks passed on their respective pipelines. These results are independent of the physical benchmark issue.
- **Historical physical measurements:** three scenarios reportedly produced zero overruns, but effective firmware build flags were not independently archived and must not be used as a release gate or proof that the new modes are faster than M0.
- **Physical rerun:** `NOT_EXECUTED_SINCE_BENCHMARK_FIX`. Requires user-authorized flashing and fresh `artifacts/phase7-4b/esp32_s3_benchmark_verified.json`, verbose compiler logs, firmware SHA-256 hashes, and raw serial logs.
- **Production firmware:** M0 / Classic C is still the production default. All M1/M2/M3 variants remain experimental, and no experimental mode has been promoted.
