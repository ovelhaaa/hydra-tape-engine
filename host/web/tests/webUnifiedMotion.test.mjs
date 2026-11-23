import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import {
  DEFAULT_CONTROL_STATE,
  deserializePresetFromText,
  serializePreset
} from '../presetSerialization.js';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const WEB_DIR = path.resolve(__dirname, '..');
const WEB_BUILD_DIR = process.env.HYDRA_WEB_BUILD ? path.resolve(process.env.HYDRA_WEB_BUILD) : null;

async function createWorklet() {
  const processorUrl = pathToFileURL(path.join(WEB_BUILD_DIR, 'hydra-processor.js')).href;
  const wasmBinary = await fs.readFile(path.join(WEB_BUILD_DIR, 'hydra_dsp.wasm'));

  let ProcessorClass;
  const messages = [];
  globalThis.sampleRate = 48000;
  globalThis.currentFrame = 0;
  globalThis.AudioWorkletProcessor = class {
    constructor() {
      this.port = {
        onmessage: null,
        postMessage: (msg) => messages.push(msg)
      };
    }
  };
  globalThis.registerProcessor = (_name, cls) => {
    ProcessorClass = cls;
  };

  await import(`${processorUrl}?t=${Date.now()}-${Math.random()}`);
  const node = new ProcessorClass({ processorOptions: { wasmBinary: wasmBinary.buffer } });
  const deadline = Date.now() + 5000;
  while (!node.ready && Date.now() < deadline) {
    await new Promise((r) => setTimeout(r, 10));
  }
  assert.equal(node.ready, true, 'AudioWorklet should become ready');
  assert.deepStrictEqual(messages[0], { type: 'ready' });
  return { node, messages };
}

function buildDefaultParamMap(overrides = {}) {
  const map = {
    p_0: [14],
    p_1: [10],
    p_2: [0],
    p_3: [40],
    p_4: [0],
    p_5: [50],
    p_6: [60],
    p_7: [30],
    p_8: [0],
    p_9: [6.0],
    p_10: [0.8],
    p_12: [120],
    p_13: [40],
    p_14: [100],
    p_15: [100],
    p_17: [120],
    p_20: [50],
    p_26: [60],
    p_27: [45],
    p_28: [50],
    p_31: [1.0],
    p_32: [80],
    p_33: [0.5],
    p_34: [0],
    p_39: [100],
    p_40: [100],
    p_41: [50],
    p_42: [50],
    p_43: [50],
    p_44: [50],
    p_45: [50],
    p_46: [50]
  };
  for (const [k, v] of Object.entries(overrides)) {
    map[k] = [v];
  }
  return map;
}

function rmsDiff(a, b, start = 0) {
  let sum = 0;
  let count = 0;
  for (let i = start; i < a.length && i < b.length; i++) {
    const d = a[i] - b[i];
    sum += d * d;
    count++;
  }
  return count > 0 ? Math.sqrt(sum / count) : 0;
}

async function renderModeAudio(mode, overrides = {}, blocks = 240) {
  const { node } = await createWorklet();
  node.port.onmessage({ data: { type: 'command', command: 'delayActive', value: 1 } });
  node.port.onmessage({ data: { type: 'command', command: 'motionMode', value: mode } });

  const params = buildDefaultParamMap({ p_1: 22, p_0: 26, ...overrides });
  // Settle 4ms smoothing
  const inL = new Float32Array(128);
  const inR = new Float32Array(128);
  const outL = new Float32Array(128);
  const outR = new Float32Array(128);
  const fullL = new Float32Array(blocks * 128);
  const fullR = new Float32Array(blocks * 128);

  let sampleIdx = 0;
  for (let b = 0; b < blocks; b++) {
    for (let i = 0; i < 128; i++) {
      const s = 0.35 * Math.sin((2 * Math.PI * 440 * sampleIdx) / 48000);
      inL[i] = s;
      inR[i] = s;
      sampleIdx++;
    }
    globalThis.currentFrame = b * 128;
    node.process([[inL, inR]], [[outL, outR]], params);
    fullL.set(outL, b * 128);
    fullR.set(outR, b * 128);
  }
  node.cleanup();
  return { fullL, fullR };
}

test('presetSerialization stores and migrates Phase 7.4C Tape Motion parameters', () => {
  const custom = {
    ...DEFAULT_CONTROL_STATE,
    motionMode: 3,
    motionAmount: 72,
    stereoMotion: 45,
    motionInertia: 80,
    motionDriftChar: 30,
    motionTexture: 65,
    motionIrregularity: 20,
    motionInstability: 85,
    motionTensionRec: 40
  };
  const json = serializePreset(custom, { name: 'Worn Guitar' });
  const imported = deserializePresetFromText(json);
  assert.equal(imported.controlState.motionMode, 3);
  assert.equal(imported.controlState.motionAmount, 72);
  assert.equal(imported.controlState.stereoMotion, 45);
  assert.equal(imported.controlState.motionInstability, 85);
  assert.equal(imported.controlState.motionTensionRec, 40);

  // V1 preset migrates to Classic (0)
  const v1 = JSON.stringify({
    version: 1,
    name: 'Legacy V1',
    engineParams: { drive: 55, flutterDepth: 12, wowDepth: 8 },
    fxChain: [{ type: 'tapeDelay', enabled: true, params: { delayTimeMs: 320, feedback: 45, dryWet: 50 } }]
  });
  const importedV1 = deserializePresetFromText(v1);
  assert.equal(importedV1.controlState.motionMode, 0);
  assert.equal(importedV1.controlState.motionAmount, 100);
  assert.equal(importedV1.controlState.drive, 55);
});

test('WASM AudioWorklet supports all 4 motion modes, clickless switching, and motion controls', {
  skip: !WEB_BUILD_DIR ? 'Set HYDRA_WEB_BUILD to the generated web runtime directory' : false
}, async () => {
  const m0 = await renderModeAudio(0);
  const m1 = await renderModeAudio(1);
  const m2 = await renderModeAudio(2);
  const m3 = await renderModeAudio(3);

  const wetStart = 12000;
  assert.ok(rmsDiff(m0.fullL, m1.fullL, wetStart) > 1e-3, 'M0 and M1 must differ');
  assert.ok(rmsDiff(m0.fullL, m2.fullL, wetStart) > 1e-3, 'M0 and M2 must differ');
  assert.ok(rmsDiff(m1.fullL, m2.fullL, wetStart) > 1e-3, 'M1 and M2 must differ');
  assert.ok(rmsDiff(m2.fullL, m3.fullL, wetStart) > 1e-3, 'M2 and M3 must differ');

  // Stereo Motion = 0 -> L == R in M2 (with feedback=0 so initial 4ms smoothing transient doesn't echo)
  const monoM2 = await renderModeAudio(2, { p_40: 0, p_13: 0 });
  assert.ok(rmsDiff(monoM2.fullL, monoM2.fullR, wetStart) < 1e-5, 'Stereo Motion=0 must link L/R');

  // Clickless switching under high feedback
  const { node, messages } = await createWorklet();
  node.port.onmessage({ data: { type: 'command', command: 'delayActive', value: 1 } });
  const params = buildDefaultParamMap({ p_13: 85, p_1: 24, p_0: 28 });
  const inL = new Float32Array(128);
  const inR = new Float32Array(128);
  const outL = new Float32Array(128);
  const outR = new Float32Array(128);
  let prevL = 0;
  let maxJump = 0;
  let sampleIdx = 0;

  for (let b = 0; b < 300; b++) {
    if (b === 100) node.port.onmessage({ data: { type: 'command', command: 'motionMode', value: 1 } });
    if (b === 140) node.port.onmessage({ data: { type: 'command', command: 'motionMode', value: 2 } });
    if (b === 180) node.port.onmessage({ data: { type: 'command', command: 'motionMode', value: 3 } });
    if (b === 220) node.port.onmessage({ data: { type: 'command', command: 'motionMode', value: 0 } });

    for (let i = 0; i < 128; i++) {
      const s = b < 110 ? 0.35 * Math.sin((2 * Math.PI * 220 * sampleIdx) / 48000) : 0;
      inL[i] = s;
      inR[i] = s;
      sampleIdx++;
    }
    globalThis.currentFrame = b * 128;
    node.process([[inL, inR]], [[outL, outR]], params);

    for (let i = 0; i < 128; i++) {
      assert.ok(Number.isFinite(outL[i]) && Number.isFinite(outR[i]));
      if (b > 2) {
        maxJump = Math.max(maxJump, Math.abs(outL[i] - prevL));
      }
      prevL = outL[i];
    }
  }
  assert.ok(maxJump < 0.25, `Expected clickless transitions, got maxJump=${maxJump}`);
  const telemetryMsgs = messages.filter((m) => m.type === 'transportTelemetry' && Number.isFinite(m.motionModL));
  assert.ok(telemetryMsgs.length > 0, 'Worklet should emit real motionModL/R telemetry');
  node.cleanup();
});
