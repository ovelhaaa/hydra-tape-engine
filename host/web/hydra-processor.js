import createHydraModule from './hydra_dsp.js';

const HYDRA_SHARED = globalThis.__hydraShared || {
  runtimes: new Map()
};
if (!(HYDRA_SHARED.runtimes instanceof Map)) {
  HYDRA_SHARED.runtimes = new Map();
}
globalThis.__hydraShared = HYDRA_SHARED;

function resolveUrl(baseHref, relativeOrAbsolute) {
  try {
    return new URL(relativeOrAbsolute, baseHref).href;
  } catch (_) {
    return relativeOrAbsolute;
  }
}

function loadHydraRuntime(wasmUrl) {
  const wasmUrlResolved = resolveUrl(import.meta.url, wasmUrl || './hydra_dsp.wasm');
  const cached = HYDRA_SHARED.runtimes.get(wasmUrlResolved);
  if (cached) return cached;

  const runtimePromise = (async () => {
    const module = await createHydraModule({
      locateFile: (path) => (path.endsWith('.wasm') ? wasmUrlResolved : path)
    });
    return module;
  })();
  HYDRA_SHARED.runtimes.set(wasmUrlResolved, runtimePromise);

  return runtimePromise;
}

class HydraProcessor extends AudioWorkletProcessor {
  static get parameterDescriptors() {
    return [
      { name: 'p_12', defaultValue: 500, minValue: 10, maxValue: 2000, automationRate: 'a-rate' }, // delayTimeMs
      { name: 'p_13', defaultValue: 40, minValue: 0, maxValue: 100, automationRate: 'a-rate' }, // feedback
      { name: 'p_14', defaultValue: 50, minValue: 0, maxValue: 100, automationRate: 'a-rate' }, // dryWet
      { name: 'p_3', defaultValue: 40, minValue: 0, maxValue: 100, automationRate: 'a-rate' }, // drive
      { name: 'p_0', defaultValue: 20, minValue: 0, maxValue: 100, automationRate: 'a-rate' }, // flutterDepth
      { name: 'p_1', defaultValue: 15, minValue: 0, maxValue: 100, automationRate: 'a-rate' } // wowDepth
    ];
  }

  constructor(options) {
    super();
    this.ready = false;
    this.bypass = false;
    this.handle = 0;
    this.capacity = 0;
    this.pendingMessages = [];
    this.paramState = new Map([
      [12, 500],
      [13, 40],
      [14, 50],
      [3, 40],
      [0, 20],
      [1, 15]
    ]);
    this.delayActive = 1;
    this.debugState = false;
    this.blocksUntilDebugSnapshot = 0;

    this.bypassMixCurrent = 1.0;
    this.bypassMixTarget = 1.0;
    this.bypassRampSamples = Math.max(1, Math.round(sampleRate * 0.01)); // 10ms clickless transition

    const opts = (options && options.processorOptions) || {};
    this.wasmUrl = opts.wasmUrl || './hydra_dsp.wasm';

    this.port.onmessage = (event) => this.onMessage(event.data);
    this.initWasm();
  }

  ensureBuffers(frames) {
    if (frames <= this.capacity) return;

    if (this.inL) this.module._free(this.inL);
    if (this.inR) this.module._free(this.inR);
    if (this.outL) this.module._free(this.outL);
    if (this.outR) this.module._free(this.outR);

    this.capacity = frames;
    this.inL = this.module._malloc(this.capacity * 4);
    this.inR = this.module._malloc(this.capacity * 4);
    this.outL = this.module._malloc(this.capacity * 4);
    this.outR = this.module._malloc(this.capacity * 4);

    this.api.prepare(this.handle, this.capacity, 2);
  }

  async initWasm() {
    try {
      this.module = await loadHydraRuntime(this.wasmUrl);
      this.api = {
        create: this.module.cwrap('hydra_dsp_create', 'number', ['number', 'number', 'number']),
        destroy: this.module.cwrap('hydra_dsp_destroy', null, ['number']),
        prepare: this.module.cwrap('hydra_dsp_prepare', 'number', ['number', 'number', 'number']),
        reset: this.module.cwrap('hydra_dsp_reset', null, ['number']),
        setParameter: this.module.cwrap('hydra_dsp_set_parameter', 'number', ['number', 'number', 'number']),
        commit: this.module.cwrap('hydra_dsp_commit', 'number', ['number']),
        process: this.module.cwrap('hydra_dsp_process', 'number', ['number', 'number', 'number', 'number', 'number', 'number'])
      };

      const handlePtr = this.module._malloc(4);
      this.api.create(sampleRate, 4000.0, handlePtr);
      this.handle = this.module.HEAP32[handlePtr >> 2];
      this.module._free(handlePtr);

      this.ensureBuffers(128);

      this.ready = true;
      for (const msg of this.pendingMessages) this.onMessage(msg);
      this.pendingMessages.length = 0;
      this.port.postMessage({ type: 'ready' });
    } catch (error) {
      this.port.postMessage({ type: 'error', message: String(error) });
    }
  }

  smoothToward(paramId, targetValue, alpha) {
    const current = this.paramState.get(paramId) ?? targetValue;
    const next = current + (targetValue - current) * alpha;
    this.paramState.set(paramId, next);
    this.api.setParameter(this.handle, paramId, next);
  }

  applyContinuousParams(parameters) {
    const entries = [
      [12, parameters.p_12],
      [13, parameters.p_13],
      [14, parameters.p_14],
      [3, parameters.p_3],
      [0, parameters.p_0],
      [1, parameters.p_1]
    ];
    for (const [paramId, values] of entries) {
      if (!values || values.length === 0) continue;
      const targetValue = values.length > 1 ? values[values.length - 1] : values[0];
      this.smoothToward(paramId, targetValue, 0.35);
    }
    this.api.commit(this.handle);
  }

  onMessage(msg) {
    if (!msg) return;

    if (!this.ready) {
      if (msg.type === 'bypass') {
        this.bypass = !!msg.enabled;
        this.bypassMixTarget = this.bypass ? 0 : 1;
        // Keep startup state coherent: before first processed block, there is no
        // prior wet/dry history to preserve, so align current mix immediately.
        this.bypassMixCurrent = this.bypassMixTarget;
      } else this.pendingMessages.push(msg);
      return;
    }

    if (msg.type === 'bypass') {
      this.bypass = !!msg.enabled;
      this.bypassMixTarget = this.bypass ? 0 : 1;
      this.port.postMessage({ type: 'stateAck', key: 'bypass', value: this.bypass ? 1 : 0 });
    } else if (msg.type === 'reset') {
      this.api.reset(this.handle);
      this.port.postMessage({ type: 'stateAck', key: 'reset', value: 1 });
    } else if (msg.type === 'command' && msg.command === 'delayActive') {
      this.delayActive = msg.value ? 1 : 0;
      this.api.setParameter(this.handle, 11, this.delayActive);
      this.api.commit(this.handle);
      this.port.postMessage({ type: 'stateAck', key: 'delayActive', value: this.delayActive });
    } else if (msg.type === 'debugState') {
      this.debugState = !!msg.enabled;
    }
  }

  process(inputs, outputs, parameters) {
    const input = inputs[0] || [];
    const output = outputs[0] || [];
    const frames = output[0]?.length || 128;

    const inL = input[0] || new Float32Array(frames);
    const inR = input[1] || inL;

    if (!output[0]) return true;

    if (!this.ready) {
      output[0].set(inL);
      if (output[1]) output[1].set(inR);
      return true;
    }

    this.ensureBuffers(frames);
    this.applyContinuousParams(parameters);

    this.module.HEAPF32.set(inL, this.inL >> 2);
    this.module.HEAPF32.set(inR, this.inR >> 2);
    this.api.process(this.handle, this.inL, this.inR, this.outL, this.outR, frames);

    const wetL = this.module.HEAPF32.subarray(this.outL >> 2, (this.outL >> 2) + frames);
    const wetR = this.module.HEAPF32.subarray(this.outR >> 2, (this.outR >> 2) + frames);
    const rampStep = (this.bypassMixTarget - this.bypassMixCurrent) / this.bypassRampSamples;
    for (let i = 0; i < frames; i++) {
      if (Math.abs(this.bypassMixCurrent - this.bypassMixTarget) > 1e-6) {
        this.bypassMixCurrent += rampStep;
        if ((rampStep > 0 && this.bypassMixCurrent > this.bypassMixTarget) ||
            (rampStep < 0 && this.bypassMixCurrent < this.bypassMixTarget)) {
          this.bypassMixCurrent = this.bypassMixTarget;
        }
      }
      const dryMix = 1 - this.bypassMixCurrent;
      output[0][i] = (wetL[i] * this.bypassMixCurrent) + (inL[i] * dryMix);
      if (output[1]) {
        output[1][i] = (wetR[i] * this.bypassMixCurrent) + (inR[i] * dryMix);
      }
    }

    if (this.debugState) {
      this.blocksUntilDebugSnapshot -= 1;
      if (this.blocksUntilDebugSnapshot <= 0) {
        this.blocksUntilDebugSnapshot = 10;
        this.port.postMessage({
          type: 'stateSnapshot',
          values: Object.fromEntries(this.paramState.entries())
        });
      }
    }

    return true;
  }
}

registerProcessor('hydra-processor', HydraProcessor);
