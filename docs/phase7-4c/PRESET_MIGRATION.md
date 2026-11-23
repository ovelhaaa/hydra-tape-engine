# Phase 7.4C — Preset Serialization & Migration

## Schema

`CURRENT_PRESET_VERSION` remains `2` in [host/web/presetSerialization.js](../../host/web/presetSerialization.js), extended additively with the 9 Tape Motion fields:

- `motionMode` (`0..3`, integer, default `0`)
- `motionAmount` (`0..100`, default `100`)
- `stereoMotion` (`0..100`, default `100`)
- `motionInertia`, `motionDriftChar`, `motionTexture`, `motionIrregularity`, `motionInstability`, `motionTensionRec` (`0..100`, default `50`)

## Backward Compatibility

- **V1 Presets**: `migrateV1ToV2()` populates `DEFAULT_CONTROL_STATE`, setting `motionMode: 0` (Classic) and `motionAmount: 100` while preserving all original V1 parameters.
- **V2 Presets without `motionMode`**: `applyControlStateToUI()` in [host/web/main.js](../../host/web/main.js) defaults `motionMode` to `0` (`DEFAULT_CONTROL_STATE.motionMode`) without altering existing parameters.
- **Roundtrip**: Tested in `host/web/tests/webUnifiedMotion.test.mjs` and `scripts/test_phase74c_browser.mjs`.
