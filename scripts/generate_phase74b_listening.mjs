import { createHash } from 'node:crypto';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { analyzeModulation } from './phase74b_demodulator.mjs';

const root = resolve(import.meta.dirname, '..');
const outRoot = resolve(root, process.env.HYDRA_PHASE74B_OUT || 'artifacts/phase7-4b');

const VARIANTS = [
  { key: 'm0', dir: 'm0-classic', label: 'M0 — Classic C Baseline', buildDir: resolve(root, 'build/web-m0/web'), expectedMode: 'M0_CLASSIC_C_BASELINE' },
  { key: 'm1', dir: 'm1-motor-drift', label: 'M1 — Motor Drift & Inertia', buildDir: resolve(root, 'build/web-m1/web'), expectedMode: 'M1_MOTOR_DRIFT' },
  { key: 'm2', dir: 'm2-capstan-flutter', label: 'M2 — Capstan Flutter & Texture', buildDir: resolve(root, 'build/web-m2/web'), expectedMode: 'M2_CAPSTAN_FLUTTER' },
  { key: 'm3', dir: 'm3-unstable-transport', label: 'M3 — Worn Transport / Unstable', buildDir: resolve(root, 'build/web-m3/web'), expectedMode: 'M3_UNSTABLE_TRANSPORT' }
];

const SAMPLE_RATE = 48000;
const BLOCK = 128;

// Param IDs from dsp/core/hydra_dsp_c_api.cpp
const PARAM = {
  FLUTTER_DEPTH: 0,
  WOW_DEPTH: 1,
  DROPOUT_SEV: 2,
  DRIVE: 3,
  NOISE: 4,
  TAPE_SPEED: 5,
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
  BPM: 17,
  HEADS_MUSICAL: 18,
  GUITAR_FOCUS: 19,
  TONE: 20,
  PING_PONG: 21,
  FREEZE: 22,
  REVERSE: 23,
  REVERSE_SMEAR: 24,
  SPRING: 25,
  SPRING_DECAY: 26,
  SPRING_DAMPING: 27,
  SPRING_MIX: 28,
  RESET: 29,
  WARP_ACTIVE: 30,
  WARP_SPEED: 31,
  WARP_GRAIN_MS: 32,
  WARP_STEREO: 33,
  WARP_FEEDBACK: 34,
  WARP_MODE: 35,
  REVERSE_MODE: 36,
  REVERSE_DIVISION: 37
};

function encodeStereoWav(left, right, sampleRate = SAMPLE_RATE) {
  const frames = left.length;
  const dataBytes = frames * 4;
  const buf = Buffer.alloc(44 + dataBytes);
  buf.write('RIFF', 0);
  buf.writeUInt32LE(36 + dataBytes, 4);
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
  buf.writeUInt32LE(dataBytes, 40);
  for (let i = 0; i < frames; i++) {
    const l = Math.max(-1, Math.min(1, left[i]));
    const r = Math.max(-1, Math.min(1, right[i]));
    buf.writeInt16LE(Math.round(l * 32767), 44 + i * 4);
    buf.writeInt16LE(Math.round(r * 32767), 44 + i * 4 + 2);
  }
  return buf;
}

function sha256(buf) {
  return createHash('sha256').update(buf).digest('hex');
}

// Deterministic PRNG for audio synthesis
function makeRng(seed) {
  let s = seed >>> 0;
  return () => {
    s = (Math.imul(s, 1664525) + 1013904223) >>> 0;
    return ((s >>> 8) / 16777216.0) * 2.0 - 1.0;
  };
}

function synthCleanSustainedNote(frames, sr) {
  const out = new Float32Array(frames);
  const f0 = 220.0; // A3
  for (let i = 0; i < frames; i++) {
    const t = i / sr;
    if (t < 3.6) {
      const atk = Math.min(1.0, t / 0.008);
      const env = atk * Math.exp(-0.75 * t);
      const s = 0.55 * Math.sin(2 * Math.PI * f0 * t)
              + 0.25 * Math.sin(2 * Math.PI * 2 * f0 * t) * Math.exp(-0.9 * t)
              + 0.14 * Math.sin(2 * Math.PI * 3 * f0 * t) * Math.exp(-1.3 * t)
              + 0.07 * Math.sin(2 * Math.PI * 4 * f0 * t) * Math.exp(-1.8 * t);
      out[i] = 0.55 * env * s;
    }
  }
  return { left: out, right: Float32Array.from(out) };
}

function synthLongChords(frames, sr) {
  const left = new Float32Array(frames);
  const right = new Float32Array(frames);
  const freqs = [196.0, 246.94, 293.66, 369.99, 440.0]; // Gmaj9
  for (let i = 0; i < frames; i++) {
    const t = i / sr;
    if (t < 4.5) {
      const atk = Math.min(1.0, t / 0.06);
      const rel = t > 3.6 ? Math.max(0.0, 1.0 - (t - 3.6) / 0.9) : 1.0;
      const env = atk * rel * Math.exp(-0.22 * t);
      let sL = 0, sR = 0;
      for (let k = 0; k < freqs.length; k++) {
        const f = freqs[k];
        const v = Math.sin(2 * Math.PI * f * t) + 0.22 * Math.sin(2 * Math.PI * 2 * f * t);
        const pan = (k % 2 === 0) ? 0.55 : 0.45;
        sL += v * pan;
        sR += v * (1 - pan);
      }
      left[i] = 0.16 * env * sL;
      right[i] = 0.16 * env * sR;
    }
  }
  return { left, right };
}

function synthPickAttacks(frames, sr) {
  const out = new Float32Array(frames);
  const rng = makeRng(7403);
  const hits = [0.05, 0.45, 0.85, 1.25, 1.65, 2.05];
  const notes = [329.63, 392.0, 440.0, 392.0, 329.63, 293.66];
  for (let h = 0; h < hits.length; h++) {
    const start = Math.floor(hits[h] * sr);
    const f0 = notes[h];
    for (let i = start; i < frames; i++) {
      const dt = (i - start) / sr;
      if (dt > 0.55) break;
      const pick = dt < 0.012 ? rng() * Math.exp(-dt / 0.003) * 0.35 : 0;
      const env = Math.exp(-6.5 * dt);
      const body = Math.sin(2 * Math.PI * f0 * dt) + 0.45 * Math.sin(2 * Math.PI * 2 * f0 * dt) + 0.2 * Math.sin(2 * Math.PI * 3 * f0 * dt);
      out[i] += 0.32 * (env * body + pick);
    }
  }
  return { left: out, right: Float32Array.from(out) };
}

function synthElectricPiano(frames, sr) {
  const left = new Float32Array(frames);
  const right = new Float32Array(frames);
  const chord = [261.63, 329.63, 392.0, 493.88]; // Cmaj7
  for (let c = 0; c < chord.length; c++) {
    const f = chord[c];
    const start = Math.floor((0.04 + c * 0.045) * sr);
    for (let i = start; i < frames; i++) {
      const dt = (i - start) / sr;
      if (dt > 3.8) break;
      const tine = Math.sin(2 * Math.PI * f * 7.0 * dt) * Math.exp(-22.0 * dt) * 0.22;
      const fund = (Math.sin(2 * Math.PI * f * dt) + 0.30 * Math.sin(2 * Math.PI * 2 * f * dt) * Math.exp(-1.5 * dt)) * Math.exp(-0.9 * dt);
      const sig = 0.22 * (fund + tine);
      left[i] += sig * (0.6 - 0.08 * c);
      right[i] += sig * (0.4 + 0.08 * c);
    }
  }
  return { left, right };
}

function synthPercussive(frames, sr) {
  const out = new Float32Array(frames);
  const rng = makeRng(7405);
  const times = [0.05, 0.30, 0.55, 0.80, 1.05, 1.30, 1.55, 1.80];
  for (let k = 0; k < times.length; k++) {
    const start = Math.floor(times[k] * sr);
    const isAccent = (k % 2 === 0);
    for (let i = start; i < frames; i++) {
      const dt = (i - start) / sr;
      if (dt > 0.14) break;
      const body = Math.sin(2 * Math.PI * (isAccent ? 185 : 260) * dt) * Math.exp(-28 * dt);
      const snap = rng() * Math.exp(-90 * dt);
      out[i] += (isAccent ? 0.55 : 0.38) * (0.65 * body + 0.35 * snap);
    }
  }
  return { left: out, right: Float32Array.from(out) };
}

const SCENARIOS = [
  {
    id: '01-clean-sustained-guitar',
    title: '01. Clean Sustained Guitar Note',
    durationSec: 5.5,
    synth: synthCleanSustainedNote,
    params: { delayActive: 1, delayTimeMs: 320, feedback: 45, delayWet: 48, dryWet: 52, wowRate: 0.75, wowDepth: 22, flutterRate: 5.8, flutterDepth: 20, tapeAge: 40, activeHeads: 4 }
  },
  {
    id: '02-long-chords',
    title: '02. Long Sustained Chords',
    durationSec: 6.5,
    synth: synthLongChords,
    params: { delayActive: 1, delayTimeMs: 380, feedback: 52, delayWet: 55, dryWet: 45, wowRate: 0.65, wowDepth: 26, flutterRate: 5.2, flutterDepth: 22, tapeAge: 42, activeHeads: 4 }
  },
  {
    id: '03-defined-pick-attacks',
    title: '03. Defined Pick Attacks',
    durationSec: 5.0,
    synth: synthPickAttacks,
    params: { delayActive: 1, delayTimeMs: 260, feedback: 48, delayWet: 50, dryWet: 50, wowRate: 0.85, wowDepth: 20, flutterRate: 6.5, flutterDepth: 24, tapeAge: 38, activeHeads: 4 }
  },
  {
    id: '04-electric-piano-synth',
    title: '04. Electric Piano / Harmonic Synth',
    durationSec: 6.0,
    synth: synthElectricPiano,
    params: { delayActive: 1, delayTimeMs: 350, feedback: 50, delayWet: 52, dryWet: 48, wowRate: 0.70, wowDepth: 24, flutterRate: 6.0, flutterDepth: 25, tapeAge: 40, activeHeads: 4 }
  },
  {
    id: '05-percussive-material',
    title: '05. Percussive Material',
    durationSec: 4.5,
    synth: synthPercussive,
    params: { delayActive: 1, delayTimeMs: 220, feedback: 42, delayWet: 50, dryWet: 50, wowRate: 0.90, wowDepth: 18, flutterRate: 7.0, flutterDepth: 22, tapeAge: 35, activeHeads: 4 }
  },
  {
    id: '06-single-head-moderate-fb',
    title: '06. Single-Head Moderate Feedback',
    durationSec: 6.0,
    synth: synthPickAttacks,
    params: { delayActive: 1, delayTimeMs: 340, feedback: 54, delayWet: 62, dryWet: 38, wowRate: 0.80, wowDepth: 28, flutterRate: 6.2, flutterDepth: 28, tapeAge: 45, activeHeads: 4 }
  },
  {
    id: '07-single-head-high-fb',
    title: '07. Single-Head High Feedback (Expressive)',
    durationSec: 7.5,
    synth: synthCleanSustainedNote,
    params: { delayActive: 1, delayTimeMs: 330, feedback: 82, delayWet: 68, dryWet: 32, wowRate: 0.95, wowDepth: 48, flutterRate: 6.8, flutterDepth: 45, tapeAge: 65, activeHeads: 4 }
  },
  {
    id: '08-multi-head-three',
    title: '08. Multi-Head (3 Heads, activeHeads=7)',
    durationSec: 6.5,
    synth: synthPickAttacks,
    params: { delayActive: 1, delayTimeMs: 380, feedback: 58, delayWet: 60, dryWet: 40, wowRate: 0.80, wowDepth: 30, flutterRate: 6.0, flutterDepth: 28, tapeAge: 50, activeHeads: 7 }
  },
  {
    id: '09-ping-pong-stereo',
    title: '09. Ping-Pong Stereo',
    durationSec: 6.0,
    synth: synthPickAttacks,
    params: { delayActive: 1, delayTimeMs: 290, feedback: 60, delayWet: 62, dryWet: 38, wowRate: 0.85, wowDepth: 32, flutterRate: 6.4, flutterDepth: 30, tapeAge: 48, activeHeads: 4, pingPong: 1 }
  },
  {
    id: '10-reverse-feedback',
    title: '10. Reverse with Feedback',
    durationSec: 6.5,
    synth: synthLongChords,
    params: { delayActive: 1, delayTimeMs: 360, feedback: 66, delayWet: 65, dryWet: 35, wowRate: 0.80, wowDepth: 34, flutterRate: 6.0, flutterDepth: 30, tapeAge: 52, activeHeads: 4, reverse: 1 }
  }
];

async function loadWasmVariant(variant) {
  const buildInfo = JSON.parse(await readFile(resolve(variant.buildDir, 'hydra-build-info.json'), 'utf8'));
  if (buildInfo.motionMode !== variant.expectedMode) {
    throw new Error(`Metadata mismatch for ${variant.key}: expected ${variant.expectedMode}, got ${buildInfo.motionMode}`);
  }
  const modFactory = (await import(pathToFileURL(resolve(variant.buildDir, 'hydra_dsp.js')).href)).default;
  const wasmBytes = await readFile(resolve(variant.buildDir, 'hydra_dsp.wasm'));
  const Module = await modFactory({ wasmBinary: wasmBytes });
  return { Module, buildInfo };
}

function renderWithWasm(Module, inputL, inputR, params, sampleRate = SAMPLE_RATE) {
  const frames = inputL.length;
  let handle = 0;
  let inLPtr = 0;
  let inRPtr = 0;
  let outLPtr = 0;
  let outRPtr = 0;

  try {
    const outHandlePtr = Module._malloc(4);
    if (!outHandlePtr) throw new Error('Module._malloc(4) failed for outHandlePtr');
    try {
      Module.HEAP32[outHandlePtr >> 2] = 0;
      const createStatus = Module._hydra_dsp_create(sampleRate, 1600.0, outHandlePtr);
      handle = Module.HEAP32[outHandlePtr >> 2] >>> 0;
      if (createStatus !== 0 || handle === 0) {
        handle = 0;
        throw new Error(`hydra_dsp_create failed: status=${createStatus}, handle=${handle}`);
      }
    } finally {
      Module._free(outHandlePtr);
    }

    const prepStatus = Module._hydra_dsp_prepare(handle, sampleRate, 2);
    if (prepStatus !== 0) {
      throw new Error(`hydra_dsp_prepare failed: status=${prepStatus}`);
    }

    const setP = (id, val) => {
      const st = Module._hydra_dsp_set_parameter(handle, id, val);
      if (st !== 0) {
        throw new Error(`hydra_dsp_set_parameter(${id}, ${val}) failed: status=${st}`);
      }
    };
    setP(PARAM.DELAY_ACTIVE, params.delayActive ?? 1);
    setP(PARAM.DELAY_TIME_MS, params.delayTimeMs ?? 340);
    setP(PARAM.FEEDBACK, params.feedback ?? 50);
    setP(PARAM.DELAY_WET, params.delayWet ?? 55);
    setP(PARAM.DRY_WET, params.dryWet ?? 45);
    setP(PARAM.WOW_RATE, params.wowRate ?? 0.8);
    setP(PARAM.WOW_DEPTH, params.wowDepth ?? 20);
    setP(PARAM.FLUTTER_RATE, params.flutterRate ?? 6.0);
    setP(PARAM.FLUTTER_DEPTH, params.flutterDepth ?? 20);
    setP(PARAM.TAPE_AGE, params.tapeAge ?? 40);
    setP(PARAM.ACTIVE_HEADS, params.activeHeads ?? 4);
    setP(PARAM.PING_PONG, params.pingPong ?? 0);
    setP(PARAM.REVERSE, params.reverse ?? 0);
    setP(PARAM.NOISE, params.noise ?? 8.0);
    setP(PARAM.DROPOUT_SEV, params.dropoutSeverity ?? 2.0);
    if (params.drive !== undefined) setP(PARAM.DRIVE, params.drive);
    if (params.headBumpAmount !== undefined) setP(PARAM.HEAD_BUMP, params.headBumpAmount);
    if (params.azimuthError !== undefined) setP(PARAM.AZIMUTH, params.azimuthError);
    if (params.tone !== undefined) setP(PARAM.TONE, params.tone);

    const commitStatus = Module._hydra_dsp_commit(handle);
    if (commitStatus !== 0) {
      throw new Error(`hydra_dsp_commit failed: status=${commitStatus}`);
    }
    Module._hydra_dsp_reset(handle);

    inLPtr = Module._malloc(BLOCK * 4);
    inRPtr = Module._malloc(BLOCK * 4);
    outLPtr = Module._malloc(BLOCK * 4);
    outRPtr = Module._malloc(BLOCK * 4);
    if (!inLPtr || !inRPtr || !outLPtr || !outRPtr) {
      throw new Error('Module._malloc failed for audio block buffers');
    }

    const outL = new Float32Array(frames);
    const outR = new Float32Array(frames);

    for (let off = 0; off < frames; off += BLOCK) {
      const n = Math.min(BLOCK, frames - off);
      const heapInL = Module.HEAPF32.subarray(inLPtr >> 2, (inLPtr >> 2) + BLOCK);
      const heapInR = Module.HEAPF32.subarray(inRPtr >> 2, (inRPtr >> 2) + BLOCK);
      heapInL.fill(0);
      heapInR.fill(0);
      heapInL.set(inputL.subarray(off, off + n));
      heapInR.set(inputR.subarray(off, off + n));

      const procStatus = Module._hydra_dsp_process(handle, inLPtr, inRPtr, outLPtr, outRPtr, BLOCK);
      if (procStatus !== 0) {
        throw new Error(`hydra_dsp_process failed at offset ${off}: status=${procStatus}`);
      }

      const heapOutL = Module.HEAPF32.subarray(outLPtr >> 2, (outLPtr >> 2) + n);
      const heapOutR = Module.HEAPF32.subarray(outRPtr >> 2, (outRPtr >> 2) + n);
      outL.set(heapOutL, off);
      outR.set(heapOutR, off);
    }

    return { left: outL, right: outR };
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

function analyzeAudioMetrics(left, right) {
  let sumSqL = 0, sumSqR = 0, peak = 0, maxDelta = 0, nonFinite = 0;
  let dotLR = 0, sumMidSq = 0, sumSideSq = 0;
  let prevL = 0;
  for (let i = 0; i < left.length; i++) {
    const l = left[i], r = right[i];
    if (!Number.isFinite(l) || !Number.isFinite(r)) {
      nonFinite++;
      continue;
    }
    sumSqL += l * l;
    sumSqR += r * r;
    peak = Math.max(peak, Math.abs(l), Math.abs(r));
    if (i > 0) maxDelta = Math.max(maxDelta, Math.abs(l - prevL));
    prevL = l;
    dotLR += l * r;
    const m = 0.5 * (l + r);
    const s = 0.5 * (l - r);
    sumMidSq += m * m;
    sumSideSq += s * s;
  }
  const n = left.length;
  const rmsL = Math.sqrt(sumSqL / n);
  const rmsR = Math.sqrt(sumSqR / n);
  const lrCorr = (sumSqL > 1e-12 && sumSqR > 1e-12) ? dotLR / Math.sqrt(sumSqL * sumSqR) : 1.0;
  const sideRatioDb = 10 * Math.log10((sumSideSq + 1e-12) / (sumMidSq + 1e-12));
  return {
    rmsDb: 20 * Math.log10(Math.max(1e-9, 0.5 * (rmsL + rmsR))),
    peakDb: 20 * Math.log10(Math.max(1e-9, peak)),
    maxSampleDelta: maxDelta,
    lrCorrelation: lrCorr,
    sideToMidDb: sideRatioDb,
    nonFinite
  };
}

// Quadrature demodulation of an isolated 1000 Hz sine carrier (dryWet=100 -> dryGain=0, delayWet=0 -> single delayed tap)
function runModulationProbe(Module, settingLabel, wowDepth, flutterDepth, tapeAge, sampleRate = SAMPLE_RATE) {
  const durationSec = 9.2;
  const frames = Math.floor(durationSec * sampleRate);
  const carrierHz = 1000.0;
  const inL = new Float32Array(frames);
  const inR = new Float32Array(frames);
  for (let i = 0; i < frames; i++) {
    const s = 0.35 * Math.sin((2 * Math.PI * carrierHz * i) / sampleRate);
    inL[i] = s;
    inR[i] = s;
  }

  const { left, right } = renderWithWasm(Module, inL, inR, {
    delayActive: 1,
    delayTimeMs: 300,
    feedback: 0,
    delayWet: 0,
    dryWet: 100,
    wowRate: 0.8,
    wowDepth,
    flutterRate: 6.0,
    flutterDepth,
    tapeAge,
    activeHeads: 4,
    noise: 0,
    dropoutSeverity: 0,
    drive: 20,
    headBumpAmount: 0,
    azimuthError: 0,
    tone: 50
  }, sampleRate);

  const metrics = analyzeModulation(left, right, {
    sampleRate,
    carrierHz,
    settleSec: 0.60,
    filterWarmupSec: 0.25,
    lpfCutoffHz: 35.0
  });

  return {
    setting: settingLabel,
    ...metrics
  };
}

function buildHtmlPlayer(manifest) {
  const jsonEscaped = JSON.stringify(manifest).replace(/</g, '\\u003c');
  return `<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8" />
<meta name="viewport" content="width=device-width,initial-scale=1" />
<title>Hydra Tape Engine — Phase 7.4B Organic Motion A/B Lab</title>
<style>
  :root { --bg:#0d1016; --panel:#161b24; --border:#273142; --text:#e6ecf5; --muted:#93a1b8; --accent:#4cc9f0; --warn:#f7b801; }
  * { box-sizing:border-box; }
  body { margin:0; font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif; background:var(--bg); color:var(--text); padding:24px; }
  .wrap { max-width:1040px; margin:0 auto; }
  h1 { margin:0 0 6px; font-size:1.5rem; }
  p.sub { margin:0 0 18px; color:var(--muted); font-size:0.92rem; }
  .card { background:var(--panel); border:1px solid var(--border); border-radius:12px; padding:18px; margin-bottom:16px; }
  .row { display:flex; flex-wrap:wrap; gap:10px; align-items:center; }
  select, button { background:#1e2634; color:var(--text); border:1px solid var(--border); border-radius:8px; padding:9px 14px; font-size:0.92rem; cursor:pointer; }
  button.active { background:var(--accent); color:#081018; border-color:var(--accent); font-weight:700; }
  button.blind-on { border-color:var(--warn); color:var(--warn); }
  .meta { font-size:0.84rem; color:var(--muted); margin-top:10px; line-height:1.5; }
  table { width:100%; border-collapse:collapse; font-size:0.85rem; margin-top:10px; }
  th, td { border-bottom:1px solid var(--border); padding:8px 10px; text-align:left; }
  th { color:var(--muted); font-weight:600; }
</style>
</head>
<body>
<div class="wrap">
  <h1>Hydra Tape Engine — Phase 7.4B: Organic Tape Motion A/B Lab</h1>
  <p class="sub">Synchronized, glitch-free switching between M0 (Classic C), M1 (Motor Drift), M2 (Capstan Flutter), and M3 (Worn Transport). No automatic winner.</p>

  <div class="card">
    <div class="row" style="margin-bottom:12px;">
      <label for="scenarioSelect"><strong>Scenario:</strong></label>
      <select id="scenarioSelect" style="flex:1; min-width:260px;"></select>
      <button id="playBtn">Play</button>
      <button id="stopBtn">Stop</button>
      <label style="display:flex;align-items:center;gap:6px;font-size:0.88rem;"><input type="checkbox" id="loopChk" checked /> Loop</label>
    </div>
    <div class="row" id="variantBtns" style="margin-bottom:10px;"></div>
    <div class="row">
      <button id="blindBtn">Enable Blind A/B Mode</button>
      <button id="revealBtn" style="display:none;">Reveal Identities</button>
      <span id="statusText" style="color:var(--muted);font-size:0.86rem;">Ready</span>
    </div>
    <div class="meta" id="scenarioMeta"></div>
  </div>

  <div class="card">
    <strong>Objective Modulation Summary (1 kHz Carrier Probe)</strong>
    <div id="probeTable"></div>
  </div>
</div>
<script>
const MANIFEST = ${jsonEscaped};
const scenarioSelect = document.getElementById('scenarioSelect');
const variantBtnsDiv = document.getElementById('variantBtns');
const playBtn = document.getElementById('playBtn');
const stopBtn = document.getElementById('stopBtn');
const loopChk = document.getElementById('loopChk');
const blindBtn = document.getElementById('blindBtn');
const revealBtn = document.getElementById('revealBtn');
const statusText = document.getElementById('statusText');
const scenarioMeta = document.getElementById('scenarioMeta');
const probeTable = document.getElementById('probeTable');

let audioCtx = null;
let buffers = {};
let gains = {};
let sources = [];
let activeSlot = 0;
let isPlaying = false;
let blindMode = false;
let revealed = false;
let slotOrder = ['m0', 'm1', 'm2', 'm3'];

MANIFEST.scenarios.forEach((sc, idx) => {
  const opt = document.createElement('option');
  opt.value = idx;
  opt.textContent = sc.title;
  scenarioSelect.appendChild(opt);
});

function shuffleSlots(arr) {
  const copy = [...arr];
  for (let i = copy.length - 1; i > 0; i--) {
    const j = Math.floor(Math.random() * (i + 1));
    [copy[i], copy[j]] = [copy[j], copy[i]];
  }
  return copy;
}

function applySlotGains() {
  if (!audioCtx) return;
  const now = audioCtx.currentTime;
  slotOrder.forEach((k, i) => {
    if (gains[k]) {
      gains[k].gain.setTargetAtTime(i === activeSlot ? 1.0 : 0.0, now, 0.015);
    }
  });
}

function renderVariantButtons() {
  variantBtnsDiv.innerHTML = '';
  const labels = ['Slot A', 'Slot B', 'Slot C', 'Slot D'];
  slotOrder.forEach((vKey, idx) => {
    const vInfo = MANIFEST.variants.find(v => v.key === vKey);
    const btn = document.createElement('button');
    btn.textContent = (blindMode && !revealed) ? labels[idx] : vInfo.label;
    btn.dataset.slotIndex = String(idx);
    if (!blindMode || revealed) btn.dataset.variant = vKey;
    if (idx === activeSlot) btn.classList.add('active');
    btn.onclick = () => switchSlot(idx);
    variantBtnsDiv.appendChild(btn);
  });
}

function switchSlot(idx) {
  activeSlot = idx;
  applySlotGains();
  renderVariantButtons();
}

async function loadScenario(idx) {
  stopAudio();
  const sc = MANIFEST.scenarios[idx];
  statusText.textContent = 'Loading WAVs...';
  if (!audioCtx) audioCtx = new (window.AudioContext || window.webkitAudioContext)();
  buffers = {};
  await Promise.all(MANIFEST.variants.map(async v => {
    const resp = await fetch('../' + sc.files[v.key]);
    const arr = await resp.arrayBuffer();
    buffers[v.key] = await audioCtx.decodeAudioData(arr);
  }));
  scenarioMeta.innerHTML = '<strong>Parameters:</strong> ' + JSON.stringify(sc.params);
  statusText.textContent = 'Loaded: ' + sc.title;
}

function startAudio() {
  if (!audioCtx) return;
  if (audioCtx.state === 'suspended') audioCtx.resume();
  stopAudio();
  const now = audioCtx.currentTime + 0.03;
  sources = [];
  gains = {};
  slotOrder.forEach((k, idx) => {
    const src = audioCtx.createBufferSource();
    src.buffer = buffers[k];
    src.loop = loopChk.checked;
    const g = audioCtx.createGain();
    g.gain.setValueAtTime(idx === activeSlot ? 1.0 : 0.0, now);
    src.connect(g).connect(audioCtx.destination);
    src.start(now);
    sources.push(src);
    gains[k] = g;
  });
  isPlaying = true;
  statusText.textContent = 'Playing (synchronized)';
}

function stopAudio() {
  sources.forEach(s => { try { s.stop(); } catch {} });
  sources = [];
  isPlaying = false;
  statusText.textContent = 'Stopped';
}

playBtn.onclick = async () => {
  if (Object.keys(buffers).length === 0) await loadScenario(Number(scenarioSelect.value));
  startAudio();
};
stopBtn.onclick = stopAudio;
loopChk.onchange = () => {
  sources.forEach(s => { s.loop = loopChk.checked; });
};
scenarioSelect.onchange = () => loadScenario(Number(scenarioSelect.value));
blindBtn.onclick = () => {
  blindMode = !blindMode;
  revealed = false;
  if (blindMode) {
    slotOrder = shuffleSlots(['m0', 'm1', 'm2', 'm3']);
    blindBtn.classList.add('blind-on');
    blindBtn.textContent = 'Blind A/B Active (Click to Exit)';
    revealBtn.style.display = 'inline-block';
  } else {
    slotOrder = ['m0', 'm1', 'm2', 'm3'];
    blindBtn.classList.remove('blind-on');
    blindBtn.textContent = 'Enable Blind A/B Mode';
    revealBtn.style.display = 'none';
  }
  applySlotGains();
  renderVariantButtons();
};
revealBtn.onclick = () => { revealed = true; renderVariantButtons(); };

window.__hydraLabState = () => ({
  activeSlot,
  activeVariant: slotOrder[activeSlot],
  slotOrder: [...slotOrder],
  blindMode,
  revealed,
  isPlaying,
  gainTargets: Object.fromEntries(slotOrder.map(k => [k, gains[k] ? gains[k].gain.value : null]))
});

// Render modulation probe summary
let html = '<table><tr><th>Variant</th><th>Setting</th><th>RMS Pitch (cents)</th><th>Max (cents)</th><th>Max Excursion (ms)</th><th>Wow Band (cents)</th><th>Flutter Band (cents)</th><th>L/R Mod Corr</th></tr>';
MANIFEST.modulationProbes.forEach(p => {
  html += '<tr><td>' + p.variant + '</td><td>' + p.setting + '</td><td>' + p.rmsCents.toFixed(2) + '</td><td>' + p.maxAbsCents.toFixed(2) + '</td><td>' + p.maxExcursionMs.toFixed(3) + '</td><td>' + p.wowBandRmsCents.toFixed(2) + '</td><td>' + p.flutterBandRmsCents.toFixed(2) + '</td><td>' + p.modCorrelationLR.toFixed(3) + '</td></tr>';
});
html += '</table>';
probeTable.innerHTML = html;

renderVariantButtons();
loadScenario(0);
</script>
</body>
</html>`;
}

async function main() {
  await mkdir(outRoot, { recursive: true });
  for (const v of VARIANTS) {
    await mkdir(resolve(outRoot, v.dir), { recursive: true });
  }
  await mkdir(resolve(outRoot, 'comparisons'), { recursive: true });

  const loaded = {};
  for (const v of VARIANTS) {
    loaded[v.key] = await loadWasmVariant(v);
  }

  const modulationProbes = [];
  for (const v of VARIANTS) {
    modulationProbes.push({
      variant: v.key,
      ...runModulationProbe(loaded[v.key].Module, 'minimal (wow=0, flt=0, age=0) @ 48k', 0, 0, 0, 48000)
    });
    modulationProbes.push({
      variant: v.key,
      ...runModulationProbe(loaded[v.key].Module, 'subtle (wow=15, flt=15, age=40) @ 48k', 15, 15, 40, 48000)
    });
    modulationProbes.push({
      variant: v.key,
      ...runModulationProbe(loaded[v.key].Module, 'expressive (wow=50, flt=45, age=70) @ 48k', 50, 45, 70, 48000)
    });
    modulationProbes.push({
      variant: v.key,
      ...runModulationProbe(loaded[v.key].Module, 'expressive (wow=50, flt=45, age=70) @ 44.1k', 50, 45, 70, 44100)
    });
  }

  const scenarioResults = [];
  for (const sc of SCENARIOS) {
    const frames = Math.floor(sc.durationSec * SAMPLE_RATE);
    const { left: inL, right: inR } = sc.synth(frames, SAMPLE_RATE);
    const files = {};
    const sha = {};
    const metrics = {};

    for (const v of VARIANTS) {
      const rendered = renderWithWasm(loaded[v.key].Module, inL, inR, sc.params);
      const wav = encodeStereoWav(rendered.left, rendered.right, SAMPLE_RATE);
      const relPath = `${v.dir}/${sc.id}.wav`;
      await writeFile(resolve(outRoot, relPath), wav);
      files[v.key] = relPath;
      sha[v.key] = sha256(wav);
      metrics[v.key] = analyzeAudioMetrics(rendered.left, rendered.right);
    }
    scenarioResults.push({
      id: sc.id,
      title: sc.title,
      durationSec: sc.durationSec,
      params: sc.params,
      files,
      sha256: sha,
      metrics
    });
  }

  const manifest = {
    phase: '7.4B',
    title: 'Organic Tape Motion — Motor Drift, Flutter Texture & Mechanical Instability',
    sampleRate: SAMPLE_RATE,
    variants: VARIANTS.map(v => ({ key: v.key, label: v.label, buildInfo: loaded[v.key].buildInfo })),
    modulationProbes,
    scenarios: scenarioResults
  };

  await writeFile(resolve(outRoot, 'manifest.json'), JSON.stringify(manifest, null, 2) + '\n', 'utf8');
  await writeFile(resolve(outRoot, 'modulation-analysis.json'), JSON.stringify({ sampleRate: SAMPLE_RATE, modulationProbes, scenarios: scenarioResults.map(s => ({ id: s.id, metrics: s.metrics })) }, null, 2) + '\n', 'utf8');
  await writeFile(resolve(outRoot, 'comparisons/index.html'), buildHtmlPlayer(manifest), 'utf8');

  let report = '# Phase 7.4B — Organic Tape Motion Listening Report\n\n';
  report += '## Modulation Probe Summary (1 kHz Carrier Quadrature Demodulation)\n\n';
  report += '| Variant | Setting | RMS Pitch (cents) | Max (cents) | Max Excursion (ms) | Wow (0.1-3 Hz) | Flutter (3-25 Hz) | Events | L/R Corr |\n';
  report += '|---|---|---|---|---|---|---|---|---|\n';
  for (const p of modulationProbes) {
    report += `| ${p.variant.toUpperCase()} | ${p.setting} | ${p.rmsCents.toFixed(2)} | ${p.maxAbsCents.toFixed(2)} | ${p.maxExcursionMs.toFixed(3)} | ${p.wowBandRmsCents.toFixed(2)} | ${p.flutterBandRmsCents.toFixed(2)} | ${p.transientEventCount} | ${p.modCorrelationLR.toFixed(3)} |\n`;
  }
  await writeFile(resolve(outRoot, 'listening-report.md'), report, 'utf8');
  console.log(`Generated Phase 7.4B listening package at ${outRoot}`);
}

main().catch(err => {
  console.error(err);
  process.exit(1);
});
