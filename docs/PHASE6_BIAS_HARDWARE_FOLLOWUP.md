# Phase 6.2 — physical performance and verified CI follow-up

2026-11-19. This supplements the [anti-aliasing report](PHASE6_BIAS_ANTIALIASING_REPORT.md). DSP source tested: `5e3d2bfa4ee8c5e6e765f7e11b12c6ffe87063c6`; subsequent evidence/tooling changes do not change the magnetic algorithm.

**BLOCKED — REGRESSION / PERFORMANCE.** The phase-averaged candidate remains the best measured compromise among the six alternatives, but rhythmic Reverse exceeds the physical block deadline in both baseline and candidate. No budget, threshold, original assertion, golden or Reverse algorithm was changed. Subsequent human audition approved the candidate 12/12 in the exact f0c16cd local package; see the [human audition record](PHASE6_BIAS_HUMAN_AUDITION.md). No merge.

## Identified board and firmware provenance

The user disconnected the other ESP32; the remaining Espressif USB device was COM15. The candidate was flashed successfully with esptool hash verification. All six variants were then built/flashed from an isolated git-archive project copy under `artifacts`, without overwriting production source. Every upload and the final restoration of the production candidate succeeded. After restoration, serial confirmation shows generator, Reverse, Warp and spring OFF. Built-in generator defaults ON after reboot, so it was explicitly switched off after testing.

[Candidate measurement JSON](evidence/phase6-bias-hardware.json), [six-variant measurement JSON](evidence/phase6-bias-hardware-variants.json), [raw serial/build/upload logs](evidence/phase6-bias-hardware/). Firmware/source hashes are recorded. The firmware's main translation-unit date can predate the magnetic object's rebuild; its printed date alone is not commit provenance. Upload logs verify the actual image hash. Variant source hashes use LF-normalized text; raw filesystem source hashes may differ by line endings.

## Matched physical comparison

ESP32-S3 at 240 MHz, 44.1 kHz, stereo 128 frames, 2902.49 µs block budget. Each variant ran delay and rhythmic Reverse for 1000 measured blocks after 256 warmup blocks. Raw counts and non-saturated histograms were crosschecked for all 12 measurements. Physical 48 kHz was not run.

Identical CLI configuration per variant: source=1 (built-in synth, **not a 440 Hz sine**), delay 500 ms, feedback 40%, head mask 4, mix 50%, drive 75, speed/age 50, noise 10, no Freeverb/Warp/spring. Flutter 8 and wow 6 are CLI percentages mapping to approximately 3.6/2.1 core values, printed as 4/2. Source/event phase was not synchronized between variants. These single ordered trials are comparative feasibility evidence, not statistical bounds or isolated magnetic-stage timing.

| Variant | Delay mean/P95/P99/peak µs | Overruns | Reverse mean/P95/P99/peak µs | Overruns |
|---|---:|---:|---:|---:|
| baseline | 1783.79/1831.00/1841.00/1983.00 | 0/1000 | 2804.91/2856.00/2919.00/3045.00 | 18/1000 |
| bias_off | 1790.19/1828.00/1881.00/2095.00 | 0/1000 | 2810.25/2845.00/2957.00/3076.00 | 22/1000 |
| sine | 2045.23/2088.00/2115.00/2290.00 | 0/1000 | 3061.81/3109.00/3220.00/3330.00 | 1000/1000 |
| bandlimited | 2035.60/2086.00/2098.00/2294.00 | 0/1000 | 3068.00/3110.00/3217.00/3334.00 | 1000/1000 |
| oversampled | 4776.27/4818.00/4828.00/5062.00 | 1000/1000 | 5812.34/5850.00/5964.00/6051.00 | 1000/1000 |
| averaged | 1807.37/1856.00/1865.00/2053.00 | 0/1000 | 2827.01/2878.00/2940.00/3111.00 | 20/1000 |

The observed mean timed-loop differences F minus baseline are +23.58 µs (+1.32%) for delay and +22.10 µs (+0.79%) for Reverse. Cache/layout/run variability prevents attributing every microsecond exclusively to quadrature; bias-off also varied slightly above baseline. Baseline Reverse already failed with 18 overruns; F failed with 20. Sine/band-limited variants had 100% Reverse overruns. The evaluated 2× FIR prototype had 100% overruns in both workloads. A zero-overrun release gate is not met.

## Candidate extended measurements and limits

The candidate-only run measured 3000 blocks per configuration after 256 warmup:

| Configuration | P99 µs | Overruns / 3000 |
|---|---:|---:|
| Standard delay | 1866 | 0 |
| Rhythmic Reverse | 2965 | 67 |
| Rhythmic Reverse, feedback 95% | 2994 | 81 |
| Multi-Head, feedback 95% | 2105 | 0 |
| Warp + spring, rev flag set | 2013 | 0 |
| Same routed chain after sustain | 2016 | 0 |

Multi-Head P99 is 72.52% of budget, above the preferred 70%. Full values, including P95/peak, are in JSON/raw logs.

The 120-second sustain exercised **Warp and spring**. The CLI also prints rev=1, but existing routing requires !warpActive for Reverse, so this does not establish simultaneous Reverse coverage. Cumulative overruns remained 351 throughout sustain. The two measured Reverse windows account for 148; another 203 occurred outside those windows during the session. No claim of a zero-overrun session is made.

Every recorded benchmark reported zero I2S write failures. This counts errors/short writes, not uninstrumented DMA underruns or audible dropouts; successful writes coexist with missed DSP deadlines. The timer includes source generation, TapeCore, enabled external stages and PCM preparation, but excludes parameter updates and blocking I2S writes. It is neither TapeCore-only nor end-to-end callback latency.

Heap/PSRAM endpoints match within individual measurements. The candidate session's heap changed from 122928 to 122728 bytes and was then stable during sustain; PSRAM remained 1663048 bytes free. Stack and allocation counts were not instrumented. Stable heap endpoints do not prove allocation-free processing.

Raw cumulative MaxParamUpdUs reached 81816 µs in the candidate session; the matched comparison showed about 82 ms in every variant, including baseline 82237 and F 82283 µs. The triggering command was not isolated. An existing possible contributor is delay activation clearing both large PSRAM buffers; no cause is asserted without isolation. These excluded stalls prevent an end-to-end realtime-safety claim even when a measured block passes.

The first candidate suite sent legacy `wet 50`, which does not set mix; the freshly booted firmware default was 50%. The matched six-model suite used valid `mix 50`. Both command histories and actual reported configuration are retained.

## Verified Linux delivery and numerical gates

[Run 44608110989](https://github.com/ovelhaaa/hydra-tape-engine/actions/runs/44608110989) published:

* [60-pair A/B package](https://github.com/ovelhaaa/hydra-tape-engine/actions/runs/44608110989/artifacts/14572426486): 194889940 bytes, SHA256 `dc79c5821c32b510df15d68d7b8d170cae038527e53cd8553dda01d7aaa6327f`, retention to 2026-12-19.
* [Technical evidence](https://github.com/ovelhaaa/hydra-tape-engine/actions/runs/44608110989/artifacts/14572406706): downloaded archive SHA256 `64cfb153e13182e8ecff90a353a68f64c8b13b6ce1c0904745996056a7d988eb` verified locally.

Manifest source HEAD is `5e3d2bfa4ee8c5e6e765f7e11b12c6ffe87063c6`: 60 pairs, 120 PCM24 stereo WAVs and 60 CSVs. Actual Chrome Web Audio passed all 60 comparisons, hashes, decoding, synchronized controls, review persistence and missing-file handling. Human audition remains NOT REVIEWED. Later documentation/tooling commits are separate from this immutable verified package; newest artifact links are recorded in PR #54 after verification.

Ubuntu 24.04 CI: isolated 49/49; complete engine 22/27; Native 12/15; WASM 11/13 (two carrier-related assertions fail). All three Native and three WASM goldens pass; firmware builds. Final job correctly FAILS native_tests/engine/wasm_tests/equivalence. The original Phase 6 Listening Lab workflow fails on the unchanged Native assertions; Web Demo passes. Core's AddressSanitizer legacy ABI consumer passes; the combined Warp/ABI job fails on the Warp assertion, with no observed sanitizer memory violation.

| Matched Ubuntu 24.04 O3 | max_abs | RMSE | Strict state |
|---|---:|---:|---|
| Original triangle | 3.7671e-5 | 1.403858027725e-5 | FAIL |
| Phase average | 3.2217e-5 | 1.317145864513e-5 | FAIL |

RMSE difference is −8.67121632126e-7, but both builds exceed both unchanged thresholds. [Linux matched evidence](evidence/phase6-bias-linux-regressions.json). Core on ubuntu-latest separately measured candidate max_abs 3.466e-5 / RMSE 1.455802807777e-5. These are separate toolchains from the Windows local and historical CI measurements; do not mix their scalar differences.

After the physical bottleneck appeared, a one-LUT second-moment Taylor probe was tested only in isolated source copies. Spot sine/high-drive tests and transfer errors stayed within the original limits; high-drive nonharmonic residual was about 11 dB above two-point quadrature. It has not completed the full suite, board benchmark or listening comparison and was not selected to bypass the Reverse failure. [Probe measurements](evidence/phase6-bias-moment-probe.json). Official DSP remains the tested two-point candidate.

## Reproduce the physical acquisition

Install `pyserial` and use a supported PlatformIO Python. Explicitly identify the intended test board before selecting its port; port names are machine-specific.

```text
python scripts/capture_bias_hardware.py --port COM15
python scripts/benchmark_bias_variants_hardware.py --port COM15 --pio-python C:/.platformio/penv/Scripts/python.exe
```

The first command only captures an already identified/flashed image. The second flashes the six isolated builds and restores production in its finalization path. Missing measurement fields are errors, never substituted zeros. Raw logs accompany every numerical result. No physical results are generated by CI runners or inferred from Native performance.
