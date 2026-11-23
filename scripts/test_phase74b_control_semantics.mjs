import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { analyzeModulation } from './phase74b_demodulator.mjs';

const root = resolve(import.meta.dirname, '..');
const SAMPLE_RATE = 48000;
const BLOCK = 128;

const VARIANTS = [
  { key: 'm0', buildDir: resolve(root, 'build/web-m0/web') },
  { key: 'm1', buildDir: resolve(root, 'build/web-m1/web') },
  { key: 'm2', buildDir: resolve(root, 'build/web-m2/web') },
  { key: 'm3', buildDir: resolve(root, 'build/web-m3/web') }
];

const PARAM = {
  FLUTTER_DEPTH: 0,
  WOW_DEPTH: 1,
  DROPOUT_SEV: 2,
  DRIVE: 3,
  NOISE: 4,
  TAPE_AGE: 6,
  HEAD_BUMP: 7,
  AZIMUTH: 8,
  FLUTTER_RATE: 9,
  WOW_RATE: 10,
  DELAY_ACTIVE: 11,
  DELAY_TIME_MS: 12,
  FEEDBACK: 13,
  DRY_WET: 14,
  DELAY_WET: 15,
  ACTIVE_HEADS: 16,
  TONE: 20
};

async function loadVariant(v) {
  const modFactory = (await import(pathToFileURL(resolve(v.buildDir, 'hydra_dsp.js')).href)).default;
  const wasmBinary = await readFile(resolve(v.buildDir, 'hydra_dsp.wasm'));
  return modFactory({ wasmBinary });
}

function probe(Module, { wowDepth, flutterDepth, wowRate = 0.8, flutterRate = 6.0, tapeAge = 40 }) {
  const durationSec = 6.0;
  const frames = Math.floor(durationSec * SAMPLE_RATE);
  const outHandlePtr = Module._malloc(4);
  let handle = 0;
  let inLPtr = 0, inRPtr = 0, outLPtr = 0, outRPtr = 0;
  try {
    Module.HEAP32[outHandlePtr >> 2] = 0;
    assert.equal(Module._hydra_dsp_create(SAMPLE_RATE, 1600.0, outHandlePtr), 0);
    handle = Module.HEAP32[outHandlePtr >> 2] >>> 0;
    Module._free(outHandlePtr);

    assert.equal(Module._hydra_dsp_prepare(handle, SAMPLE_RATE, 2), 0);
    const setP = (id, val) => assert.equal(Module._hydra_dsp_set_parameter(handle, id, val), 0);
    setP(PARAM.DELAY_ACTIVE, 1);
    setP(PARAM.DELAY_TIME_MS, 300);
    setP(PARAM.FEEDBACK, 0);
    setP(PARAM.DELAY_WET, 0);
    setP(PARAM.DRY_WET, 100);
    setP(PARAM.WOW_RATE, wowRate);
    setP(PARAM.WOW_DEPTH, wowDepth);
    setP(PARAM.FLUTTER_RATE, flutterRate);
    setP(PARAM.FLUTTER_DEPTH, flutterDepth);
    setP(PARAM.TAPE_AGE, tapeAge);
    setP(PARAM.ACTIVE_HEADS, 4);
    setP(PARAM.NOISE, 0);
    setP(PARAM.DROPOUT_SEV, 0);
    setP(PARAM.DRIVE, 20);
    setP(PARAM.HEAD_BUMP, 0);
    setP(PARAM.AZIMUTH, 0);
    setP(PARAM.TONE, 50);
    assert.equal(Module._hydra_dsp_commit(handle), 0);
    Module._hydra_dsp_reset(handle);

    inLPtr = Module._malloc(BLOCK * 4);
    inRPtr = Module._malloc(BLOCK * 4);
    outLPtr = Module._malloc(BLOCK * 4);
    outRPtr = Module._malloc(BLOCK * 4);

    const outL = new Float32Array(frames);
    const outR = new Float32Array(frames);
    for (let off = 0; off < frames; off += BLOCK) {
      const n = Math.min(BLOCK, frames - off);
      const hInL = Module.HEAPF32.subarray(inLPtr >> 2, (inLPtr >> 2) + BLOCK);
      const hInR = Module.HEAPF32.subarray(inRPtr >> 2, (inRPtr >> 2) + BLOCK);
      for (let i = 0; i < n; i++) {
        const s = 0.35 * Math.sin((2 * Math.PI * 1000.0 * (off + i)) / SAMPLE_RATE);
        hInL[i] = s;
        hInR[i] = s;
      }
      assert.equal(Module._hydra_dsp_process(handle, inLPtr, inRPtr, outLPtr, outRPtr, BLOCK), 0);
      outL.set(Module.HEAPF32.subarray(outLPtr >> 2, (outLPtr >> 2) + n), off);
      outR.set(Module.HEAPF32.subarray(outRPtr >> 2, (outRPtr >> 2) + n), off);
    }
    return analyzeModulation(outL, outR, { sampleRate: SAMPLE_RATE });
  } finally {
    if (inLPtr) Module._free(inLPtr);
    if (inRPtr) Module._free(inRPtr);
    if (outLPtr) Module._free(outLPtr);
    if (outRPtr) Module._free(outRPtr);
    if (handle) Module._hydra_dsp_destroy(handle);
  }
}

async function main() {
  for (const v of VARIANTS) {
    const Module = await loadVariant(v);
    const zero = probe(Module, { wowDepth: 0, flutterDepth: 0, tapeAge: 0 });
    const mid = probe(Module, { wowDepth: 25, flutterDepth: 25, tapeAge: 40 });
    const high = probe(Module, { wowDepth: 60, flutterDepth: 60, tapeAge: 80 });
    const fastFlt = probe(Module, { wowDepth: 10, flutterDepth: 45, flutterRate: 12.0, tapeAge: 40 });
    const slowFlt = probe(Module, { wowDepth: 10, flutterDepth: 45, flutterRate: 3.5, tapeAge: 40 });

    // Monotonic depth scaling must hold for all variants
    assert.ok(high.rmsCents > mid.rmsCents && mid.rmsCents > zero.rmsCents,
      `${v.key}: expected monotonic depth scaling (${zero.rmsCents} < ${mid.rmsCents} < ${high.rmsCents})`);

    // Flutter rate increase should shift energy / peak frequency upward in M2/M3
    if (v.key === 'm2') {
      assert.ok(fastFlt.peakModFreqHz > slowFlt.peakModFreqHz,
        `m2: expected flutterRate=12Hz peak (${fastFlt.peakModFreqHz}) > 3.5Hz peak (${slowFlt.peakModFreqHz})`);
    }

    console.log(
      `Control Audit ${v.key.toUpperCase()}: zero=${zero.rmsCents.toFixed(3)}c, mid=${mid.rmsCents.toFixed(2)}c, high=${high.rmsCents.toFixed(2)}c, fltPeak(3.5->12Hz)=${slowFlt.peakModFreqHz.toFixed(2)}->${fastFlt.peakModFreqHz.toFixed(2)}Hz`
    );
  }
}

main().catch((err) => {
  console.error('Control semantics audit FAILED:', err);
  process.exit(1);
});
