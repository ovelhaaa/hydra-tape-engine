# Phase 7.4A — ESP32-S3 Real-Time Feasibility Estimate

## Physical Budget

- **Sample Rate:** 44,100 Hz
- **Block Size:** 128 frames
- **Available Budget per Block:** `2,902.49 µs`
- **Observed Classic C (C0) Peak (Multi-Head):** `2,604 µs`
- **Remaining Margin:** `~298.49 µs` per 128-frame block (`~2.33 µs/frame` @ 240 MHz Xtensa LX7)

---

## Estimated Additional Cost per Variant (Stereo, 128 frames)

Both C1 and C2 were designed with **zero per-sample `exp()`, `sin()`, `cos()`, `pow()`, or `tan()`** calls:
- All envelope attack/release alphas are precomputed in `updateSampleRateConstants()`.
- Dynamic filter cutoffs use `OnePoleLP::setCutoffFastInv()` (`x / (1 + x)` via `fastRecip`, which uses hardware `recip0.s` + 2 Newton-Raphson steps on Xtensa).
- No dynamic allocations, no locks, no I/O, and no double-precision math in the audio path.

| Variant | Per-Frame Ops (Stereo, L+R) | Estimated Block Cost @ 240 MHz | Remaining Margin (vs 2,604 µs) | Firmware Candidate? |
|---|---|---|---|---|
| **C0** | 0 extra | `0 µs` | `298.5 µs` | Production default |
| **C1** | ~24 FMA/add/cmp + 2 `fastRecip` | `~18–35 µs` | `~263–280 µs` | **Yes** (subject to device profiling) |
| **C2** | ~32 FMA/add/cmp + 4 `fastRecip` | `~25–45 µs` | `~253–273 µs` | **Yes** (subject to device profiling) |
| **C3** | ~56 FMA/add/cmp + 6 `fastRecip` | `~43–80 µs` | `~218–255 µs` | **Conditional** (tighter margin; measure on-target) |

---

## Risks & Gate Before Hardware Deployment

1. **PSRAM / Cache Pressure:** Adding `RepeatAgingChannelState` (2 × ~48 bytes = ~96 bytes) fits easily in internal DRAM (`TapeCore::Impl`), so PSRAM cache misses are not expected, but must be verified with `esp32-s3-classic-c-dltrace` / `esp32-s3-stageprofile`.
2. **WASM Pass ≠ ESP32 Proof:** WASM/Native execution only validates functional correctness and numerical stability. **No Phase 7.4A variant should be enabled in `platformio.ini` production firmware** until:
   - Human listening approves the musicality of C1/C2/C3.
   - On-device ESP32-S3 block timing confirms zero deadline overruns in Multi-Head + Reverse + Spring worst-case scenarios.
