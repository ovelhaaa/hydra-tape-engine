import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

const root = resolve(import.meta.dirname, '..');
const classicBuild = resolve(root, process.env.HYDRA_WEB_CLASSIC_BUILD || 'build-web/web');
const legacyBuild = resolve(root, process.env.HYDRA_WEB_LEGACY_BUILD || 'build-web-legacy/web');
const outRoot = resolve(root, process.env.HYDRA_LISTENING_OUT || 'artifacts/phase73-web-listening');

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

function stats(left, right) {
  let sumSq = 0;
  let peak = 0;
  let sumSqL = 0;
  let sumSqR = 0;
  for (let i = 0; i < left.length; i++) {
    const l = left[i];
    const r = right[i];
    sumSqL += l * l;
    sumSqR += r * r;
    sumSq += l * l + r * r;
    peak = Math.max(peak, Math.abs(l), Math.abs(r));
  }
  const n = Math.max(1, left.length);
  return {
    rms: Math.sqrt(sumSq / (2 * n)),
    rmsL: Math.sqrt(sumSqL / n),
    rmsR: Math.sqrt(sumSqR / n),
    peak
  };
}

// Original deterministic stimuli (no commercial audio)
function generateTransientStimulus(durationSec = 4.0) {
  const frames = Math.floor(SAMPLE_RATE * durationSec);
  const left = new Float32Array(frames);
  const right = new Float32Array(frames);
  const hits = [
    { t: 0.05, f: 440, pan: 0.45 },
    { t: 0.45, f: 587.33, pan: 0.55 },
    { t: 0.85, f: 659.25, pan: 0.50 },
    { t: 1.25, f: 880, pan: 0.48 }
  ];
  for (const hit of hits) {
    const start = Math.floor(hit.t * SAMPLE_RATE);
    const len = Math.floor(0.14 * SAMPLE_RATE);
    for (let k = 0; k < len && start + k < frames; k++) {
      const env = Math.exp(-k / (0.028 * SAMPLE_RATE)) * (1 - Math.exp(-k / 48));
      const s = 0.55 * env * (
        Math.sin((2 * Math.PI * hit.f * k) / SAMPLE_RATE) +
        0.35 * Math.sin((2 * Math.PI * hit.f * 2.0 * k) / SAMPLE_RATE) +
        0.15 * Math.sin((2 * Math.PI * hit.f * 3.0 * k) / SAMPLE_RATE)
      );
      left[start + k] += s * (1 - hit.pan * 0.2);
      right[start + k] += s * (0.8 + hit.pan * 0.2);
    }
  }
  return { left, right };
}

function generateHarmonicStimulus(durationSec = 4.0) {
  const frames = Math.floor(SAMPLE_RATE * durationSec);
  const left = new Float32Array(frames);
  const right = new Float32Array(frames);
  // Two sustained chords with silence tail for decay observation
  const chords = [
    { t0: 0.05, t1: 1.15, freqs: [220, 277.18, 329.63, 415.30] },
    { t0: 1.25, t1: 2.35, freqs: [196, 246.94, 293.66, 369.99] }
  ];
  for (const ch of chords) {
    const i0 = Math.floor(ch.t0 * SAMPLE_RATE);
    const i1 = Math.floor(ch.t1 * SAMPLE_RATE);
    for (let i = i0; i < i1 && i < frames; i++) {
      const tRel = (i - i0) / SAMPLE_RATE;
      const dur = ch.t1 - ch.t0;
      const attack = Math.min(1, tRel / 0.04);
      const release = Math.min(1, (dur - tRel) / 0.12);
      const env = attack * release;
      let sL = 0;
      let sR = 0;
      ch.freqs.forEach((f, idx) => {
        const tone = Math.sin((2 * Math.PI * f * i) / SAMPLE_RATE) + 0.25 * Math.sin((2 * Math.PI * f * 2 * i) / SAMPLE_RATE);
        const pan = 0.35 + (idx / (ch.freqs.length - 1)) * 0.3;
        sL += tone * (1 - pan) * 0.16;
        sR += tone * pan * 0.16;
      });
      left[i] += sL * env;
      right[i] += sR * env;
    }
  }
  return { left, right };
}

async function loadProcessor(buildDir) {
  if (globalThis.__hydraShared?.runtimes instanceof Map) {
    globalThis.__hydraShared.runtimes.clear();
  }
  let Processor;
  globalThis.sampleRate = SAMPLE_RATE;
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
  const worklet = new Processor({ processorOptions: { wasmBinary } });
  const ready = await worklet.readyMessage;
  if (ready.type !== 'ready') throw new Error(`Worklet init failed: ${ready.message}`);
  try {
    worklet.port.onmessage({ data: { type: 'command', command: 'delayActive', value: 1 } });
    for (const [command, value] of Object.entries(scenario.commands || {})) {
      worklet.port.onmessage({ data: { type: 'command', command, value } });
    }
    const audioParams = {
      p_0: [12], p_1: [8], p_2: [2], p_3: [35], p_4: [0],
      p_5: [50], p_6: [35], p_7: [30], p_8: [10], p_9: [6.0], p_10: [0.8],
      p_12: [260], p_13: [50], p_14: [55], p_15: [65], p_17: [120], p_20: [50],
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
      inL.fill(0);
      inR.fill(0);
      inL.set(stimulus.left.subarray(start, start + count));
      inR.set(stimulus.right.subarray(start, start + count));
      worklet.process([[inL, inR]], [[outL, outR]], audioParams);
      outAllL.set(outL.subarray(0, count), start);
      outAllR.set(outR.subarray(0, count), start);
    }
    return { left: outAllL, right: outAllR };
  } finally {
    worklet.port.onmessage({ data: { type: 'destroy' } });
  }
}

const classicMeta = JSON.parse(await readFile(resolve(classicBuild, 'hydra-build-info.json'), 'utf8'));
const legacyMeta = JSON.parse(await readFile(resolve(legacyBuild, 'hydra-build-info.json'), 'utf8'));
if (classicMeta.feedbackRouting !== 'PHASE72_CLASSIC_C') throw new Error(`Expected Classic C build, got ${classicMeta.feedbackRouting}`);
if (legacyMeta.feedbackRouting !== 'LEGACY_A') throw new Error(`Expected Legacy A build, got ${legacyMeta.feedbackRouting}`);

const classicRuntime = await loadProcessor(classicBuild);
const legacyRuntime = await loadProcessor(legacyBuild);

await mkdir(resolve(outRoot, 'classic-c'), { recursive: true });
await mkdir(resolve(outRoot, 'legacy-a'), { recursive: true });
await mkdir(resolve(outRoot, 'comparison'), { recursive: true });

const stimuli = {
  transient: generateTransientStimulus(4.0),
  harmonic: generateHarmonicStimulus(4.0)
};

const scenarios = [
  {
    id: '01_conventional_delay',
    title: 'Delay convencional',
    objective: 'Musicalidade e filtragem progressiva das repetições',
    stimulusKey: 'transient',
    params: { p_12: [320], p_13: [55], p_14: [55], p_15: [70], p_6: [35], p_20: [50] },
    commands: { activeHeads: 1, pingPong: 0, reverse: 0 }
  },
  {
    id: '02_multihead_pingpong',
    title: 'Multi-Head + Ping-Pong',
    objective: 'Separação espacial e dinâmica das cabeças',
    stimulusKey: 'transient',
    params: { p_12: [280], p_13: [60], p_14: [60], p_15: [75], p_6: [30], p_20: [55] },
    commands: { activeHeads: 4, pingPong: 1, reverse: 0 }
  },
  {
    id: '03_high_tape_age',
    title: 'Tape Age elevado',
    objective: 'Escurecimento e degradação sucessiva',
    stimulusKey: 'harmonic',
    params: { p_12: [360], p_13: [65], p_14: [65], p_15: [75], p_6: [92], p_20: [35], p_3: [55] },
    commands: { activeHeads: 2, pingPong: 0, reverse: 0 }
  },
  {
    id: '04_reverse_feedback',
    title: 'Reverse com feedback',
    objective: 'Evolução das repetições reversas',
    stimulusKey: 'transient',
    params: { p_12: [350], p_13: [68], p_14: [65], p_15: [80], p_6: [45], p_17: [120] },
    commands: { activeHeads: 2, pingPong: 0, reverse: 1, reverseMode: 1 }
  },
  {
    id: '05_high_feedback',
    title: 'Feedback elevado',
    objective: 'Estabilidade, saturação e cauda longa',
    stimulusKey: 'harmonic',
    params: { p_12: [240], p_13: [88], p_14: [60], p_15: [75], p_6: [50], p_3: [60] },
    commands: { activeHeads: 3, pingPong: 1, reverse: 0 }
  }
];

const manifestScenarios = [];
for (const sc of scenarios) {
  const stim = stimuli[sc.stimulusKey];
  const outC = await renderScenario(classicRuntime, stim, sc);
  const outA = await renderScenario(legacyRuntime, stim, sc);

  const diffL = new Float32Array(stim.left.length);
  const diffR = new Float32Array(stim.right.length);
  for (let i = 0; i < diffL.length; i++) {
    diffL[i] = outA.left[i] - outC.left[i];
    diffR[i] = outA.right[i] - outC.right[i];
  }

  const dryWav = encodeStereoWav(stim.left, stim.right);
  const cWav = encodeStereoWav(outC.left, outC.right);
  const aWav = encodeStereoWav(outA.left, outA.right);
  const diffWav = encodeStereoWav(diffL, diffR);

  await writeFile(resolve(outRoot, 'comparison', `${sc.id}_dry.wav`), dryWav);
  await writeFile(resolve(outRoot, 'classic-c', `${sc.id}.wav`), cWav);
  await writeFile(resolve(outRoot, 'legacy-a', `${sc.id}.wav`), aWav);
  await writeFile(resolve(outRoot, 'comparison', `${sc.id}_diff_A_minus_C.wav`), diffWav);

  const stDry = stats(stim.left, stim.right);
  const stC = stats(outC.left, outC.right);
  const stA = stats(outA.left, outA.right);
  const stDiff = stats(diffL, diffR);

  manifestScenarios.push({
    id: sc.id,
    title: sc.title,
    objective: sc.objective,
    stimulus: sc.stimulusKey,
    params: sc.params,
    commands: sc.commands,
    normalization: 'none (unity gain, identical 16-bit PCM WAV encoding)',
    files: {
      dry: `comparison/${sc.id}_dry.wav`,
      classicC: `classic-c/${sc.id}.wav`,
      legacyA: `legacy-a/${sc.id}.wav`,
      diffAMinusC: `comparison/${sc.id}_diff_A_minus_C.wav`
    },
    sha256: {
      dry: sha256(dryWav),
      classicC: sha256(cWav),
      legacyA: sha256(aWav),
      diffAMinusC: sha256(diffWav)
    },
    metrics: {
      dry: stDry,
      classicC: stC,
      legacyA: stA,
      diffAMinusC: stDiff
    }
  });
}

const manifest = {
  generatedAt: new Date().toISOString(),
  gitCommit: execFileSync('git', ['rev-parse', 'HEAD'], { cwd: root, encoding: 'utf8' }).trim(),
  sampleRate: SAMPLE_RATE,
  format: 'PCM 16-bit stereo WAV',
  classicCBuild: classicMeta,
  legacyABuild: legacyMeta,
  scenarios: manifestScenarios
};

await writeFile(resolve(outRoot, 'manifest.json'), JSON.stringify(manifest, null, 2));

const reportLines = [
  '# Phase 7.3 Web Listening Comparison — Legacy A vs Classic C',
  '',
  `- **Commit:** \`${manifest.gitCommit}\``,
  `- **Sample Rate:** ${SAMPLE_RATE} Hz | **Format:** ${manifest.format}`,
  `- **Normalization:** None (unity gain; no independent A/C normalization)`,
  `- **Classic C Build:** \`${classicMeta.feedbackRouting}\` (Emscripten ${classicMeta.emscriptenVersion})`,
  `- **Legacy A Build:** \`${legacyMeta.feedbackRouting}\` (Emscripten ${legacyMeta.emscriptenVersion})`,
  '',
  '| Scenario | Stimulus | Classic C RMS / Peak | Legacy A RMS / Peak | Diff (A-C) RMS |',
  '|---|---|---|---|---|'
];

for (const s of manifestScenarios) {
  const m = s.metrics;
  reportLines.push(
    `| ${s.title} (\`${s.id}\`) | ${s.stimulus} | ${m.classicC.rms.toFixed(4)} / ${m.classicC.peak.toFixed(4)} | ${m.legacyA.rms.toFixed(4)} / ${m.legacyA.peak.toFixed(4)} | ${m.diffAMinusC.rms.toFixed(4)} |`
  );
}
reportLines.push('');
await writeFile(resolve(outRoot, 'listening-report.md'), reportLines.join('\n'));
console.log(`Generated ${manifestScenarios.length} A/C listening scenarios at ${outRoot}`);
