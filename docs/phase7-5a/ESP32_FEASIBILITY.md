# ESP32 feasibility

`esp32-s3-phase75a1-studio` extends the existing Unified Motion / Classic C
environment. Local compile PASS with PlatformIO's supported bundled Python:

```
C:\.platformio\penv\Scripts\python.exe -m platformio run -e esp32-s3-phase75a1-studio
```

RAM static: 27,152 / 327,680 bytes (8.3%). Flash: 650,355 / 16,777,216 (3.9%).
Local framework Arduino 3.3.6, Xtensa toolchain 14.2.0. No upload/flash command
was run. The machine's default Python 3.14 is unsupported by its PlatformIO
installation; use the bundled environment or supported Python 3.11-3.13.

Incremental DSP memory consists of two channels of 3 biquads, one one-pole and
one allpass plus two smoothing floats. There is no second tape buffer or added
Spring/Smear buffer, no hot-path allocation and no oversampling. Both playback
chains run to keep transition state warm; extra filtering is measured, not free.

Native C++ benchmark example at 48 kHz/128 frames: Classic 0.0838 ms/block,
Studio 0.0883 ms/block (~5.3% incremental in that run), against 2.6667 ms budget.
WASM local examples across M0..M3 were ~0.10-0.14 ms/block, with noisy P99s below
0.33 ms in the captured run. metrics.json and wasm-validation.json retain the
raw benchmark runs, including baseline Classic; ctypes timings include bridge
cost and host scheduling. Do not use noisy desktop ratios as ESP32 cycle counts.

These measurements demonstrate low desktop incremental cost, not a new physical
ESP32 deadline measurement. A sustained physical CPU/heap/audio benchmark remains
for a separately authorized hardware phase; this task explicitly forbids flash.
The existing 7.4C hardware results cannot prove the new firmware's runtime budget.
Keep the experiment in draft until human audition and that feasibility review.
