import { createTransportState } from './transportState.js';
import { createTransportController } from './transportController.js';
import { DEFAULT_CONTROL_STATE, deserializePresetFromText, serializePreset } from './presetSerialization.js';
import { formatRuntimeError } from './runtimeDiagnostics.js';

const PARAM = {
  flutterDepth: 0,
  wowDepth: 1,
  dropoutSeverity: 2,
  drive: 3,
  noise: 4,
  tapeSpeed: 5,
  tapeAge: 6,
  headBumpAmount: 7,
  azimuthError: 8,
  flutterRate: 9,
  wowRate: 10,
  delayActive: 11,
  delayTimeMs: 12,
  feedback: 13,
  dryWet: 14,
  activeHeads: 15,
  bpm: 16,
  headsMusical: 17,
  guitarFocus: 18,
  tone: 19,
  pingPong: 20,
  freeze: 21,
  reverse: 22,
  reverseSmear: 23,
  spring: 24,
  springDecay: 25,
  springDamping: 26,
  springMix: 27,
  reset: 28
};

const ALL_PARAM_IDS = Object.keys(PARAM).filter(id => id !== 'reset');

const CONTINUOUS_PARAM_IDS = new Set([
  'flutterDepth', 'wowDepth', 'dropoutSeverity', 'drive', 'noise',
  'tapeSpeed', 'tapeAge', 'headBumpAmount', 'azimuthError',
  'flutterRate', 'wowRate', 'delayTimeMs', 'feedback', 'dryWet',
  'bpm', 'tone', 'springDecay', 'springDamping', 'springMix'
]);

const PARAM_METADATA = {
  flutterDepth: { group: 'Tape Dynamics', engineParamId: PARAM.flutterDepth, layer: 'AudioParam (Smooth)' },
  wowDepth: { group: 'Tape Dynamics', engineParamId: PARAM.wowDepth, layer: 'AudioParam (Smooth)' },
  dropoutSeverity: { group: 'Tape Quality', engineParamId: PARAM.dropoutSeverity, layer: 'AudioParam (Smooth)' },
  drive: { group: 'Saturation', engineParamId: PARAM.drive, layer: 'AudioParam (Smooth)' },
  noise: { group: 'Tape Noise', engineParamId: PARAM.noise, layer: 'AudioParam (Smooth)' },
  tapeSpeed: { group: 'Tape Speed', engineParamId: PARAM.tapeSpeed, layer: 'AudioParam (Smooth)' },
  tapeAge: { group: 'Tape Age', engineParamId: PARAM.tapeAge, layer: 'AudioParam (Smooth)' },
  headBumpAmount: { group: 'Low Freq Bump', engineParamId: PARAM.headBumpAmount, layer: 'AudioParam (Smooth)' },
  azimuthError: { group: 'Stereo Width', engineParamId: PARAM.azimuthError, layer: 'AudioParam (Smooth)' },
  flutterRate: { group: 'Modulation Speed', engineParamId: PARAM.flutterRate, layer: 'AudioParam (Smooth)' },
  wowRate: { group: 'Modulation Speed', engineParamId: PARAM.wowRate, layer: 'AudioParam (Smooth)' },
  delayActive: { group: 'Master Bypass', engineParamId: PARAM.delayActive, layer: 'MessagePort (Toggle)' },
  delayTimeMs: { group: 'Time Domain', engineParamId: PARAM.delayTimeMs, layer: 'AudioParam (Live Smooth)' },
  feedback: { group: 'Feedback Loop', engineParamId: PARAM.feedback, layer: 'AudioParam (Smooth)' },
  dryWet: { group: 'Mix', engineParamId: PARAM.dryWet, layer: 'AudioParam (Smooth)' },
  activeHeads: { group: 'Multitap Configuration', engineParamId: PARAM.activeHeads, layer: 'MessagePort (Int Enum)' },
  bpm: { group: 'Tempo Sync', engineParamId: PARAM.bpm, layer: 'AudioParam (Smooth)' },
  headsMusical: { group: 'Time Mode', engineParamId: PARAM.headsMusical, layer: 'MessagePort (Toggle)' },
  guitarFocus: { group: 'DSP Focus', engineParamId: PARAM.guitarFocus, layer: 'MessagePort (Toggle)' },
  tone: { group: 'EQ / Filtering', engineParamId: PARAM.tone, layer: 'AudioParam (Smooth)' },
  pingPong: { group: 'Stereo Spatialization', engineParamId: PARAM.pingPong, layer: 'MessagePort (Toggle)' },
  freeze: { group: 'Buffer Mode', engineParamId: PARAM.freeze, layer: 'MessagePort (Toggle)' },
  reverse: { group: 'Playback Direction', engineParamId: PARAM.reverse, layer: 'MessagePort (Toggle)' },
  reverseSmear: { group: 'Artifact Handling', engineParamId: PARAM.reverseSmear, layer: 'MessagePort (Toggle)' },
  spring: { group: 'Reverb Toggle', engineParamId: PARAM.spring, layer: 'MessagePort (Toggle)' },
  springDecay: { group: 'Spring Length', engineParamId: PARAM.springDecay, layer: 'AudioParam (Smooth)' },
  springDamping: { group: 'Spring Filter', engineParamId: PARAM.springDamping, layer: 'AudioParam (Smooth)' },
  springMix: { group: 'Reverb Mix', engineParamId: PARAM.springMix, layer: 'AudioParam (Smooth)' }
};

const DEBUG_ROUNDTRIP = false;
const UI_THROTTLE_NORMAL_MS = 33;
const UI_THROTTLE_LOW_POWER_MS = 120;

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
const exportPresetBtn = document.getElementById('exportPresetBtn');
const importPresetBtn = document.getElementById('importPresetBtn');
const importPresetInput = document.getElementById('importPresetInput');
const downloadLink = document.getElementById('downloadLink');
const previewBadge = document.getElementById('previewBadge');
const perfBadge = document.getElementById('perfBadge');
const actionFeedback = document.getElementById('actionFeedback');
const paramMapTableBody = document.getElementById('paramMapTableBody');
const latencyCheckBtn = document.getElementById('latencyCheckBtn');
const latencyReport = document.getElementById('latencyReport');

let context;
let source;
let fxNode;
let bypass = false;
let connected = false;
let currentFileArrayBuffer;
const uiState = {};
const engineState = {};
let lowPowerMode = false;
let uiUpdateThrottleMs = UI_THROTTLE_NORMAL_MS;
let uiVisualUpdateAt = 0;
let uiNeedsFlush = false;
let performanceMonitorStarted = false;
let lastRafAt = 0;
let lowPerfSamples = 0;
let rafId = 0;

// Cached WASM Binary to pass securely into Worklet instance constructors
let cachedWasmBinary = null;

async function getWasmBinary() {
  if (cachedWasmBinary) return cachedWasmBinary;
  setStatus('Fetching DSP binary...', 'info');
  const response = await fetch('./hydra_dsp.wasm');
  if (!response.ok) {
    throw new Error(`Failed to download WASM module: ${response.status} ${response.statusText}`);
  }
  cachedWasmBinary = await response.arrayBuffer();
  return cachedWasmBinary;
}

const transportState = createTransportState();
createTransportController({ player, transportState, setStatus });

function updateRepeatUI({ isRepeatEnabled }) {
  repeatBtn.textContent = isRepeatEnabled ? 'Repeat ON' : 'Repeat OFF';
  repeatBtn.setAttribute('aria-pressed', String(isRepeatEnabled));
  repeatBtn.dataset.pressed = String(isRepeatEnabled);
}

updateRepeatUI(transportState.getState());
transportState.subscribe(updateRepeatUI);

function setStatus(msg, state = 'info') {
  statusEl.textContent = msg;
  statusEl.dataset.state = state;
}

function setActionFeedback(msg, state = 'info') {
  actionFeedback.textContent = msg;
  actionFeedback.dataset.state = state;
}

function reportRuntimeError(contextLabel, error, feedbackFallback = 'Web DSP runtime exception') {
  const { hint, message } = formatRuntimeError(contextLabel, error);
  setStatus(message, 'error');
  setActionFeedback(hint || feedbackFallback, 'error');
  console.error(`${contextLabel}:`, error);
}

function updatePreviewBadge() {
  const previewActive = !player.paused && connected;
  previewBadge.textContent = previewActive ? 'Preview Active' : 'Preview Inactive';
  previewBadge.dataset.state = previewActive ? 'success' : 'info';
}

function applyLowPowerMode(enabled) {
  lowPowerMode = enabled;
  uiUpdateThrottleMs = enabled ? UI_THROTTLE_LOW_POWER_MS : UI_THROTTLE_NORMAL_MS;
  perfBadge.textContent = enabled ? 'Visual Fallback: ON' : 'Visual Fallback: OFF';
  perfBadge.dataset.state = enabled ? 'warning' : 'info';
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
  if (typeof param.cancelAndHoldAtTime === 'function') {
    param.cancelAndHoldAtTime(t);
  } else {
    param.cancelScheduledValues(t);
  }
  param.linearRampToValueAtTime(value, t + 0.015);
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

  setStatus('Initializing Audio Graph...', 'info');
  
  // Obtain pre-fetched WASM ArrayBuffer in the main thread to bypass AudioWorklet scope limits
  const wasmBinary = await getWasmBinary();
  
  context = new AudioContext({ sampleRate: 48000 });
  await context.audioWorklet.addModule('./hydra-processor.js');
  
  fxNode = new AudioWorkletNode(context, 'hydra-processor', {
    numberOfInputs: 1,
    numberOfOutputs: 1,
    outputChannelCount: [2],
    processorOptions: {
      wasmBinary: wasmBinary
    }
  });

  fxNode.port.onmessage = (event) => {
    if (event.data?.type === 'ready') {
      setStatus('WASM initialized in AudioWorklet context.', 'success');
      syncAllParams();
    } else if (event.data?.type === 'error') {
      setStatus(`Worklet Exception: ${event.data.message}`, 'error');
      setActionFeedback('Worklet execution failed', 'error');
    } else if (event.data?.type === 'stateAck') {
      engineState[event.data.key] = event.data.value;
    }
  };
  
  startPerformanceMonitor();
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

function getControlStateFromUI() {
  const snapshot = {};
  ALL_PARAM_IDS.forEach((id) => {
    const el = document.getElementById(id);
    if (!el) return;
    if (el.type === 'checkbox') {
      snapshot[id] = el.checked ? 1 : 0;
    } else {
      snapshot[id] = Number(el.value);
    }
  });
  return { ...DEFAULT_CONTROL_STATE, ...snapshot };
}

function applyControlStateToUI(controlState) {
  Object.entries(controlState).forEach(([id, value]) => {
    const el = document.getElementById(id);
    if (!el) return;
    if (el.type === 'checkbox') {
      el.checked = value >= 0.5;
    } else {
      el.value = String(value);
    }
  });
  syncAllParams();
}

function syncAllParams(node = fxNode) {
  ALL_PARAM_IDS.forEach((id) => {
    const el = document.getElementById(id);
    if (!el) return;
    const value = el.type === 'checkbox' ? (el.checked ? 1 : 0) : Number(el.value);
    uiState[id] = value;
    if (CONTINUOUS_PARAM_IDS.has(id)) {
      setContinuousParam(PARAM[id], value, node);
    } else {
      node?.port.postMessage({ type: 'command', command: id, value });
    }
  });
  requestUIFlush();
}

function renderParamMapping() {
  if (!paramMapTableBody) return;
  const rows = Object.entries(PARAM_METADATA).map(([id, meta]) => {
    const control = document.getElementById(id);
    let val = '-';
    if (control) {
      val = control.type === 'checkbox' ? (control.checked ? '1 (True)' : '0 (False)') : Number(control.value).toFixed(2);
    }
    return `<tr>
      <td><code>${id}</code></td>
      <td>${meta.group}</td>
      <td align="center">${meta.engineParamId}</td>
      <td>${meta.layer}</td>
      <td align="right"><strong>${val}</strong></td>
    </tr>`;
  });
  paramMapTableBody.innerHTML = rows.join('');
}

function updateControlReadouts() {
  ALL_PARAM_IDS.forEach((id) => {
    const el = document.getElementById(id);
    if (!el || el.type === 'checkbox') return;
    const valDisplay = document.querySelector(`[data-value-for="${id}"]`);
    if (valDisplay) {
      const val = Number(el.value);
      valDisplay.textContent = val % 1 === 0 ? val.toString() : val.toFixed(1);
    }
  });
}

function requestUIFlush() {
  uiNeedsFlush = true;
  if (rafId) return;
  const tick = (ts) => {
    rafId = 0;
    if (uiNeedsFlush && ts - uiVisualUpdateAt >= uiUpdateThrottleMs) {
      uiNeedsFlush = false;
      uiVisualUpdateAt = ts;
      updateControlReadouts();
      renderParamMapping();
    }
    if (uiNeedsFlush) {
      rafId = requestAnimationFrame(tick);
    }
  };
  rafId = requestAnimationFrame(tick);
}

function startPerformanceMonitor() {
  if (performanceMonitorStarted) return;
  performanceMonitorStarted = true;
  const monitor = (ts) => {
    if (lastRafAt) {
      const delta = ts - lastRafAt;
      if (delta > 45) {
        lowPerfSamples += 1;
      } else {
        lowPerfSamples = Math.max(0, lowPerfSamples - 1);
      }
      if (lowPerfSamples > 25 && !lowPowerMode) applyLowPowerMode(true);
      if (lowPerfSamples < 5 && lowPowerMode) applyLowPowerMode(false);
    }
    lastRafAt = ts;
    requestAnimationFrame(monitor);
  };
  requestAnimationFrame(monitor);
}

async function runLatencyStabilityCheck() {
  if (!fxNode || !context) {
    latencyReport.textContent = 'Initialize audio first to benchmark processing stress.';
    setActionFeedback('Validation unavailable without live audio', 'warning');
    return;
  }
  const scenarioStart = performance.now();
  const benchParams = ['delayTimeMs', 'feedback', 'dryWet', 'drive', 'flutterDepth', 'wowDepth'];
  const rounds = 40;
  for (let i = 0; i < rounds; i++) {
    const phase = i / rounds;
    benchParams.forEach((id, idx) => {
      const el = document.getElementById(id);
      if (!el) return;
      const min = Number(el.min);
      const max = Number(el.max);
      const sweep = (Math.sin((phase * Math.PI * 2) + idx) + 1) / 2;
      const value = min + (max - min) * sweep;
      setContinuousParam(PARAM[id], value);
    });
    requestUIFlush();
    await new Promise((resolve) => setTimeout(resolve, 8));
  }
  const elapsedMs = performance.now() - scenarioStart;
  const baseLatencyMs = (context.baseLatency || 0) * 1000;
  latencyReport.textContent = `Stress test finished: ${rounds} cycles across parameters in ${elapsedMs.toFixed(1)}ms | baseLatency=${baseLatencyMs.toFixed(2)}ms | fallback=${lowPowerMode ? 'ON' : 'OFF'}`;
  setActionFeedback('Control stabilization validated', 'success');
}

fileInput.addEventListener('change', async (event) => {
  const file = event.target.files?.[0];
  if (!file) return;
  const url = URL.createObjectURL(file);
  player.src = url;
  currentFileArrayBuffer = await file.arrayBuffer();
  setStatus(`Loaded: ${file.name}`, 'success');
  setActionFeedback('Audio source loaded into memory', 'success');
});

startBtn.addEventListener('click', async () => {
  try {
    await ensurePlaybackReady();
    setStatus('Audio engine started. Press Play to stream output.', 'success');
  } catch (error) {
    reportRuntimeError('Engine start exception', error);
  }
});

playBtn.addEventListener('click', async () => {
  try {
    await ensurePlaybackReady();
    await player.play();
    setStatus('Playback active.', 'success');
    updatePreviewBadge();
  } catch (error) {
    reportRuntimeError('Playback exception', error);
  }
});

stopBtn.addEventListener('click', () => {
  player.pause();
  player.currentTime = 0;
  setStatus('Playback stopped and rewound.', 'info');
  updatePreviewBadge();
});

repeatBtn.addEventListener('click', () => {
  transportState.toggleRepeat();
  const { isRepeatEnabled } = transportState.getState();
  setStatus(isRepeatEnabled ? 'Playback loop enabled.' : 'Playback loop disabled.', 'info');
});

connectBtn.addEventListener('click', () => {
  connected = !connected;
  connectBtn.textContent = connected ? 'Disconnect DSP Chain' : 'Connect DSP Chain';
  connectBtn.dataset.pressed = String(connected);
  connectGraph();
  updatePreviewBadge();
  setActionFeedback(connected ? 'Wet FX routing active' : 'Dry routing active', connected ? 'success' : 'info');
});

bypassBtn.addEventListener('click', () => {
  bypass = !bypass;
  bypassBtn.textContent = `Bypass: ${bypass ? 'ACTIVE' : 'OFF'}`;
  bypassBtn.dataset.pressed = String(bypass);
  post({ type: 'bypass', enabled: bypass });
  setActionFeedback(bypass ? 'Plugin bypassed' : 'Plugin active', bypass ? 'warning' : 'success');
});

resetBtn.addEventListener('click', () => {
  post({ type: 'reset' });
  setStatus('DSP Core clear command sent.', 'info');
  setActionFeedback('DSP buffers cleared', 'success');
});

// Setup listener bindings for all 28 configuration variables
ALL_PARAM_IDS.forEach((id) => {
  const el = document.getElementById(id);
  if (!el) return;
  el.addEventListener('input', () => {
    const value = el.type === 'checkbox' ? (el.checked ? 1 : 0) : Number(el.value);
    uiState[id] = value;
    if (CONTINUOUS_PARAM_IDS.has(id)) {
      setContinuousParam(PARAM[id], value);
    } else {
      post({ type: 'command', command: id, value });
    }
    requestUIFlush();
  });
});

exportPresetBtn.addEventListener('click', () => {
  try {
    const timestamp = new Date().toISOString();
    const presetJson = serializePreset(getControlStateFromUI(), {
      name: `Hydra Preset - ${timestamp.slice(0, 19).replace('T', ' ')}`
    });
    const blob = new Blob([presetJson], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const anchor = document.createElement('a');
    anchor.href = url;
    anchor.download = `hydra-preset-${timestamp.replace(/[:.]/g, '-')}.json`;
    document.body.append(anchor);
    anchor.click();
    anchor.remove();
    URL.revokeObjectURL(url);
    setStatus('Preset exported successfully.', 'success');
    setActionFeedback('Preset configuration saved', 'success');
  } catch (error) {
    setStatus(`Export failed: ${error.message}`, 'error');
    setActionFeedback('Failed to serialize preset', 'error');
  }
});

importPresetBtn.addEventListener('click', () => {
  importPresetInput.value = '';
  importPresetInput.click();
});

importPresetInput.addEventListener('change', async (event) => {
  const file = event.target.files?.[0];
  if (!file) return;

  try {
    const text = await file.text();
    const { controlState, migratedFromVersion } = deserializePresetFromText(text);
    applyControlStateToUI(controlState);
    if (migratedFromVersion) {
      setStatus(`Preset successfully imported and updated from v${migratedFromVersion} schema.`, 'success');
      setActionFeedback('V1 configuration migrated', 'success');
    } else {
      setStatus('Preset imported and active.', 'success');
      setActionFeedback('Configuration ingested successfully', 'success');
    }
  } catch (error) {
    setStatus(`Import failed: ${error.message}`, 'error');
    setActionFeedback('JSON Schema rejection', 'error');
  }
});

player.addEventListener('play', updatePreviewBadge);
player.addEventListener('pause', updatePreviewBadge);
player.addEventListener('ended', updatePreviewBadge);
latencyCheckBtn.addEventListener('click', runLatencyStabilityCheck);

// Bootstrap baseline visualization readouts
updateControlReadouts();
updatePreviewBadge();
applyLowPowerMode(false);
connectBtn.dataset.pressed = String(connected);
bypassBtn.dataset.pressed = String(bypass);

function encodeWav(stereoChannels, sampleRate) {
  const leftChannel = stereoChannels[0];
  // Structural Mono Fallback: Duplicate left array buffer if input is single channel mono
  const rightChannel = stereoChannels[1] || stereoChannels[0];
  const length = leftChannel.length;
  
  const totalBytes = 44 + length * 4;
  const buffer = new ArrayBuffer(totalBytes);
  const view = new DataView(buffer);

  const writeStr = (offset, str) => [...str].forEach((c, i) => view.setUint8(offset + i, c.charCodeAt(0)));
  writeStr(0, 'RIFF');
  view.setUint32(4, 36 + length * 4, true);
  writeStr(8, 'WAVE');
  writeStr(12, 'fmt ');
  view.setUint32(16, 16, true);
  view.setUint16(20, 1, true); // PCM Format
  view.setUint16(22, 2, true); // Stereo
  view.setUint32(24, sampleRate, true);
  view.setUint32(28, sampleRate * 4, true);
  view.setUint16(32, 4, true);
  view.setUint16(34, 16, true); // 16-bit PCM depth
  writeStr(36, 'data');
  view.setUint32(40, length * 4, true);

  let byteOffset = 44;
  for (let i = 0; i < length; i++) {
    const lSample = Math.max(-1, Math.min(1, leftChannel[i]));
    const rSample = Math.max(-1, Math.min(1, rightChannel[i]));
    view.setInt16(byteOffset, lSample * 32767, true);
    view.setInt16(byteOffset + 2, rSample * 32767, true);
    byteOffset += 4;
  }
  return new Blob([buffer], { type: 'audio/wav' });
}

offlineBtn.addEventListener('click', async () => {
  if (!currentFileArrayBuffer) {
    setStatus('Please load an input audio track first.', 'warning');
    setActionFeedback('Input buffer empty', 'warning');
    return;
  }

  setStatus('Commencing faster-than-realtime offline render...', 'info');
  setActionFeedback('Offline processing initiated', 'info');
  
  let decodeCtx = null;
  let offlineCtx = null;
  let offlineNode = null;
  
  try {
    // Read raw file contents at consistent 48kHz output sample rate
    decodeCtx = new AudioContext({ sampleRate: 48000 });
    const decoded = await decodeCtx.decodeAudioData(currentFileArrayBuffer.slice(0));
    await decodeCtx.close();

    const wasmBinary = await getWasmBinary();

    offlineCtx = new OfflineAudioContext(2, decoded.length, 48000);
    await offlineCtx.audioWorklet.addModule('./hydra-processor.js');

    offlineNode = new AudioWorkletNode(offlineCtx, 'hydra-processor', {
      numberOfInputs: 1,
      numberOfOutputs: 1,
      outputChannelCount: [2],
      processorOptions: {
        wasmBinary: wasmBinary
      }
    });

    await waitForWorkletReady(offlineNode);
    
    // Replicate current interactive configuration into the offline context
    offlineNode.port.postMessage({ type: 'bypass', enabled: false });
    syncAllParams(offlineNode);

    const src = offlineCtx.createBufferSource();
    src.buffer = decoded;
    src.connect(offlineNode).connect(offlineCtx.destination);
    src.start();

    const renderedBuffer = await offlineCtx.startRendering();
    
    // Cleanup WASM heap and handles to avoid leak accumulations
    offlineNode.port.postMessage({ type: 'destroy' });

    const channels = [];
    for (let c = 0; c < renderedBuffer.numberOfChannels; c++) {
      channels.push(renderedBuffer.getChannelData(c));
    }

    const wavBlob = encodeWav(channels, renderedBuffer.sampleRate);
    const exportUrl = URL.createObjectURL(wavBlob);
    
    downloadLink.href = exportUrl;
    downloadLink.style.display = 'inline-flex';
    downloadLink.textContent = 'Download Processed WAV File';
    
    setStatus('Offline render completed successfully.', 'success');
    setActionFeedback('Export artifact ready', 'success');
  } catch (error) {
    // Emergency cleanup routing
    if (offlineNode) offlineNode.port.postMessage({ type: 'destroy' });
    reportRuntimeError('Offline rendering pipeline failure', error);
  }
});