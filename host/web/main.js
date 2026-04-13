const PARAM = {
  flutterDepth: 0,
  wowDepth: 1,
  drive: 3,
  delayActive: 11,
  delayTimeMs: 12,
  feedback: 13,
  dryWet: 14,
  reset: 28
};

const statusEl = document.getElementById('status');
const player = document.getElementById('player');
const fileInput = document.getElementById('fileInput');
const startBtn = document.getElementById('startBtn');
const connectBtn = document.getElementById('connectBtn');
const bypassBtn = document.getElementById('bypassBtn');
const resetBtn = document.getElementById('resetBtn');
const offlineBtn = document.getElementById('offlineBtn');
const downloadLink = document.getElementById('downloadLink');

let context;
let source;
let fxNode;
let bypass = false;
let connected = false;
let currentFileArrayBuffer;

function setStatus(msg) {
  statusEl.textContent = msg;
}

function post(message) {
  if (fxNode) fxNode.port.postMessage(message);
}

async function ensureAudioGraph() {
  if (context) return;

  context = new AudioContext({ sampleRate: 48000 });
  await context.audioWorklet.addModule('./hydra-processor.js');
  fxNode = new AudioWorkletNode(context, 'hydra-processor', {
    numberOfInputs: 1,
    numberOfOutputs: 1,
    outputChannelCount: [2],
    processorOptions: {
      moduleUrl: './hydra_dsp.js',
      wasmUrl: './hydra_dsp.wasm'
    }
  });

  fxNode.port.onmessage = (event) => {
    if (event.data?.type === 'ready') {
      setStatus('WASM ready in AudioWorklet.');
      syncAllParams();
    }
  };
}

function connectGraph() {
  if (!source || !fxNode || !context) return;
  source.disconnect();
  fxNode.disconnect();
  if (connected) {
    source.connect(fxNode).connect(context.destination);
  } else {
    source.connect(context.destination);
  }
}

function syncAllParams(targetPort = fxNode?.port) {
  const map = [
    ['delayActive', (v) => (v.checked ? 1 : 0)],
    ['delayTimeMs', (v) => Number(v.value)],
    ['feedback', (v) => Number(v.value)],
    ['dryWet', (v) => Number(v.value)],
    ['drive', (v) => Number(v.value)],
    ['flutterDepth', (v) => Number(v.value)],
    ['wowDepth', (v) => Number(v.value)]
  ];

  map.forEach(([id, fn]) => {
    const el = document.getElementById(id);
    targetPort?.postMessage({ type: 'param', paramId: PARAM[id], value: fn(el) });
  });
}

fileInput.addEventListener('change', async (event) => {
  const file = event.target.files?.[0];
  if (!file) return;
  const url = URL.createObjectURL(file);
  player.src = url;
  currentFileArrayBuffer = await file.arrayBuffer();
  setStatus(`Loaded ${file.name}`);
});

startBtn.addEventListener('click', async () => {
  await ensureAudioGraph();
  if (context.state !== 'running') await context.resume();

  if (!source) {
    source = context.createMediaElementSource(player);
  }
  connectGraph();
  setStatus('Audio context running. Press play on the audio element.');
});

connectBtn.addEventListener('click', () => {
  connected = !connected;
  connectBtn.textContent = connected ? 'Disconnect FX' : 'Connect FX';
  connectGraph();
});

bypassBtn.addEventListener('click', () => {
  bypass = !bypass;
  bypassBtn.textContent = `Bypass: ${bypass ? 'ON' : 'OFF'}`;
  post({ type: 'bypass', enabled: bypass });
});

resetBtn.addEventListener('click', () => {
  post({ type: 'reset' });
  post({ type: 'param', paramId: PARAM.reset, value: 1 });
});

['delayActive', 'delayTimeMs', 'feedback', 'dryWet', 'drive', 'flutterDepth', 'wowDepth'].forEach((id) => {
  const el = document.getElementById(id);
  el.addEventListener('input', () => {
    const value = el.type === 'checkbox' ? (el.checked ? 1 : 0) : Number(el.value);
    post({ type: 'param', paramId: PARAM[id], value });
  });
});

function encodeWav(stereo, sampleRate) {
  const length = stereo[0].length;
  const bytes = 44 + length * 4;
  const buffer = new ArrayBuffer(bytes);
  const view = new DataView(buffer);

  const writeStr = (offset, str) => [...str].forEach((c, i) => view.setUint8(offset + i, c.charCodeAt(0)));
  writeStr(0, 'RIFF');
  view.setUint32(4, 36 + length * 4, true);
  writeStr(8, 'WAVE');
  writeStr(12, 'fmt ');
  view.setUint32(16, 16, true);
  view.setUint16(20, 1, true);
  view.setUint16(22, 2, true);
  view.setUint32(24, sampleRate, true);
  view.setUint32(28, sampleRate * 4, true);
  view.setUint16(32, 4, true);
  view.setUint16(34, 16, true);
  writeStr(36, 'data');
  view.setUint32(40, length * 4, true);

  let o = 44;
  for (let i = 0; i < length; i++) {
    const l = Math.max(-1, Math.min(1, stereo[0][i]));
    const r = Math.max(-1, Math.min(1, stereo[1][i]));
    view.setInt16(o, l * 32767, true);
    view.setInt16(o + 2, r * 32767, true);
    o += 4;
  }
  return new Blob([buffer], { type: 'audio/wav' });
}

offlineBtn.addEventListener('click', async () => {
  if (!currentFileArrayBuffer) {
    setStatus('Load an audio file first.');
    return;
  }

  setStatus('Offline render started...');
  const decodeCtx = new AudioContext({ sampleRate: 48000 });
  const decoded = await decodeCtx.decodeAudioData(currentFileArrayBuffer.slice(0));
  await decodeCtx.close();

  const offline = new OfflineAudioContext(2, decoded.length, decoded.sampleRate);
  await offline.audioWorklet.addModule('./hydra-processor.js');

  const node = new AudioWorkletNode(offline, 'hydra-processor', {
    numberOfInputs: 1,
    numberOfOutputs: 1,
    outputChannelCount: [2],
    processorOptions: {
      moduleUrl: './hydra_dsp.js',
      wasmUrl: './hydra_dsp.wasm'
    }
  });

  node.port.postMessage({ type: 'bypass', enabled: false });
  syncAllParams(node.port);

  const src = offline.createBufferSource();
  src.buffer = decoded;
  src.connect(node).connect(offline.destination);
  src.start();

  const rendered = await offline.startRendering();
  const wav = encodeWav([rendered.getChannelData(0), rendered.getChannelData(1)], rendered.sampleRate);
  const url = URL.createObjectURL(wav);
  downloadLink.href = url;
  downloadLink.style.display = 'inline';
  downloadLink.textContent = 'Download offline WAV';
  setStatus('Offline render complete.');
});

setStatus('Ready. Build WASM and click Start Audio.');
