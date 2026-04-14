import { createTransportState } from './transportState.js';
import { createTransportController } from './transportController.js';

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
const CONTINUOUS_PARAM_IDS = new Set([
  'delayTimeMs',
  'feedback',
  'dryWet',
  'drive',
  'flutterDepth',
  'wowDepth'
]);
const DEBUG_ROUNDTRIP = false;

const statusEl = document.getElementById('status');
const player = document.getElementById('player');
const fileInput = document.getElementById('fileInput');
const startBtn = document.getElementById('startBtn');
const playBtn = document.getElementById('playBtn');
const stopBtn = document.getElementById('stopBtn');
const repeatBtn = document.getElementById('repeatBtn');
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
const uiState = {};
const engineState = {};

const transportState = createTransportState();
createTransportController({ player, transportState, setStatus });

function updateRepeatUI({ isRepeatEnabled }) {
  repeatBtn.textContent = isRepeatEnabled ? 'Repeat ON' : 'Repeat OFF';
  repeatBtn.classList.toggle('repeat-toggle--active', isRepeatEnabled);
  repeatBtn.setAttribute('aria-pressed', String(isRepeatEnabled));

}

updateRepeatUI(transportState.getState());
transportState.subscribe(updateRepeatUI);

function setStatus(msg) {
  statusEl.textContent = msg;
}

function post(message) {
  if (fxNode) fxNode.port.postMessage(message);
}

function getAudioParamName(id) {
  return `p_${id}`;
}

function setContinuousParam(id, value, node = fxNode) {
  if (!node) return;
  const param = node.parameters.get(getAudioParamName(id));
  if (!param) return;
  const t = node.context.currentTime;
  param.cancelScheduledValues(t);
  param.setValueAtTime(value, t);
}

function waitForWorkletReady(node) {
  return new Promise((resolve, reject) => {
    const timeout = setTimeout(() => reject(new Error('Timeout waiting for Hydra worklet readiness')), 10000);
    const onMessage = (event) => {
      if (event.data?.type === 'ready') {
        clearTimeout(timeout);
        node.port.removeEventListener('message', onMessage);
        resolve();
      } else if (event.data?.type === 'error') {
        clearTimeout(timeout);
        node.port.removeEventListener('message', onMessage);
        reject(new Error(event.data.message || 'Hydra worklet failed to initialize'));
      }
    };
    node.port.addEventListener('message', onMessage);
    node.port.start();
  });
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
      wasmUrl: './hydra_dsp.wasm'
    }
  });

  fxNode.port.onmessage = (event) => {
    if (event.data?.type === 'ready') {
      setStatus('WASM ready in AudioWorklet.');
      if (DEBUG_ROUNDTRIP) {
        post({ type: 'debugState', enabled: true });
      }
      syncAllParams();
    } else if (event.data?.type === 'error') {
      setStatus(`Worklet init error: ${event.data.message}`);
    } else if (event.data?.type === 'stateAck') {
      engineState[event.data.key] = event.data.value;
    } else if (event.data?.type === 'stateSnapshot') {
      Object.assign(engineState, event.data.values);
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

async function ensurePlaybackReady() {
  await ensureAudioGraph();
  if (context.state !== 'running') await context.resume();
  if (!source) {
    source = context.createMediaElementSource(player);
  }
  connectGraph();
}

function syncAllParams(node = fxNode) {
  const map = ['delayActive', 'delayTimeMs', 'feedback', 'dryWet', 'drive', 'flutterDepth', 'wowDepth'];
  map.forEach((id) => {
    const el = document.getElementById(id);
    const value = el.type === 'checkbox' ? (el.checked ? 1 : 0) : Number(el.value);
    uiState[id] = value;
    if (CONTINUOUS_PARAM_IDS.has(id)) {
      setContinuousParam(PARAM[id], value, node);
    } else {
      node?.port.postMessage({ type: 'command', command: id, value });
    }
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
  await ensurePlaybackReady();
  setStatus('Audio context running. Press Play or use the audio element controls.');
});

playBtn.addEventListener('click', async () => {
  try {
    await ensurePlaybackReady();
    await player.play();
    setStatus('Playback started.');
  } catch (error) {
    setStatus(`Playback failed: ${error.message}`);
  }
});

stopBtn.addEventListener('click', () => {
  player.pause();
  player.currentTime = 0;
  setStatus('Playback stopped and rewound.');
});

repeatBtn.addEventListener('click', () => {
  transportState.toggleRepeat();
  const { isRepeatEnabled } = transportState.getState();
  setStatus(isRepeatEnabled ? 'Repeat enabled.' : 'Repeat disabled.');
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
});

['delayActive', 'delayTimeMs', 'feedback', 'dryWet', 'drive', 'flutterDepth', 'wowDepth'].forEach((id) => {
  const el = document.getElementById(id);
  el.addEventListener('input', () => {
    const value = el.type === 'checkbox' ? (el.checked ? 1 : 0) : Number(el.value);
    uiState[id] = value;
    if (CONTINUOUS_PARAM_IDS.has(id)) {
      setContinuousParam(PARAM[id], value);
    } else {
      post({ type: 'command', command: id, value });
    }
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
  try {
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
        wasmUrl: './hydra_dsp.wasm'
      }
    });

    await waitForWorkletReady(node);
    node.port.postMessage({ type: 'bypass', enabled: false });
    syncAllParams(node);

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
  } catch (error) {
    setStatus(`Offline render failed: ${error.message}`);
  }
});
