# Phase 7.4A.1 — ESP32-S3 Real-Time Feasibility Estimate

**Physical Hardware Status:** `ESP32_PHYSICAL_NOT_EXECUTED` (no physical ESP32-S3 board measurement in this session; physical feasibility remains **pending** on-device profiling).

## Physical Budget (Reference Baseline)

- **Sample Rate:** 44,100 Hz
- **Block Size:** 128 frames
- **Available Budget per Block:** `2,902.49 µs`
- **Observed Classic C (C0) Peak (Multi-Head):** `2,604 µs`
- **Remaining Margin:** `~298.49 µs` per 128-frame block (`~2.33 µs/frame` @ 240 MHz Xtensa LX7)

---

## Parallel Dry/Wet Architecture Cost Note

With the Phase 7.4A.1 parallel dry/wet refinement (`x + amount * (wet - x)`):
- **Zero dynamic allocations, no locks, no I/O, no per-sample `exp()`/`sin()`/`cos()`/`pow()`/`tan()`, and no unnecessary `double` math.**
- **Continuous State Tracking Cost:** Even when `amount == 0` or `mod == 0` (audible effect is zero), the envelope detectors and internal filter states (`wearLPF`, `softLPF`, `smearAP`) **continue updating every sample** to guarantee click-free, phase-coherent activation. Therefore, the per-sample CPU cost is **constant** (branchless) and **never drops to zero** during quiet or sustained passages.
- **Extra ops vs. Phase 7.4A initial:** +2 `sub`/`fma` per channel per active stage for `x + amount * (wet - x)` (~+2–4 µs per 128-frame block).

| Variant | Per-Frame Ops (Stereo, L+R) | Estimated Block Cost @ 240 MHz | Remaining Margin (vs 2,604 µs) | Firmware Promotion? |
|---|---|---|---|---|
| **C0** | 0 extra | `0 µs` | `298.5 µs` | Production default (PR #58) |
| **C1** | ~28 FMA/add/cmp + 2 `raFastRecip` | `~20–38 µs` (est.) | `~260–278 µs` (est.) | **Not promoted** (pending human listening + HW profile) |
| **C2** | ~36 FMA/add/cmp + 4 `raFastRecip` | `~27–48 µs` (est.) | `~250–271 µs` (est.) | **Not promoted** (pending human listening + HW profile) |
| **C3** | ~64 FMA/add/cmp + 6 `raFastRecip` | `~47–86 µs` (est.) | `~212–251 µs` (est.) | **Not promoted** (tighter margin; requires HW profile) |

---

## Gate Before Hardware Deployment

1. Do **not** enable `HYDRA_PHASE74_REPEAT_AGING_MODE > 0` in `platformio.ini` production firmware.
2. Operation-count estimates are **not** proof of real-time safety on hardware. On-device ESP32-S3 profiling (`esp32-s3-classic-c-dltrace` / `esp32-s3-stageprofile`) in Multi-Head (`activeHeads=7`) + Reverse + Spring worst-case is mandatory before any promotion.
