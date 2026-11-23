# Phase 7.4C — C ABI & DSP Parameter Contract

`HYDRA_DSP_API_VERSION` is bumped to `1.8.0` (`HYDRA_DSP_PARAM_COUNT = 47`) in [include/hydra_dsp.h](../../include/hydra_dsp.h). All existing parameter IDs `0..37` and legacy structs (`v1_2`, `v1_3`, `v1_4`) remain unchanged.

## Added Parameter IDs

| ID | Enum | Name | Range | Default | Smoothing | Mode Scope |
|---|---|---|---|---|---|---|
| 38 | `HYDRA_DSP_PARAM_MOTION_MODE` | `motionMode` | `0..3` | `0` (Classic) | `NONE` | All |
| 39 | `HYDRA_DSP_PARAM_MOTION_AMOUNT` | `motionAmount` | `0..100` | `100` | `BLOCK_EDGE` | All |
| 40 | `HYDRA_DSP_PARAM_STEREO_MOTION` | `stereoMotion` | `0..100` | `100` | `BLOCK_EDGE` | All |
| 41 | `HYDRA_DSP_PARAM_MOTION_INERTIA` | `motionInertia` | `0..100` | `50` | `BLOCK_EDGE` | M1 (Motor Drift) |
| 42 | `HYDRA_DSP_PARAM_MOTION_DRIFT_CHAR` | `motionDriftChar` | `0..100` | `50` | `BLOCK_EDGE` | M1 (Motor Drift) |
| 43 | `HYDRA_DSP_PARAM_MOTION_TEXTURE` | `motionTexture` | `0..100` | `50` | `BLOCK_EDGE` | M2 (Capstan Flutter) |
| 44 | `HYDRA_DSP_PARAM_MOTION_IRREGULARITY` | `motionIrregularity` | `0..100` | `50` | `BLOCK_EDGE` | M2 (Capstan Flutter) |
| 45 | `HYDRA_DSP_PARAM_MOTION_INSTABILITY` | `motionInstability` | `0..100` | `50` | `BLOCK_EDGE` | M3 (Worn Transport) |
| 46 | `HYDRA_DSP_PARAM_MOTION_TENSION_REC` | `motionTensionRec` | `0..100` | `50` | `BLOCK_EDGE` | M3 (Worn Transport) |

## Zero-Depth & Nominal Parity Contract

- `Motion Amount = 100`, `Stereo Motion = 100`, and advanced controls at `50` reproduce the approved Phase 7.4B M1/M2/M3 behavior and exact Classic C M0 behavior.
- `Motion Amount = 0` scales mechanical modulation displacement to `0.0` (neutral transport).
- `Stereo Motion = 0` collapses L/R differential modulation to zero, leaving shared mono transport modulation.

## Telemetry API

- `hydra_dsp_get_motion_telemetry(const hydra_dsp_handle* handle, float* out_mod_l, float* out_mod_r)` exposes the current L/R read-head displacement in samples.
