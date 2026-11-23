import crypto from 'node:crypto';
import fs from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const ROOT = path.resolve(__dirname, '..');
const WEB_BUILD = path.resolve(process.env.HYDRA_WEB_BUILD || path.join(ROOT, 'build-web', 'web'));
const OUT_DIR = path.resolve(process.env.HYDRA_LISTENING_OUT || path.join(ROOT, 'artifacts', 'phase7-4c'));
const AUDIO_DIR = path.join(OUT_DIR, 'audio');
const COMP_DIR = path.join(OUT_DIR, 'comparisons');
const PARITY_DIR = path.join(OUT_DIR, 'parity');

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
  const notes = [196.0, 246.94, 293.66, 392.0, 493.88, 392.0];
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

await fs.mkdir(AUDIO_DIR, { recursive: true });
await fs.mkdir(COMP_DIR, { recursive: true });
await fs.mkdir(PARITY_DIR, { recursive: true });

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

const NOMINAL_CONTROLS = {
  sampleRate: 48000,
  blockSize: 128,
  delayActive: 1,
  delayTimeMs: 320,
  feedback: 55,
  dryWet: 60,
  delayWet: 70,
  activeHeads: 4,
  wowDepth: 16,
  wowRate: 0.8,
  flutterDepth: 20,
  flutterRate: 6.0,
  tapeAge: 55,
  motionAmount: 100,
  stereoMotion: 100,
  advancedControls: 50
};

function renderWasm(input, modeSchedule, feedback = NOMINAL_CONTROLS.feedback) {
  const sr = NOMINAL_CONTROLS.sampleRate;
  const block = NOMINAL_CONTROLS.blockSize;
  const hPtr = mod._malloc(4);
  api.create(sr, 2000, 4000, hPtr);
  const h = mod.HEAP32[hPtr >> 2];
  mod._free(hPtr);
  api.prepare(h, block, 2);

  api.setParam(h, 11, NOMINAL_CONTROLS.delayActive);
  api.setParam(h, 12, NOMINAL_CONTROLS.delayTimeMs);
  api.setParam(h, 13, feedback);
  api.setParam(h, 14, NOMINAL_CONTROLS.dryWet);
  api.setParam(h, 15, NOMINAL_CONTROLS.delayWet);
  api.setParam(h, 16, NOMINAL_CONTROLS.activeHeads);
  api.setParam(h, 1, NOMINAL_CONTROLS.wowDepth);
  api.setParam(h, 10, NOMINAL_CONTROLS.wowRate);
  api.setParam(h, 0, NOMINAL_CONTROLS.flutterDepth);
  api.setParam(h, 9, NOMINAL_CONTROLS.flutterRate);
  api.setParam(h, 6, NOMINAL_CONTROLS.tapeAge);
  api.setParam(h, 38, modeSchedule[0].mode);
  api.setParam(h, 39, NOMINAL_CONTROLS.motionAmount);
  api.setParam(h, 40, NOMINAL_CONTROLS.stereoMotion);
  for (let id = 41; id <= 46; id++) api.setParam(h, id, NOMINAL_CONTROLS.advancedControls);
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
  { id: 'classic', name: 'M0 — Classic', mode: 0 },
  { id: 'motor', name: 'M1 — Motor Drift', mode: 1 },
  { id: 'capstan', name: 'M2 — Capstan Flutter', mode: 2 },
  { id: 'worn', name: 'M3 — Worn Transport', mode: 3 }
];

const renders = {};
const metrics = { controls: NOMINAL_CONTROLS, modes: {} };
for (const m of modes) {
  const { resL, resR } = renderWasm(dry, [{ block: 0, mode: m.mode }]);
  renders[m.id] = { resL, resR };
  metrics.modes[m.id] = computeStats(resL, resR);
  await fs.writeFile(path.join(AUDIO_DIR, `${m.id}.wav`), encodeWavStereo(resL, resR));
}

// Runtime switching under high feedback (85%)
const switchRes = renderWasm(dry, [
  { block: 0, mode: 0 },
  { block: 375, mode: 1 },
  { block: 750, mode: 2 },
  { block: 1125, mode: 3 },
  { block: 1350, mode: 0 }
], 85);
await fs.writeFile(path.join(AUDIO_DIR, 'runtime-switch.wav'), encodeWavStereo(switchRes.resL, switchRes.resR));
metrics.runtime_switch = computeStats(switchRes.resL, switchRes.resR);

metrics.diff_vs_classic = {
  motor_rms_diff: rmsDiff(renders.classic.resL, renders.motor.resL),
  capstan_rms_diff: rmsDiff(renders.classic.resL, renders.capstan.resL),
  worn_rms_diff: rmsDiff(renders.classic.resL, renders.worn.resL)
};

// Merge parity metrics if generated by verify_phase74c_equivalence.py
try {
  const parityRaw = await fs.readFile(path.join(OUT_DIR, 'parity-metrics.json'), 'utf8');
  metrics.equivalence = JSON.parse(parityRaw);
} catch {
  // Optional when run standalone without verify_phase74c_equivalence.py
}

const comparisonHtml = `<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>Hydra Phase 7.4C — Unified Motion Listening Lab</title>
  <style>
    :root { color-scheme: dark; --bg: #0f172a; --panel: #1e293b; --border: #334155; --text: #e2e8f0; --muted: #94a3b8; --accent: #38bdf8; }
    body { margin: 0; font-family: system-ui, -apple-system, sans-serif; background: var(--bg); color: var(--text); padding: 1.5rem; line-height: 1.45; }
    .wrap { max-width: 960px; margin: 0 auto; display: grid; gap: 1rem; }
    .card { background: var(--panel); border: 1px solid var(--border); border-radius: 10px; padding: 1rem 1.25rem; }
    h1 { margin: 0 0 0.25rem; font-size: 1.35rem; }
    h2 { margin: 0 0 0.6rem; font-size: 1rem; color: var(--accent); }
    .row { display: flex; flex-wrap: wrap; gap: 0.5rem; align-items: center; }
    button { background: #0f172a; color: var(--text); border: 1px solid var(--border); border-radius: 6px; padding: 0.5rem 0.9rem; cursor: pointer; font-weight: 600; }
    button.active { background: rgba(56, 189, 248, 0.2); border-color: var(--accent); color: #fff; }
    button:hover { border-color: var(--accent); }
    .mono { font-family: ui-monospace, SFMono-Regular, monospace; font-size: 0.82rem; color: var(--muted); }
    audio { width: 100%; margin-top: 0.4rem; }
    .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(260px, 1fr)); gap: 0.75rem; }
  </style>
</head>
<body>
  <div class="wrap">
    <div class="card">
      <h1>Hydra Phase 7.4C — Unified Motion Listening Lab</h1>
      <div class="mono">Single WASM binary · Unnormalized raw WAVs · 48 kHz stereo · Nominal: Motion=100, Stereo=100, Adv=50, Delay=320 ms, FB=55%, Wow=16, Flutter=20, Age=55</div>
    </div>

    <div class="card">
      <h2>1. Synchronized 4-Engine Comparison</h2>
      <div class="row" style="margin-bottom: 0.75rem;">
        <button id="playSyncBtn" type="button">Play Synchronized</button>
        <button id="stopSyncBtn" type="button">Stop</button>
        <label class="mono"><input id="loopToggle" type="checkbox" checked /> Loop</label>
        <label class="mono"><input id="blindToggle" type="checkbox" /> Blind A/B/C/D</label>
        <button id="revealBtn" type="button">Reveal Labels</button>
      </div>
      <div class="row" id="modeButtons"></div>
      <p class="mono" id="syncStatus">Ready. Serve this folder over HTTP (e.g. python -m http.server 8740).</p>
    </div>

    <div class="card">
      <h2>2. Runtime Switching Audition (150 ms Transitions @ 85% Feedback)</h2>
      <div class="mono">Schedule: 0.0s Classic → 1.0s Motor Drift → 2.0s Capstan Flutter → 3.0s Worn Transport → 3.6s Classic</div>
      <audio controls loop src="../audio/runtime-switch.wav"></audio>
    </div>

    <div class="card">
      <h2>3. Phase 7.4B Reference vs 7.4C Unified Parity</h2>
      <div class="grid">
        <div><div class="mono">M1 Reference (7.4B)</div><audio controls src="../parity/m1-reference.wav"></audio></div>
        <div><div class="mono">M1 Unified (7.4C)</div><audio controls src="../parity/m1-unified.wav"></audio></div>
        <div><div class="mono">M2 Reference (7.4B)</div><audio controls src="../parity/m2-reference.wav"></audio></div>
        <div><div class="mono">M2 Unified (7.4C)</div><audio controls src="../parity/m2-unified.wav"></audio></div>
        <div><div class="mono">M3 Reference (7.4B)</div><audio controls src="../parity/m3-reference.wav"></audio></div>
        <div><div class="mono">M3 Unified (7.4C)</div><audio controls src="../parity/m3-unified.wav"></audio></div>
      </div>
    </div>
  </div>
  <script>
    const TRACKS = [
      { id: 'classic', label: 'M0 — Classic', url: '../audio/classic.wav' },
      { id: 'motor', label: 'M1 — Motor Drift', url: '../audio/motor.wav' },
      { id: 'capstan', label: 'M2 — Capstan Flutter', url: '../audio/capstan.wav' },
      { id: 'worn', label: 'M3 — Worn Transport', url: '../audio/worn.wav' }
    ];
    let order = [0, 1, 2, 3];
    let activeSlot = 0;
    let blind = false;
    let revealed = false;
    let ctx = null;
    let buffers = null;
    let sources = [];
    let gains = [];

    const modeButtonsEl = document.getElementById('modeButtons');
    const syncStatusEl = document.getElementById('syncStatus');
    const loopToggle = document.getElementById('loopToggle');
    const blindToggle = document.getElementById('blindToggle');

    function renderButtons() {
      modeButtonsEl.innerHTML = '';
      order.forEach((trackIdx, slot) => {
        const btn = document.createElement('button');
        btn.type = 'button';
        btn.className = slot === activeSlot ? 'active' : '';
        const blindName = 'Option ' + String.fromCharCode(65 + slot);
        btn.textContent = (blind && !revealed) ? blindName : (blind ? blindName + ' (' + TRACKS[trackIdx].label + ')' : TRACKS[trackIdx].label);
        btn.onclick = () => selectSlot(slot);
        modeButtonsEl.appendChild(btn);
      });
    }

    function selectSlot(slot) {
      activeSlot = slot;
      if (ctx && gains.length === 4) {
        const now = ctx.currentTime;
        gains.forEach((g, idx) => g.gain.setTargetAtTime(idx === activeSlot ? 1 : 0, now, 0.012));
      }
      renderButtons();
    }

    function stopPlayback() {
      sources.forEach((s) => { try { s.stop(); } catch {} });
      sources = [];
      gains = [];
    }

    async function startPlayback() {
      if (!ctx) ctx = new AudioContext();
      if (ctx.state === 'suspended') await ctx.resume();
      if (!buffers) {
        syncStatusEl.textContent = 'Loading 4 WAV tracks...';
        buffers = await Promise.all(TRACKS.map(async (t) => {
          const res = await fetch(t.url);
          return ctx.decodeAudioData(await res.arrayBuffer());
        }));
      }
      stopPlayback();
      const startAt = ctx.currentTime + 0.05;
      order.forEach((trackIdx, slot) => {
        const src = ctx.createBufferSource();
        src.buffer = buffers[trackIdx];
        src.loop = loopToggle.checked;
        const g = ctx.createGain();
        g.gain.value = slot === activeSlot ? 1 : 0;
        src.connect(g).connect(ctx.destination);
        src.start(startAt);
        sources.push(src);
        gains.push(g);
      });
      syncStatusEl.textContent = 'Playing synchronized comparison (click modes to switch seamlessly).';
    }

    document.getElementById('playSyncBtn').onclick = () => startPlayback().catch((e) => { syncStatusEl.textContent = String(e); });
    document.getElementById('stopSyncBtn').onclick = () => { stopPlayback(); syncStatusEl.textContent = 'Stopped.'; };
    blindToggle.onchange = () => {
      blind = blindToggle.checked;
      revealed = false;
      order = [0, 1, 2, 3];
      if (blind) order.sort(() => Math.random() - 0.5);
      renderButtons();
    };
    document.getElementById('revealBtn').onclick = () => { revealed = true; renderButtons(); };
    renderButtons();
  </script>
</body>
</html>
`;

await fs.writeFile(path.join(COMP_DIR, 'index.html'), comparisonHtml, 'utf8');

const readmeMd = `# Hydra Phase 7.4C — Unified Motion Listening Package

Single WASM build containing all four tape motion engines:
- **M0 — Classic** (\`audio/classic.wav\`)
- **M1 — Motor Drift** (\`audio/motor.wav\`)
- **M2 — Capstan Flutter** (\`audio/capstan.wav\`)
- **M3 — Worn Transport** (\`audio/worn.wav\`)
- **Runtime Switching (85% Feedback)** (\`audio/runtime-switch.wav\`)

## Local Audition on Windows

From the extracted \`phase7-4c/\` directory:

\`\`\`powershell
python -m http.server 8740
\`\`\`

Then open \`http://127.0.0.1:8740/comparisons/index.html\` in Chrome or Edge.
`;
await fs.writeFile(path.join(OUT_DIR, 'README.md'), readmeMd, 'utf8');

const reportMd = `# Phase 7.4C Unified Motion Listening Report

- **Sample Rate**: ${NOMINAL_CONTROLS.sampleRate} Hz
- **Block Size**: ${NOMINAL_CONTROLS.blockSize} frames
- **Normalization**: None (raw unnormalized outputs preserved)
- **Nominal Controls**: Motion Amount=${NOMINAL_CONTROLS.motionAmount}, Stereo Motion=${NOMINAL_CONTROLS.stereoMotion}, Advanced=${NOMINAL_CONTROLS.advancedControls}, Delay=${NOMINAL_CONTROLS.delayTimeMs} ms, Feedback=${NOMINAL_CONTROLS.feedback}%, Wow=${NOMINAL_CONTROLS.wowDepth}, Flutter=${NOMINAL_CONTROLS.flutterDepth}, Age=${NOMINAL_CONTROLS.tapeAge}

## Audio Metrics

\`\`\`json
${JSON.stringify(metrics, null, 2)}
\`\`\`
`;
await fs.writeFile(path.join(OUT_DIR, 'listening-report.md'), reportMd, 'utf8');
await fs.writeFile(path.join(OUT_DIR, 'metrics.json'), JSON.stringify(metrics, null, 2), 'utf8');

async function walkFiles(dir, base = dir) {
  const entries = await fs.readdir(dir, { withFileTypes: true });
  const files = [];
  for (const e of entries) {
    const full = path.join(dir, e.name);
    if (e.isDirectory()) {
      files.push(...(await walkFiles(full, base)));
    } else if (e.name !== 'manifest.json') {
      const data = await fs.readFile(full);
      const sha256 = crypto.createHash('sha256').update(data).digest('hex');
      files.push({
        path: path.relative(base, full).replace(/\\/g, '/'),
        bytes: data.length,
        sha256
      });
    }
  }
  return files.sort((a, b) => a.path.localeCompare(b.path));
}

const manifest = {
  phase: '7.4C.1',
  artifact: 'hydra-phase74c-unified-motion-listening',
  generatedAt: new Date().toISOString(),
  controls: NOMINAL_CONTROLS,
  files: await walkFiles(OUT_DIR)
};
await fs.writeFile(path.join(OUT_DIR, 'manifest.json'), JSON.stringify(manifest, null, 2), 'utf8');
console.log(JSON.stringify({ outDir: OUT_DIR, fileCount: manifest.files.length, metrics }, null, 2));
