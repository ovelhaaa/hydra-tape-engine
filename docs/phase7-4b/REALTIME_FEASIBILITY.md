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

## 4. Physical ESP32-S3 Hardware Benchmark (`COM15`, 240 MHz, 44.1 kHz, 128 frames, 1,000 blocks + 100 warmup)
Measured via `scripts/bench_phase74b_esp32.py` on `env:esp32-s3-devkitc-1` (`HYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING=1`):

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

## 5. Qualification Summary
- **All four variants** run comfortably inside the `2,902.49 µs` ESP32-S3 hard deadline with **0 overruns** even under Multi-Head + Reverse + Spring load.
- **M1** (`-5.0%` mean vs M0) and **M2** (`-2.1%` mean vs M0) are **faster** than Classic C on physical ESP32-S3 hardware because they replace M0's dual per-channel biquads with a shared common/differential motion step.
- **M3** adds `~85–95 µs` (`+4.6%` to `+6.1%` vs M0) and remains well within budget (`<= 70.66%` P99, `<= 2,312 µs` peak).
- **Firmware status:** M0 remains flashed as production default on the board; M1/M2/M3 remain isolated in PR #60 pending promotion decision.
