import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

const root = resolve(import.meta.dirname, '..');
const outRoot = resolve(root, process.env.HYDRA_PHASE74A_OUT || 'artifacts/phase7-4a');

const VARIANTS = [
  { key: 'c0', dir: 'c0-baseline', label: 'C0 — Classic C Baseline', buildDir: resolve(root, 'build/web-c0/web'), expectedMode: 'C0_CLASSIC_C_BASELINE' },
  { key: 'c1', dir: 'c1-spectral-wear', label: 'C1 — Dynamic Spectral Wear', buildDir: resolve(root, 'build/web-c1/web'), expectedMode: 'C1_DYNAMIC_SPECTRAL_WEAR' },
  { key: 'c2', dir: 'c2-transient-softening', label: 'C2 — Transient Softening', buildDir: resolve(root, 'build/web-c2/web'), expectedMode: 'C2_TRANSIENT_SOFTENING' },
  { key: 'c3', dir: 'c3-combined', label: 'C3 — Combined Aging', buildDir: resolve(root, 'build/web-c3/web'), expectedMode: 'C3_COMBINED_AGING' }
];

const SAMPLE_RATE = 48000;
const BLOCK = 128;

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

// Radix-2 Cooley-Tukey FFT on real window (power of 2)
function fftMagSq(signal, start, size) {
  const re = new Float64Array(size);
  const im = new Float64Array(size);
  for (let i = 0; i < size; i++) {
    const idx = start + i;
    const w = 0.5 * (1 - Math.cos((2 * Math.PI * i) / (size - 1)));
    re[i] = (idx < signal.length ? signal[idx] : 0) * w;
  }
  // Bit reversal
  let j = 0;
  for (let i = 1; i < size; i++) {
    let bit = size >> 1;
    while (j & bit) {
      j ^= bit;
      bit >>= 1;
    }
    j ^= bit;
    if (i < j) {
      const tr = re[i]; re[i] = re[j]; re[j] = tr;
    }
  }
  for (let len = 2; len <= size; len <<= 1) {
    const ang = (-2 * Math.PI) / len;
    const wlenRe = Math.cos(ang);
    const wlenIm = Math.sin(ang);
    for (let i = 0; i < size; i += len) {
      let wRe = 1, wIm = 0;
      for (let k = 0; k < len / 2; k++) {
        const uRe = re[i + k], uIm = im[i + k];
        const vRe = re[i + k + len / 2] * wRe - im[i + k + len / 2] * wIm;
        const vIm = re[i + k + len / 2] * wIm + im[i + k + len / 2] * wRe;
        re[i + k] = uRe + vRe;
        im[i + k] = uIm + vIm;
        re[i + k + len / 2] = uRe - vRe;
        im[i + k + len / 2] = uIm - vIm;
        const nextWRe = wRe * wlenRe - wIm * wlenIm;
        wIm = wRe * wlenIm + wIm * wlenRe;
        wRe = nextWRe;
      }
    }
  }
  const bins = size / 2;
  const magSq = new Float64Array(bins);
  for (let k = 0; k < bins; k++) {
    magSq[k] = re[k] * re[k] + im[k] * im[k];
  }
  return magSq;
}

function spectralMetrics(mono, startSample, lengthSamples, fftSize = 4096) {
  const hop = fftSize / 2;
  const bins = fftSize / 2;
  const acc = new Float64Array(bins);
  let frames = 0;
  for (let s = startSample; s + fftSize <= startSample + lengthSamples && s + fftSize <= mono.length; s += hop) {
    const m = fftMagSq(mono, s, fftSize);
    for (let k = 0; k < bins; k++) acc[k] += m[k];
    frames++;
  }
  if (frames === 0) {
    const m = fftMagSq(mono, startSample, fftSize);
    for (let k = 0; k < bins; k++) acc[k] = m[k];
    frames = 1;
  }
  let totalE = 0, weightedF = 0;
  let lowE = 0, midE = 0, highE = 0;
  const df = SAMPLE_RATE / fftSize;
  for (let k = 1; k < bins; k++) {
    const f = k * df;
    const e = acc[k] / frames;
    totalE += e;
    weightedF += f * e;
    if (f < 250) lowE += e;
    else if (f < 2000) midE += e;
    else if (f <= 16000) highE += e;
  }
  const eps = 1e-18;
  return {
    centroidHz: totalE > eps ? weightedF / totalE : 0,
    lowEnergy: lowE,
    midEnergy: midE,
    highEnergy: highE,
    highLowRatioDb: 10 * Math.log10((highE + eps) / (lowE + eps))
  };
}

function analyzeSignal(left, right, scenario) {
  const n = left.length;
  const mono = new Float32Array(n);
  let sumSq = 0, peak = 0, dcL = 0, dcR = 0, nonFinite = 0;
  for (let i = 0; i < n; i++) {
    const l = left[i], r = right[i];
    if (!Number.isFinite(l) || !Number.isFinite(r)) { nonFinite++; continue; }
    mono[i] = 0.5 * (l + r);
    sumSq += l * l + r * r;
    dcL += l; dcR += r;
    peak = Math.max(peak, Math.abs(l), Math.abs(r));
  }
  const rms = Math.sqrt(sumSq / Math.max(1, 2 * n));
  const spec = spectralMetrics(mono, 0, n);

  // Envelope evolution & tail decay in 0.5s windows
  const winLen = Math.floor(0.5 * SAMPLE_RATE);
  const envRmsDb = [];
  for (let s = 0; s + winLen <= n; s += winLen) {
    let wSq = 0;
    for (let i = 0; i < winLen; i++) wSq += mono[s + i] * mono[s + i];
    envRmsDb.push(20 * Math.log10(Math.sqrt(wSq / winLen) + 1e-9));
  }

  // Crest factor & residual noise
  const crestDb = 20 * Math.log10((peak + 1e-9) / (rms + 1e-9));
  const tailStart = Math.floor(n * 0.75);
  let tailSq = 0;
  for (let i = tailStart; i < n; i++) tailSq += mono[i] * mono[i];
  const tailRms = Math.sqrt(tailSq / Math.max(1, n - tailStart));

  // For Single-Head scenarios with impulse/pluck, measure per-echo spectral centroid & crest
  let echoWindows = null;
  if (scenario.singleHeadEchoMs) {
    echoWindows = [];
    const delaySec = scenario.singleHeadEchoMs / 1000;
    const t0 = scenario.impulseStartSec ?? 0.05;
    for (let e = 0; e < 5; e++) {
      const start = Math.floor((t0 + e * delaySec) * SAMPLE_RATE);
      const len = Math.min(Math.floor(delaySec * 0.85 * SAMPLE_RATE), Math.floor(0.22 * SAMPLE_RATE));
      if (start + 2048 < n) {
        let ePeak = 0, eSq = 0;
        for (let i = 0; i < len && start + i < n; i++) {
          const v = Math.abs(mono[start + i]);
          ePeak = Math.max(ePeak, v);
          eSq += v * v;
        }
        const eRms = Math.sqrt(eSq / Math.max(1, len));
        const sp = spectralMetrics(mono, start, Math.max(2048, len), 2048);
        echoWindows.push({
          echoIndex: e,
          startSec: Number((start / SAMPLE_RATE).toFixed(3)),
          rmsDb: Number((20 * Math.log10(eRms + 1e-9)).toFixed(2)),
          crestDb: Number((20 * Math.log10((ePeak + 1e-9) / (eRms + 1e-9))).toFixed(2)),
          centroidHz: Number(sp.centroidHz.toFixed(1)),
          highLowRatioDb: Number(sp.highLowRatioDb.toFixed(2))
        });
      }
    }
  }

  return {
    rms: Number(rms.toFixed(6)),
    peak: Number(peak.toFixed(6)),
    crestDb: Number(crestDb.toFixed(2)),
    dcOffsetL: Number((dcL / n).toExponential(3)),
    dcOffsetR: Number((dcR / n).toExponential(3)),
    nonFinite,
    centroidHz: Number(spec.centroidHz.toFixed(1)),
    lowEnergy: Number(spec.lowEnergy.toExponential(3)),
    midEnergy: Number(spec.midEnergy.toExponential(3)),
    highEnergy: Number(spec.highEnergy.toExponential(3)),
    highLowRatioDb: Number(spec.highLowRatioDb.toFixed(2)),
    tailRmsDb: Number((20 * Math.log10(tailRms + 1e-9)).toFixed(2)),
    envelopeRmsDb: envRmsDb.map(v => Number(v.toFixed(2))),
    echoWindows
  };
}

// Deterministic stimuli
function genPluckStimulus(durationSec = 4.0, singleHit = false) {
  const frames = Math.floor(SAMPLE_RATE * durationSec);
  const left = new Float32Array(frames);
  const right = new Float32Array(frames);
  const hits = singleHit
    ? [{ t: 0.05, f: 440, pan: 0.5 }]
    : [
        { t: 0.05, f: 440, pan: 0.45 },
        { t: 0.65, f: 587.33, pan: 0.55 },
        { t: 1.25, f: 659.25, pan: 0.48 }
      ];
  for (const hit of hits) {
    const start = Math.floor(hit.t * SAMPLE_RATE);
    const len = Math.floor(0.14 * SAMPLE_RATE);
    for (let k = 0; k < len && start + k < frames; k++) {
      const attack = 1 - Math.exp(-k / 24);
      const decay = Math.exp(-k / (0.025 * SAMPLE_RATE));
      const s = 0.60 * attack * decay * (
        Math.sin((2 * Math.PI * hit.f * k) / SAMPLE_RATE) +
        0.45 * Math.sin((2 * Math.PI * hit.f * 2 * k) / SAMPLE_RATE) +
        0.28 * Math.sin((2 * Math.PI * hit.f * 3 * k) / SAMPLE_RATE) +
        0.16 * Math.sin((2 * Math.PI * hit.f * 5 * k) / SAMPLE_RATE)
      );
      left[start + k] += s * (1 - hit.pan * 0.2);
      right[start + k] += s * (0.8 + hit.pan * 0.2);
    }
  }
  return { left, right };
}

function genSustainedChords(durationSec = 4.0) {
  const frames = Math.floor(SAMPLE_RATE * durationSec);
  const left = new Float32Array(frames);
  const right = new Float32Array(frames);
  const chords = [
    { t0: 0.05, t1: 1.15, freqs: [220, 277.18, 329.63, 415.30] },
    { t0: 1.30, t1: 2.35, freqs: [196, 246.94, 293.66, 369.99] }
  ];
  for (const ch of chords) {
    const i0 = Math.floor(ch.t0 * SAMPLE_RATE);
    const i1 = Math.floor(ch.t1 * SAMPLE_RATE);
    for (let i = i0; i < i1 && i < frames; i++) {
      const tRel = (i - i0) / SAMPLE_RATE;
      const dur = ch.t1 - ch.t0;
      const env = Math.min(1, tRel / 0.04) * Math.min(1, (dur - tRel) / 0.12);
      let sL = 0, sR = 0;
      ch.freqs.forEach((f, idx) => {
        const tone = Math.sin((2 * Math.PI * f * i) / SAMPLE_RATE) +
                     0.35 * Math.sin((2 * Math.PI * f * 2 * i) / SAMPLE_RATE) +
                     0.15 * Math.sin((2 * Math.PI * f * 3 * i) / SAMPLE_RATE);
        const pan = 0.35 + (idx / (ch.freqs.length - 1)) * 0.3;
        sL += tone * (1 - pan) * 0.15;
        sR += tone * pan * 0.15;
      });
      left[i] += sL * env;
      right[i] += sR * env;
    }
  }
  return { left, right };
}

function genMixedAutomation(durationSec = 4.0) {
  const p = genPluckStimulus(durationSec, false);
  const c = genSustainedChords(durationSec);
  for (let i = 0; i < p.left.length; i++) {
    p.left[i] = 0.55 * p.left[i] + 0.55 * c.left[i];
    p.right[i] = 0.55 * p.right[i] + 0.55 * c.right[i];
  }
  return p;
}

async function loadProcessor(buildDir) {
  if (globalThis.__hydraShared?.runtimes instanceof Map) {
    globalThis.__hydraShared.runtimes.clear();
  }
  let Processor;
  globalThis.sampleRate = SAMPLE_RATE;
  if (typeof globalThis.importScripts !== 'function') {
    globalThis.importScripts = () => {};
  }
  if (typeof globalThis.self === 'undefined') {
    globalThis.self = globalThis;
  }
  if (typeof globalThis.location === 'undefined') {
    globalThis.location = { href: import.meta.url };
  }
  globalThis.AudioWorkletProcessor = class {
    constructor() {
      this.readyMessage = new Promise((resolveReady) => {
        this.port = {
          postMessage: (msg) => {
            if (msg.type === 'ready' || msg.type === 'error') resolveReady(msg);
          }
        };
      });
    }
  };
  globalThis.registerProcessor = (_name, processor) => { Processor = processor; };
  const url = `${pathToFileURL(resolve(buildDir, 'hydra-processor.js')).href}?build=${encodeURIComponent(buildDir)}`;
  await import(url);
  const wasmBinary = await readFile(resolve(buildDir, 'hydra_dsp.wasm'));
  return { Processor, wasmBinary };
}

async function renderScenario({ Processor, wasmBinary }, stimulus, scenario) {
  if (globalThis.__hydraShared?.runtimes instanceof Map) {
    globalThis.__hydraShared.runtimes.clear();
  }
  const origVersions = process.versions;
  Object.defineProperty(process, 'versions', { value: {}, configurable: true });
  const worklet = new Processor({ processorOptions: { wasmBinary } });
  const ready = await worklet.readyMessage;
  Object.defineProperty(process, 'versions', { value: origVersions, configurable: true });
  if (ready.type !== 'ready') throw new Error(`Worklet init failed: ${ready.message}`);
  try {
    worklet.port.onmessage({ data: { type: 'command', command: 'delayActive', value: 1 } });
    for (const [command, value] of Object.entries(scenario.commands || {})) {
      worklet.port.onmessage({ data: { type: 'command', command, value } });
    }
    const baseParams = {
      p_0: [10], p_1: [8], p_2: [2], p_3: [35], p_4: [0],
      p_5: [50], p_6: [35], p_7: [30], p_8: [10], p_9: [6.0], p_10: [0.8],
      p_12: [280], p_13: [55], p_14: [55], p_15: [70], p_17: [120], p_20: [50],
      ...scenario.params
    };

    const frames = stimulus.left.length;
    const outAllL = new Float32Array(frames);
    const outAllR = new Float32Array(frames);
    const inL = new Float32Array(BLOCK);
    const inR = new Float32Array(BLOCK);
    const outL = new Float32Array(BLOCK);
    const outR = new Float32Array(BLOCK);

    for (let start = 0; start < frames; start += BLOCK) {
      const count = Math.min(BLOCK, frames - start);
      const t = start / frames;
      const blockParams = scenario.automate ? scenario.automate(baseParams, t) : baseParams;
      inL.fill(0); inR.fill(0);
      inL.set(stimulus.left.subarray(start, start + count));
      inR.set(stimulus.right.subarray(start, start + count));
      worklet.process([[inL, inR]], [[outL, outR]], blockParams);
      outAllL.set(outL.subarray(0, count), start);
      outAllR.set(outR.subarray(0, count), start);
    }
    return { left: outAllL, right: outAllR };
  } finally {
    worklet.port.onmessage({ data: { type: 'destroy' } });
  }
}

const stimuli = {
  singlePluck: genPluckStimulus(4.0, true),
  multiPluck: genPluckStimulus(4.0, false),
  chords: genSustainedChords(4.0),
  mixed: genMixedAutomation(4.0)
};

const scenarios = [
  {
    id: '01_single_head_moderate_fb',
    title: 'Single-Head Moderate Feedback',
    stimulusKey: 'singlePluck',
    singleHeadEchoMs: 320,
    params: { p_12: [320], p_13: [55], p_14: [55], p_15: [75], p_6: [35] },
    commands: { activeHeads: 4, pingPong: 0, reverse: 0 }
  },
  {
    id: '02_single_head_high_fb',
    title: 'Single-Head High Feedback',
    stimulusKey: 'singlePluck',
    singleHeadEchoMs: 280,
    params: { p_12: [280], p_13: [85], p_14: [60], p_15: [80], p_6: [40], p_3: [55] },
    commands: { activeHeads: 4, pingPong: 0, reverse: 0 }
  },
  {
    id: '03_multi_head_three',
    title: 'Multi-Head (3 Heads, activeHeads=7)',
    stimulusKey: 'multiPluck',
    params: { p_12: [360], p_13: [58], p_14: [55], p_15: [75], p_6: [35] },
    commands: { activeHeads: 7, pingPong: 0, reverse: 0 }
  },
  {
    id: '04_high_tape_age',
    title: 'High Tape Age (90)',
    stimulusKey: 'chords',
    params: { p_12: [340], p_13: [60], p_14: [60], p_15: [75], p_6: [90], p_20: [40], p_3: [50] },
    commands: { activeHeads: 4, pingPong: 0, reverse: 0 }
  },
  {
    id: '05_guitar_pluck_transients',
    title: 'Guitar Pluck Transients',
    stimulusKey: 'multiPluck',
    params: { p_12: [300], p_13: [62], p_14: [55], p_15: [75], p_6: [30] },
    commands: { activeHeads: 4, pingPong: 0, reverse: 0 }
  },
  {
    id: '06_sustained_chords',
    title: 'Sustained Chords',
    stimulusKey: 'chords',
    params: { p_12: [340], p_13: [62], p_14: [55], p_15: [75], p_6: [35] },
    commands: { activeHeads: 4, pingPong: 0, reverse: 0 }
  },
  {
    id: '07_reverse_feedback',
    title: 'Reverse + Feedback',
    stimulusKey: 'multiPluck',
    params: { p_12: [350], p_13: [65], p_14: [60], p_15: [80], p_6: [40] },
    commands: { activeHeads: 4, pingPong: 0, reverse: 1, reverseMode: 1 }
  },
  {
    id: '08_ping_pong',
    title: 'Ping-Pong Stereo',
    stimulusKey: 'multiPluck',
    params: { p_12: [300], p_13: [60], p_14: [55], p_15: [75], p_6: [35] },
    commands: { activeHeads: 7, pingPong: 1, reverse: 0 }
  },
  {
    id: '09_silence_after_stimulus',
    title: 'Silence Tail & Residual Noise',
    stimulusKey: 'singlePluck',
    singleHeadEchoMs: 280,
    params: { p_12: [280], p_13: [65], p_14: [60], p_15: [80], p_4: [30], p_6: [40] },
    commands: { activeHeads: 4, pingPong: 0, reverse: 0 }
  },
  {
    id: '10_control_automation',
    title: 'Control Automation Sweep',
    stimulusKey: 'mixed',
    params: { p_12: [300], p_14: [55], p_15: [75] },
    commands: { activeHeads: 4, pingPong: 0, reverse: 0 },
    automate: (base, t) => ({
      ...base,
      p_6: [20 + 65 * t],
      p_13: [35 + 40 * t],
      p_5: [35 + 40 * (1 - t)]
    })
  }
];

const runtimes = {};
const buildMetas = {};
for (const v of VARIANTS) {
  const meta = JSON.parse(await readFile(resolve(v.buildDir, 'hydra-build-info.json'), 'utf8'));
  if (meta.feedbackRouting !== 'PHASE72_CLASSIC_C') {
    throw new Error(`Expected PHASE72_CLASSIC_C for ${v.key}, got ${meta.feedbackRouting}`);
  }
  if (meta.repeatAgingMode !== v.expectedMode) {
    throw new Error(`Expected ${v.expectedMode} for ${v.key}, got ${meta.repeatAgingMode}`);
  }
  buildMetas[v.key] = meta;
  runtimes[v.key] = await loadProcessor(v.buildDir);
  await mkdir(resolve(outRoot, v.dir), { recursive: true });
}
await mkdir(resolve(outRoot, 'comparisons'), { recursive: true });

const manifestScenarios = [];
for (const sc of scenarios) {
  const stim = stimuli[sc.stimulusKey];
  const outputs = {};
  const files = {};
  const hashes = {};
  const metrics = {};

  const dryWav = encodeStereoWav(stim.left, stim.right);
  await writeFile(resolve(outRoot, 'comparisons', `${sc.id}_dry.wav`), dryWav);
  files.dry = `comparisons/${sc.id}_dry.wav`;
  hashes.dry = sha256(dryWav);

  for (const v of VARIANTS) {
    const out = await renderScenario(runtimes[v.key], stim, sc);
    outputs[v.key] = out;
    const wav = encodeStereoWav(out.left, out.right);
    const rel = `${v.dir}/${sc.id}.wav`;
    await writeFile(resolve(outRoot, rel), wav);
    files[v.key] = rel;
    hashes[v.key] = sha256(wav);
    metrics[v.key] = analyzeSignal(out.left, out.right, sc);
  }

  for (const v of ['c1', 'c2', 'c3']) {
    const diffL = new Float32Array(stim.left.length);
    const diffR = new Float32Array(stim.right.length);
    for (let i = 0; i < diffL.length; i++) {
      diffL[i] = outputs[v].left[i] - outputs.c0.left[i];
      diffR[i] = outputs[v].right[i] - outputs.c0.right[i];
    }
    const diffWav = encodeStereoWav(diffL, diffR);
    const diffRel = `comparisons/${sc.id}_diff_${v}_minus_c0.wav`;
    await writeFile(resolve(outRoot, diffRel), diffWav);
    files[`diff_${v}_minus_c0`] = diffRel;
    hashes[`diff_${v}_minus_c0`] = sha256(diffWav);
  }

  manifestScenarios.push({
    id: sc.id,
    title: sc.title,
    stimulus: sc.stimulusKey,
    params: sc.params,
    commands: sc.commands,
    normalization: 'none (unity gain, 16-bit PCM stereo WAV)',
    files,
    sha256: hashes,
    metrics
  });
}

const manifest = {
  generatedAt: new Date().toISOString(),
  gitCommit: execFileSync('git', ['rev-parse', 'HEAD'], { cwd: root, encoding: 'utf8' }).trim(),
  sampleRate: SAMPLE_RATE,
  format: 'PCM 16-bit stereo WAV',
  builds: buildMetas,
  scenarios: manifestScenarios
};

await writeFile(resolve(outRoot, 'manifest.json'), JSON.stringify(manifest, null, 2));

const spectralJson = {
  generatedAt: manifest.generatedAt,
  gitCommit: manifest.gitCommit,
  sampleRate: SAMPLE_RATE,
  format: manifest.format,
  scenarios: manifestScenarios.map(sc => ({
    id: sc.id,
    title: sc.title,
    metrics: sc.metrics
  }))
};
await writeFile(resolve(outRoot, 'spectral-analysis.json'), JSON.stringify(spectralJson, null, 2));

const reportLines = [
  '# Hydra Tape Engine — Phase 7.4A.1 Listening & Spectral Report',
  '',
  `- **Git Commit:** \`${manifest.gitCommit}\``,
  `- **Generated:** ${manifest.generatedAt}`,
  `- **Format:** 48,000 Hz, 16-bit PCM stereo WAV, unity gain (no independent normalization)`,
  '- **Status:** Pending human listening evaluation (`WEB_LISTENING_READY`)',
  '',
  '## How to Audition Locally (Windows / macOS / Linux)',
  '',
  '```powershell',
  'npx --yes serve .',
  '# or with Python:',
  'python -m http.server 8080',
  '```',
  '',
  'Open `http://localhost:8080/comparisons/index.html` (if serving inside `phase7-4a/`) or `http://localhost:8080/artifacts/phase7-4a/comparisons/index.html` (if serving from repo root).',
  '',
  '## Cross-Scenario Summary',
  '',
  '| Scenario | Variant | RMS | Peak | Crest (dB) | Centroid (Hz) | H/L Ratio (dB) | Tail RMS (dB) | Non-Finite |',
  '|---|---|---:|---:|---:|---:|---:|---:|---:|'
];
for (const sc of manifestScenarios) {
  for (const v of VARIANTS) {
    const m = sc.metrics[v.key];
    reportLines.push(
      `| \`${sc.id}\` | ${v.key.toUpperCase()} | ${m.rms.toFixed(6)} | ${m.peak.toFixed(6)} | ${m.crestDb.toFixed(2)} | ${m.centroidHz.toFixed(1)} | ${m.highLowRatioDb.toFixed(2)} | ${m.tailRmsDb.toFixed(2)} | ${m.nonFinite} |`
    );
  }
}
await writeFile(resolve(outRoot, 'listening-report.md'), reportLines.join('\n') + '\n');

const abHtml = `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8" />
<title>HYDRA — Repeat Aging Lab</title>
<style>
  :root { color-scheme: dark; }
  body { font-family: system-ui, -apple-system, sans-serif; background: #121417; color: #e8eaed; max-width: 780px; margin: 32px auto; padding: 0 18px; }
  h1 { font-size: 1.2rem; letter-spacing: 0.06em; margin-bottom: 4px; }
  .sub { color: #9aa0a6; font-size: 0.85rem; margin-bottom: 18px; }
  .panel { background: #1b1f24; border: 1px solid #2d333b; border-radius: 10px; padding: 18px; }
  .row { display: flex; gap: 10px; flex-wrap: wrap; align-items: center; margin-bottom: 14px; }
  label { font-size: 0.9rem; color: #9aa0a6; }
  select { background: #262b33; color: #e8eaed; border: 1px solid #3f4752; border-radius: 6px; padding: 7px 10px; font-size: 0.95rem; flex: 1; min-width: 240px; }
  button { background: #262b33; color: #e8eaed; border: 1px solid #3f4752; border-radius: 6px; padding: 8px 14px; cursor: pointer; font-size: 0.92rem; transition: background 0.1s; }
  button:hover:not(:disabled) { background: #313844; }
  button:disabled { opacity: 0.5; cursor: default; }
  button.active { background: #2563eb; border-color: #60a5fa; color: #fff; font-weight: 600; }
  button.toggle-on { background: #1d4ed8; border-color: #60a5fa; color: #fff; }
  .timeline-wrap { display: flex; align-items: center; gap: 10px; margin: 14px 0; }
  input[type="range"] { flex: 1; accent-color: #3b82f6; cursor: pointer; }
  .time { font-variant-numeric: tabular-nums; font-size: 0.85rem; color: #9aa0a6; min-width: 92px; text-align: right; }
  .status { display: flex; justify-content: space-between; align-items: center; font-size: 0.9rem; padding-top: 10px; border-top: 1px solid #2d333b; }
  .current { font-weight: 600; color: #93c5fd; }
  .meta { color: #9aa0a6; font-size: 0.8rem; }
</style>
</head>
<body>
<h1>HYDRA — REPEAT AGING LAB</h1>
<div class="sub">48 kHz / 16-bit PCM stereo WAV, unity gain (no normalization). Web Audio API sample-aligned A/B switching (8 ms crossfade).</div>

<div class="panel">
  <div class="row">
    <label for="scenarioSelect">Scenario:</label>
    <select id="scenarioSelect"></select>
  </div>

  <div class="row" id="variantButtons">
    <button data-variant="c0" class="active">C0 Original</button>
    <button data-variant="c1">C1 Wear</button>
    <button data-variant="c2">C2 Transient</button>
    <button data-variant="c3">C3 Combined</button>
    <button data-variant="dry">Dry</button>
  </div>

  <div class="row">
    <button id="playBtn" disabled>&#9654; Play</button>
    <button id="loopBtn" class="toggle-on">&#8635; Loop: ON</button>
    <span class="meta" id="loadStatus">Loading manifest...</span>
  </div>

  <div class="timeline-wrap">
    <input type="range" id="timeline" min="0" max="1" step="0.001" value="0" disabled />
    <span class="time" id="timeDisplay">0.00s / 0.00s</span>
  </div>

  <div class="status">
    <span class="current" id="currentLabel">Current: C0 — Classic C Baseline</span>
    <span class="meta">Keys: [1-4] C0-C3, [Space] Play/Pause</span>
  </div>
</div>

<script>
const VARIANT_LABELS = {
  c0: 'C0 — Classic C Baseline',
  c1: 'C1 — Dynamic Spectral Wear',
  c2: 'C2 — Transient Softening',
  c3: 'C3 — Combined Aging',
  dry: 'Dry — Input Stimulus'
};
const VARIANT_KEYS = ['c0', 'c1', 'c2', 'c3', 'dry'];
const XFADE_SEC = 0.008;

let manifest = null;
let audioCtx = null;
let buffers = {};
let sources = {};
let gains = {};
let activeVariant = 'c0';
let isPlaying = false;
let isLooping = true;
let durationSec = 0;
let offsetSec = 0;
let startCtxTime = 0;
let rafId = null;

const scenarioSelect = document.getElementById('scenarioSelect');
const playBtn = document.getElementById('playBtn');
const loopBtn = document.getElementById('loopBtn');
const timeline = document.getElementById('timeline');
const timeDisplay = document.getElementById('timeDisplay');
const currentLabel = document.getElementById('currentLabel');
const loadStatus = document.getElementById('loadStatus');
const variantButtons = Array.from(document.querySelectorAll('#variantButtons button'));

function ensureAudioContext() {
  if (!audioCtx) {
    const Ctx = window.AudioContext || window.webkitAudioContext;
    audioCtx = new Ctx();
  }
  if (audioCtx.state === 'suspended') {
    return audioCtx.resume();
  }
  return Promise.resolve();
}

function stopSources(preserveOffset) {
  if (preserveOffset && isPlaying && audioCtx && durationSec > 0) {
    const elapsed = audioCtx.currentTime - startCtxTime;
    offsetSec = isLooping ? ((offsetSec + elapsed) % durationSec) : Math.min(durationSec, offsetSec + elapsed);
  }
  for (const k of Object.keys(sources)) {
    try { sources[k].stop(); } catch (_) {}
    try { sources[k].disconnect(); } catch (_) {}
  }
  for (const k of Object.keys(gains)) {
    try { gains[k].disconnect(); } catch (_) {}
  }
  sources = {};
  gains = {};
  isPlaying = false;
  playBtn.innerHTML = '&#9654; Play';
  if (rafId) { cancelAnimationFrame(rafId); rafId = null; }
}

function startSources(fromOffset) {
  if (!audioCtx || !buffers.c0) return;
  stopSources(false);
  const when = audioCtx.currentTime + 0.02;
  offsetSec = Math.max(0, Math.min(fromOffset, Math.max(0, durationSec - 0.001)));
  startCtxTime = when;

  for (const k of VARIANT_KEYS) {
    if (!buffers[k]) continue;
    const src = audioCtx.createBufferSource();
    src.buffer = buffers[k];
    src.loop = isLooping;
    src.loopStart = 0;
    src.loopEnd = durationSec;

    const g = audioCtx.createGain();
    g.gain.setValueAtTime(k === activeVariant ? 1.0 : 0.0, when);

    src.connect(g);
    g.connect(audioCtx.destination);
    src.start(when, offsetSec);

    sources[k] = src;
    gains[k] = g;
  }

  if (sources.c0) {
    sources.c0.onended = () => {
      if (!isLooping && isPlaying) {
        isPlaying = false;
        offsetSec = 0;
        timeline.value = 0;
        playBtn.innerHTML = '&#9654; Play';
        updateTimeUi();
      }
    };
  }

  isPlaying = true;
  playBtn.innerHTML = '&#9646;&#9646; Pause';
  tickTimeline();
}

function setVariant(nextKey) {
  if (!VARIANT_LABELS[nextKey]) return;
  activeVariant = nextKey;
  variantButtons.forEach(btn => {
    btn.classList.toggle('active', btn.dataset.variant === nextKey);
  });
  currentLabel.textContent = 'Current: ' + VARIANT_LABELS[nextKey];
  if (audioCtx && isPlaying) {
    const now = audioCtx.currentTime;
    for (const k of VARIANT_KEYS) {
      const g = gains[k];
      if (!g) continue;
      g.gain.cancelScheduledValues(now);
      g.gain.setValueAtTime(g.gain.value, now);
      g.gain.linearRampToValueAtTime(k === nextKey ? 1.0 : 0.0, now + XFADE_SEC);
    }
  }
}

function currentPositionSec() {
  if (!isPlaying || !audioCtx || durationSec <= 0) return offsetSec;
  const elapsed = Math.max(0, audioCtx.currentTime - startCtxTime);
  const pos = offsetSec + elapsed;
  return isLooping ? (pos % durationSec) : Math.min(durationSec, pos);
}

function updateTimeUi() {
  const pos = currentPositionSec();
  if (durationSec > 0) {
    timeline.value = pos / durationSec;
  }
  timeDisplay.textContent = pos.toFixed(2) + 's / ' + durationSec.toFixed(2) + 's';
}

function tickTimeline() {
  updateTimeUi();
  if (isPlaying) {
    rafId = requestAnimationFrame(tickTimeline);
  }
}

async function loadScenario(index) {
  const wasPlaying = isPlaying;
  stopSources(false);
  offsetSec = 0;
  timeline.value = 0;
  timeline.disabled = true;
  playBtn.disabled = true;
  loadStatus.textContent = 'Preloading & decoding WAVs...';

  await ensureAudioContext();
  const sc = manifest.scenarios[index];
  const decoded = {};
  await Promise.all(VARIANT_KEYS.map(async (k) => {
    const resp = await fetch('../' + sc.files[k]);
    if (!resp.ok) throw new Error('Failed to fetch ' + sc.files[k]);
    const arr = await resp.arrayBuffer();
    decoded[k] = await audioCtx.decodeAudioData(arr);
  }));
  buffers = decoded;
  durationSec = buffers.c0.duration;
  timeline.disabled = false;
  playBtn.disabled = false;
  loadStatus.textContent = 'Ready (' + durationSec.toFixed(2) + 's)';
  updateTimeUi();
  if (wasPlaying) {
    startSources(0);
  }
}

variantButtons.forEach(btn => {
  btn.addEventListener('click', () => setVariant(btn.dataset.variant));
});

playBtn.addEventListener('click', async () => {
  await ensureAudioContext();
  if (isPlaying) {
    stopSources(true);
    updateTimeUi();
  } else {
    startSources(offsetSec);
  }
});

loopBtn.addEventListener('click', () => {
  isLooping = !isLooping;
  loopBtn.classList.toggle('toggle-on', isLooping);
  loopBtn.innerHTML = isLooping ? '&#8635; Loop: ON' : '&#8635; Loop: OFF';
  for (const k of Object.keys(sources)) {
    sources[k].loop = isLooping;
  }
});

timeline.addEventListener('input', () => {
  const target = parseFloat(timeline.value) * durationSec;
  offsetSec = target;
  if (isPlaying) {
    startSources(target);
  } else {
    updateTimeUi();
  }
});

scenarioSelect.addEventListener('change', () => {
  loadScenario(parseInt(scenarioSelect.value, 10)).catch(err => {
    loadStatus.textContent = 'Error: ' + err.message;
  });
});

window.addEventListener('keydown', (e) => {
  if (e.target.tagName === 'SELECT' || e.target.tagName === 'INPUT') return;
  if (e.code === 'Space') {
    e.preventDefault();
    if (!playBtn.disabled) playBtn.click();
  } else if (e.key === '1') setVariant('c0');
  else if (e.key === '2') setVariant('c1');
  else if (e.key === '3') setVariant('c2');
  else if (e.key === '4') setVariant('c3');
});

fetch('../manifest.json')
  .then(r => r.json())
  .then(m => {
    manifest = m;
    m.scenarios.forEach((sc, idx) => {
      const opt = document.createElement('option');
      opt.value = String(idx);
      opt.textContent = sc.title + ' (' + sc.id + ')';
      scenarioSelect.appendChild(opt);
    });
    loadStatus.textContent = 'Click Play or select scenario to decode';
    return loadScenario(0);
  })
  .catch(err => {
    loadStatus.textContent = 'Error loading manifest: ' + err.message;
  });
</script>
</body>
</html>
`;
await writeFile(resolve(outRoot, 'comparisons', 'index.html'), abHtml);
console.log(`Generated ${manifestScenarios.length} Phase 7.4A scenarios at ${outRoot}`);
