import fs from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const ROOT = path.resolve(__dirname, '..');
const WEB_BUILD = path.resolve(process.env.HYDRA_WEB_BUILD || path.join(ROOT, 'build-web', 'web'));
const OUT_DIR = path.join(ROOT, 'artifacts', 'phase7-4c');

function encodeWavStereo(samplesL, samplesR, sampleRate = 48000) {
  const n = samplesL.length;
  const dataSize = n * 4;
  const buf = Buffer.alloc(44 + dataSize);
  buf.write('RIFF', 0);
  buf.writeUInt32LE(36 + dataSize, 4);
  buf.write('WAVE', 8);
  buf.write('fmt ', 12);
  buf.writeUInt32LE(16, 16);
  buf.writeUInt16LE(1, 20);
  buf.writeUInt16LE(2, 22);
  buf.writeUInt32LE(sampleRate, 24);
  buf.writeUInt32LE(sampleRate * 4, 28);
  buf.writeUInt16LE(4, 32);
  buf.writeUInt16LE(16, 34);
  buf.write('data', 36);
  buf.writeUInt32LE(dataSize, 40);
  for (let i = 0; i < n; i++) {
    const l = Math.max(-1, Math.min(1, samplesL[i]));
    const r = Math.max(-1, Math.min(1, samplesR[i]));
    buf.writeInt16LE(Math.round(l * 32767), 44 + i * 4);
    buf.writeInt16LE(Math.round(r * 32767), 44 + i * 4 + 2);
  }
  return buf;
}

function generateGuitarArpeggio(sampleRate = 48000, durationSec = 4.0) {
  const n = Math.floor(sampleRate * durationSec);
  const out = new Float32Array(n);
  const notes = [196.0, 246.94, 293.66, 392.0, 493.88, 392.0]; // G major arpeggio
  const stepSamples = Math.floor(sampleRate * 0.45);
  for (let idx = 0; idx < notes.length; idx++) {
    const start = idx * stepSamples;
    const freq = notes[idx];
    for (let i = start; i < n; i++) {
      const t = (i - start) / sampleRate;
      const env = Math.exp(-t * 2.2) * (1 - Math.exp(-t * 180));
      const s = 0.24 * env * (
        0.65 * Math.sin(2 * Math.PI * freq * t) +
        0.25 * Math.sin(2 * Math.PI * freq * 2 * t) +
        0.10 * Math.sin(2 * Math.PI * freq * 3 * t)
      );
      out[i] += s;
    }
  }
  return out;
}

function computeStats(l, r) {
  let sumSq = 0;
  let peak = 0;
  let diffSumSq = 0;
  for (let i = 0; i < l.length; i++) {
    sumSq += 0.5 * (l[i] * l[i] + r[i] * r[i]);
    peak = Math.max(peak, Math.abs(l[i]), Math.abs(r[i]));
    const d = l[i] - r[i];
    diffSumSq += d * d;
  }
  return {
    rms: Number(Math.sqrt(sumSq / l.length).toFixed(6)),
    peak: Number(peak.toFixed(6)),
    stereoDiffRms: Number(Math.sqrt(diffSumSq / l.length).toFixed(6))
  };
}

function rmsDiff(a, b) {
  let sum = 0;
  for (let i = 0; i < a.length; i++) {
    const d = a[i] - b[i];
    sum += d * d;
  }
  return Number(Math.sqrt(sum / a.length).toFixed(6));
}

await fs.mkdir(OUT_DIR, { recursive: true });
const createHydraModule = (await import(pathToFileURL(path.join(WEB_BUILD, 'hydra_dsp.js')).href)).default;
const wasmBinary = await fs.readFile(path.join(WEB_BUILD, 'hydra_dsp.wasm'));
const mod = await createHydraModule({ wasmBinary });

const api = {
  create: mod.cwrap('hydra_dsp_create_v1_4', 'number', ['number', 'number', 'number', 'number']),
  destroy: mod.cwrap('hydra_dsp_destroy', null, ['number']),
  prepare: mod.cwrap('hydra_dsp_prepare', 'number', ['number', 'number', 'number']),
  setParam: mod.cwrap('hydra_dsp_set_parameter', 'number', ['number', 'number', 'number']),
  commit: mod.cwrap('hydra_dsp_commit', 'number', ['number']),
  process: mod.cwrap('hydra_dsp_process', 'number', ['number', 'number', 'number', 'number', 'number', 'number'])
};

function renderWasm(input, modeSchedule, feedback = 55) {
  const sr = 48000;
  const block = 128;
  const hPtr = mod._malloc(4);
  api.create(sr, 2000, 4000, hPtr);
  const h = mod.HEAP32[hPtr >> 2];
  mod._free(hPtr);
  api.prepare(h, block, 2);

  api.setParam(h, 11, 1);    // delayActive
  api.setParam(h, 12, 320);  // delayTimeMs
  api.setParam(h, 13, feedback);
  api.setParam(h, 14, 60);   // dryWet
  api.setParam(h, 15, 70);   // delayWet
  api.setParam(h, 16, 4);    // activeHeads
  api.setParam(h, 1, 16);    // wowDepth
  api.setParam(h, 0, 20);    // flutterDepth
  api.setParam(h, 6, 55);    // tapeAge
  api.setParam(h, 38, modeSchedule[0].mode);
  api.commit(h);

  const inL = mod._malloc(block * 4);
  const inR = mod._malloc(block * 4);
  const outL = mod._malloc(block * 4);
  const outR = mod._malloc(block * 4);
  const totalBlocks = Math.floor(input.length / block);
  const resL = new Float32Array(totalBlocks * block);
  const resR = new Float32Array(totalBlocks * block);
  let schedIdx = 1;

  for (let b = 0; b < totalBlocks; b++) {
    if (schedIdx < modeSchedule.length && b === modeSchedule[schedIdx].block) {
      api.setParam(h, 38, modeSchedule[schedIdx].mode);
      api.commit(h);
      schedIdx++;
    }
    mod.HEAPF32.set(input.subarray(b * block, (b + 1) * block), inL >> 2);
    mod.HEAPF32.set(input.subarray(b * block, (b + 1) * block), inR >> 2);
    api.process(h, inL, inR, outL, outR, block);
    resL.set(mod.HEAPF32.subarray(outL >> 2, (outL >> 2) + block), b * block);
    resR.set(mod.HEAPF32.subarray(outR >> 2, (outR >> 2) + block), b * block);
  }

  mod._free(inL); mod._free(inR); mod._free(outL); mod._free(outR);
  api.destroy(h);
  return { resL, resR };
}

const dry = generateGuitarArpeggio(48000, 4.0);
const modes = [
  { id: 'm0_classic', name: 'M0 — Classic', mode: 0 },
  { id: 'm1_motor_drift', name: 'M1 — Motor Drift', mode: 1 },
  { id: 'm2_capstan_flutter', name: 'M2 — Capstan Flutter', mode: 2 },
  { id: 'm3_worn_transport', name: 'M3 — Worn Transport', mode: 3 }
];

const renders = {};
const metrics = {};
for (const m of modes) {
  const { resL, resR } = renderWasm(dry, [{ block: 0, mode: m.mode }]);
  renders[m.id] = { resL, resR };
  metrics[m.id] = computeStats(resL, resR);
  await fs.writeFile(path.join(OUT_DIR, `${m.id}.wav`), encodeWavStereo(resL, resR));
}

// Runtime switching under high feedback (85%)
const switchRes = renderWasm(dry, [
  { block: 0, mode: 0 },
  { block: 375, mode: 1 },
  { block: 750, mode: 2 },
  { block: 1125, mode: 3 },
  { block: 1350, mode: 0 }
], 85);
await fs.writeFile(path.join(OUT_DIR, 'runtime_switch_high_feedback.wav'), encodeWavStereo(switchRes.resL, switchRes.resR));
metrics.runtime_switch_high_feedback = computeStats(switchRes.resL, switchRes.resR);

// Difference metrics vs M0
metrics.diff_vs_m0 = {
  m1_rms_diff: rmsDiff(renders.m0_classic.resL, renders.m1_motor_drift.resL),
  m2_rms_diff: rmsDiff(renders.m0_classic.resL, renders.m2_capstan_flutter.resL),
  m3_rms_diff: rmsDiff(renders.m0_classic.resL, renders.m3_worn_transport.resL)
};

await fs.writeFile(path.join(OUT_DIR, 'metrics.json'), JSON.stringify(metrics, null, 2));
console.log(JSON.stringify(metrics, null, 2));
