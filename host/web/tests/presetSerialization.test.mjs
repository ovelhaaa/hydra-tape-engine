import test from 'node:test';
import assert from 'node:assert/strict';

import {
  DEFAULT_CONTROL_STATE,
  deserializePresetFromText,
  serializePreset
} from '../presetSerialization.js';

test('preset serialization roundtrip (export → import)', () => {
  const exported = serializePreset({
    delayActive: 0,
    delayTimeMs: 777,
    feedback: 65,
    dryWet: 35,
    drive: 55,
    flutterDepth: 12,
    wowDepth: 18
  }, { name: 'Roundtrip Preset' });

  const imported = deserializePresetFromText(exported);
  assert.equal(imported.migratedFromVersion, null);
  assert.deepEqual(imported.controlState, {
    delayActive: 0,
    delayTimeMs: 777,
    feedback: 65,
    dryWet: 35,
    drive: 55,
    flutterDepth: 12,
    wowDepth: 18
  });
});

test('invalid preset file returns clear validation error', () => {
  assert.throws(
    () => deserializePresetFromText('{"version":2,'),
    /JSON malformado/
  );
});

test('supports v1 preset migration (v1 -> v2)', () => {
  const v1Preset = JSON.stringify({
    version: 1,
    name: 'Legacy',
    engineParams: {
      delayActive: 1,
      delayTimeMs: 450,
      feedback: 41,
      dryWet: 56,
      drive: 39,
      flutterDepth: 9,
      wowDepth: 11
    },
    metadata: { author: 'legacy-user' }
  });

  const imported = deserializePresetFromText(v1Preset);
  assert.equal(imported.migratedFromVersion, 1);
  assert.equal(imported.preset.version, 2);
  assert.equal(imported.preset.fxChain[0].type, 'tapeDelay');
  assert.equal(imported.controlState.delayTimeMs, 450);
  assert.equal(imported.controlState.delayActive, 1);
});

test('partial preset uses defaults and clamps out-of-range values', () => {
  const partial = JSON.stringify({
    version: 2,
    name: 'Partial',
    engineParams: {
      drive: 999
    },
    fxChain: [
      {
        type: 'tapeDelay',
        enabled: true,
        params: {
          delayTimeMs: -500
        }
      }
    ],
    metadata: {}
  });

  const imported = deserializePresetFromText(partial);
  assert.equal(imported.controlState.drive, 100);
  assert.equal(imported.controlState.delayTimeMs, 10);
  assert.equal(imported.controlState.feedback, DEFAULT_CONTROL_STATE.feedback);
  assert.equal(imported.controlState.dryWet, DEFAULT_CONTROL_STATE.dryWet);
  assert.equal(imported.controlState.wowDepth, DEFAULT_CONTROL_STATE.wowDepth);
});
