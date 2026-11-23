# Phase 7.4C — Web Interface & AudioWorklet Integration

## UI Structure (`host/web/index.html`, `host/web/main.js`)

Card 2 (`#tapeMotionCard`) integrates the unified Tape Motion controls without altering the rest of Hydra's layout:

- **4-Mode Selector** (`role="radiogroup"`): `CLASSIC` (0), `MOTOR` (1), `CAPSTAN` (2), `WORN` (3), supporting mouse click and `ArrowLeft`/`ArrowRight`/`Home`/`End` keyboard navigation.
- **Mode Character & DSP Sync Status** (`#motionModeDesc`, `#motionModeStatus`): Distinguishes requested mode from DSP-confirmed mode (`hydra_dsp_get_parameter` readback via `stateAck`).
- **Common Motion Controls**: `Motion Amount` (`#motionAmount`) and `Stereo Motion` (`#stereoMotion`), plus existing `Wow Depth/Rate` and `Flutter Depth/Rate`.
- **Collapsible Advanced Section** (`#motionAdvancedSection`): Displays only the two advanced controls relevant to the active mode (`.motion-advanced-panel[hidden] { display: none !important; }` prevents `.grid-2col` from overriding `[hidden]`) while preserving all other modes' values in the DOM/state.
- **Dual-Channel Real DSP Telemetry Indicator** (`#motionTelemetryMeter`): Displays actual left (`#motionTelemetryThumbL`, `#motionTelemetryValueL`) and right (`#motionTelemetryThumbR`, `#motionTelemetryValueR`) read-head displacement in samples from `hydra_dsp_get_motion_telemetry()`, normalized to `±24.0 samples` (`clamp[-1, 1]`) so stereo differential motion is immediately visible without visual clipping.
- **Dynamic Parameter Count** (`#dspParamCountSummary`): Computed dynamically from `PARAM` (`47 Parameters Mapped`).

## AudioWorklet (`host/web/hydra-processor.js`)

- Single unified WASM module (`hydra_dsp.wasm`) supporting all four modes.
- `motionMode` (ID 38) is dispatched as a discrete `command` via `MessagePort` (never as an interpolated `AudioParam`).
- Continuous parameters `p_39..p_46` use `AudioParam` descriptors with 4 ms block-edge smoothing.
- `initWasm()` validates `hydra_dsp_get_param_count() >= 47`.
- Offline WAV export (`#offlineBtn`) synchronizes all parameters including `motionMode` via `syncAllParams(offlineNode, true)`.

## Browser Verification

Validated in headless Chromium via [scripts/test_phase74c_browser.mjs](../../scripts/test_phase74c_browser.mjs):
- Zero console/page errors.
- Live mode switching during playback with `feedback = 85%` without audio context restart.
- Offline WAV export in Worn Transport mode.
- Preset import/export and 390px mobile responsive layout.
