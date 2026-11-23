import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

const root = resolve(import.meta.dirname, '..');

const VARIANTS = [
  { key: 'm0', buildDir: resolve(root, 'build/web-m0/web'), expectedMode: 'M0_CLASSIC_C_BASELINE' },
  { key: 'm1', buildDir: resolve(root, 'build/web-m1/web'), expectedMode: 'M1_MOTOR_DRIFT' },
  { key: 'm2', buildDir: resolve(root, 'build/web-m2/web'), expectedMode: 'M2_CAPSTAN_FLUTTER' },
  { key: 'm3', buildDir: resolve(root, 'build/web-m3/web'), expectedMode: 'M3_UNSTABLE_TRANSPORT' }
];

const SAMPLE_RATE = 48000;
const BLOCK = 128;

const PARAM = {
  FLUTTER_DEPTH: 0,
  WOW_DEPTH: 1,
  DROPOUT_SEV: 2,
  DRIVE: 3,
  NOISE: 4,
  TAPE_SPEED: 5,
  TAPE_AGE: 6,
  WOW_RATE: 10,
  FLUTTER_RATE: 9,
  DELAY_ACTIVE: 11,
  DELAY_TIME_MS: 12,
  FEEDBACK: 13,
  DRY_WET: 14,
  DELAY_WET: 15,
  ACTIVE_HEADS: 16,
  BPM: 17
};

async function loadVariant(variant) {
  const buildInfo = JSON.parse(await readFile(resolve(variant.buildDir, 'hydra-build-info.json'), 'utf8'));
  assert.equal(buildInfo.motionMode, variant.expectedMode, `Unexpected motionMode for ${variant.key}`);
  const modFactory = (await import(pathToFileURL(resolve(variant.buildDir, 'hydra_dsp.js')).href)).default;
  const wasmBinary = await readFile(resolve(variant.buildDir, 'hydra_dsp.wasm'));
  return modFactory({ wasmBinary });
}

function createHandle(Module, sampleRate = SAMPLE_RATE, maxDelayMs = 1600.0) {
  const outHandlePtr = Module._malloc(4);
  assert.ok(outHandlePtr !== 0, 'malloc(4) for outHandlePtr must succeed');
  try {
    Module.HEAP32[outHandlePtr >> 2] = 0;
    const status = Module._hydra_dsp_create(sampleRate, maxDelayMs, outHandlePtr);
    const handle = Module.HEAP32[outHandlePtr >> 2] >>> 0;
    return { status, handle };
  } finally {
    Module._free(outHandlePtr);
  }
}

function testInvalidRejections(Module) {
  // Null out_handle
  assert.equal(Module._hydra_dsp_create(SAMPLE_RATE, 1600.0, 0), -1, 'create(null out_handle) must return -1');

  // Invalid max_delay_ms
  const bad1 = createHandle(Module, SAMPLE_RATE, 0.0);
  assert.ok(bad1.status < 0 && bad1.handle === 0, 'create(max_delay_ms=0) must fail without handle');

  const bad2 = createHandle(Module, SAMPLE_RATE, -50.0);
  assert.ok(bad2.status < 0 && bad2.handle === 0, 'create(max_delay_ms=-50) must fail without handle');

  // Null handle rejections
  assert.equal(Module._hydra_dsp_prepare(0, SAMPLE_RATE, 2), -1, 'prepare(null) must return -1');
  assert.equal(Module._hydra_dsp_set_parameter(0, PARAM.WOW_DEPTH, 10), -1, 'set_parameter(null) must return -1');
  assert.equal(Module._hydra_dsp_commit(0), -1, 'commit(null) must return -1');
  assert.equal(Module._hydra_dsp_process(0, 0, 0, 0, 0, BLOCK), -1, 'process(null) must return -1');

  // Valid handle with invalid args
  const { status, handle } = createHandle(Module, SAMPLE_RATE, 1600.0);
  assert.equal(status, 0);
  assert.ok(handle !== 0);
  try {
    assert.equal(Module._hydra_dsp_prepare(handle, SAMPLE_RATE, 1), -2, 'prepare(channels=1) must return -2');
    assert.equal(Module._hydra_dsp_prepare(handle, SAMPLE_RATE, BLOCK), -2, 'prepare(channels=BLOCK) must return -2');
    assert.equal(Module._hydra_dsp_prepare(handle, SAMPLE_RATE, 2), 0, 'prepare(channels=2) must return 0');
    assert.equal(Module._hydra_dsp_set_parameter(handle, 9999, 1.0), -2, 'invalid param_id must return -2');
    assert.equal(Module._hydra_dsp_set_parameter(handle, PARAM.BPM, -5.0), -1, 'invalid BPM must return -1');

    const buf = Module._malloc(BLOCK * 4);
    try {
      assert.equal(Module._hydra_dsp_process(handle, 0, buf, buf, buf, BLOCK), -1, 'process(null in_l) must return -1');
      assert.equal(Module._hydra_dsp_process(handle, buf, buf, 0, buf, BLOCK), -1, 'process(null out_l) must return -1');
    } finally {
      Module._free(buf);
    }
  } finally {
    Module._hydra_dsp_destroy(handle);
  }
}

function runLifecycleCycle(Module, variantKey, cycleIndex) {
  let handle = 0;
  let inLPtr = 0;
  let inRPtr = 0;
  let outLPtr = 0;
  let outRPtr = 0;

  try {
    const created = createHandle(Module, SAMPLE_RATE, 1600.0);
    assert.equal(created.status, 0, `${variantKey} cycle ${cycleIndex}: hydra_dsp_create failed (${created.status})`);
    assert.ok(created.handle !== 0, `${variantKey} cycle ${cycleIndex}: null handle`);
    handle = created.handle;

    assert.equal(Module._hydra_dsp_prepare(handle, SAMPLE_RATE, 2), 0, `${variantKey}: prepare failed`);

    const params = [
      [PARAM.DELAY_ACTIVE, 1],
      [PARAM.DELAY_TIME_MS, 250],
      [PARAM.FEEDBACK, 70],
      [PARAM.DELAY_WET, 65],
      [PARAM.DRY_WET, 50],
      [PARAM.WOW_RATE, 0.8],
      [PARAM.WOW_DEPTH, 25],
      [PARAM.FLUTTER_RATE, 6.0],
      [PARAM.FLUTTER_DEPTH, 20],
      [PARAM.TAPE_AGE, 40],
      [PARAM.ACTIVE_HEADS, 4],
      [PARAM.NOISE, 0],
      [PARAM.DROPOUT_SEV, 0]
    ];
    for (const [id, val] of params) {
      assert.equal(Module._hydra_dsp_set_parameter(handle, id, val), 0, `${variantKey}: set_parameter(${id}) failed`);
    }
    assert.equal(Module._hydra_dsp_commit(handle), 0, `${variantKey}: commit failed`);

    inLPtr = Module._malloc(BLOCK * 4);
    inRPtr = Module._malloc(BLOCK * 4);
    outLPtr = Module._malloc(BLOCK * 4);
    outRPtr = Module._malloc(BLOCK * 4);
    assert.ok(inLPtr && inRPtr && outLPtr && outRPtr, `${variantKey}: buffer malloc failed`);

    let energy = 0;
    for (let b = 0; b < 24; b++) {
      const inL = Module.HEAPF32.subarray(inLPtr >> 2, (inLPtr >> 2) + BLOCK);
      const inR = Module.HEAPF32.subarray(inRPtr >> 2, (inRPtr >> 2) + BLOCK);
      for (let i = 0; i < BLOCK; i++) {
        const t = (b * BLOCK + i) / SAMPLE_RATE;
        const s = 0.4 * Math.sin(2 * Math.PI * 440.0 * t);
        inL[i] = s;
        inR[i] = s;
      }
      assert.equal(
        Module._hydra_dsp_process(handle, inLPtr, inRPtr, outLPtr, outRPtr, BLOCK),
        0,
        `${variantKey}: process failed before reset`
      );
      const outL = Module.HEAPF32.subarray(outLPtr >> 2, (outLPtr >> 2) + BLOCK);
      const outR = Module.HEAPF32.subarray(outRPtr >> 2, (outRPtr >> 2) + BLOCK);
      for (let i = 0; i < BLOCK; i++) {
        assert.ok(Number.isFinite(outL[i]) && Number.isFinite(outR[i]), `${variantKey}: non-finite output`);
        energy += outL[i] * outL[i] + outR[i] * outR[i];
      }
    }
    assert.ok(energy > 1e-3, `${variantKey}: expected audible energy before reset, got ${energy}`);

    // Reset and verify no spurious audio on silence
    Module._hydra_dsp_reset(handle);
    Module.HEAPF32.subarray(inLPtr >> 2, (inLPtr >> 2) + BLOCK).fill(0);
    Module.HEAPF32.subarray(inRPtr >> 2, (inRPtr >> 2) + BLOCK).fill(0);

    let maxPostReset = 0;
    for (let b = 0; b < 8; b++) {
      assert.equal(
        Module._hydra_dsp_process(handle, inLPtr, inRPtr, outLPtr, outRPtr, BLOCK),
        0,
        `${variantKey}: process failed after reset`
      );
      const outL = Module.HEAPF32.subarray(outLPtr >> 2, (outLPtr >> 2) + BLOCK);
      const outR = Module.HEAPF32.subarray(outRPtr >> 2, (outRPtr >> 2) + BLOCK);
      for (let i = 0; i < BLOCK; i++) {
        assert.ok(Number.isFinite(outL[i]) && Number.isFinite(outR[i]), `${variantKey}: non-finite after reset`);
        maxPostReset = Math.max(maxPostReset, Math.abs(outL[i]), Math.abs(outR[i]));
      }
    }
    assert.ok(maxPostReset < 1e-6, `${variantKey}: spurious audio after reset (maxAbs=${maxPostReset})`);
  } finally {
    if (inLPtr) Module._free(inLPtr);
    if (inRPtr) Module._free(inRPtr);
    if (outLPtr) Module._free(outLPtr);
    if (outRPtr) Module._free(outRPtr);
    if (handle) {
      Module._hydra_dsp_destroy(handle);
      handle = 0;
    }
  }
}

function testMultipleSimultaneousInstances(Module, variantKey) {
  const h1 = createHandle(Module, SAMPLE_RATE, 1600.0);
  const h2 = createHandle(Module, 44100, 1600.0);
  assert.equal(h1.status, 0);
  assert.equal(h2.status, 0);
  assert.ok(h1.handle !== 0 && h2.handle !== 0 && h1.handle !== h2.handle, `${variantKey}: distinct handles expected`);
  try {
    assert.equal(Module._hydra_dsp_prepare(h1.handle, SAMPLE_RATE, 2), 0);
    assert.equal(Module._hydra_dsp_prepare(h2.handle, 44100, 2), 0);
  } finally {
    Module._hydra_dsp_destroy(h2.handle);
    Module._hydra_dsp_destroy(h1.handle);
  }
}

async function main() {
  for (const v of VARIANTS) {
    const Module = await loadVariant(v);
    testInvalidRejections(Module);
    for (let i = 0; i < 4; i++) {
      runLifecycleCycle(Module, v.key, i);
    }
    testMultipleSimultaneousInstances(Module, v.key);
    console.log(`PASS WASM API contract: ${v.key} (${v.expectedMode})`);
  }
}

main().catch((err) => {
  console.error('WASM API contract test FAILED:', err);
  process.exit(1);
});
