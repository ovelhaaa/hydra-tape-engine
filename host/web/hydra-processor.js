const HYDRA_SHARED = globalThis.__hydraShared || {
  runtimePromise: null,
  module: null,
  moduleUrl: null,
  wasmUrl: null
};
globalThis.__hydraShared = HYDRA_SHARED;

function loadHydraRuntime(moduleUrl, wasmUrl) {
  if (HYDRA_SHARED.runtimePromise) {
    return HYDRA_SHARED.runtimePromise;
  }

  HYDRA_SHARED.moduleUrl = moduleUrl;
  HYDRA_SHARED.wasmUrl = wasmUrl;
  HYDRA_SHARED.runtimePromise = (async () => {
    const moduleUrlResolved = new URL(HYDRA_SHARED.moduleUrl, globalThis.location.href).href;
    const wasmUrlResolved = new URL(HYDRA_SHARED.wasmUrl, globalThis.location.href).href;
    const createHydraModule = (await import(moduleUrlResolved)).default;
    HYDRA_SHARED.module = await createHydraModule({
      locateFile: (path) => (path.endsWith('.wasm') ? wasmUrlResolved : path)
    });
    return HYDRA_SHARED.module;
  })();

  return HYDRA_SHARED.runtimePromise;
}

class HydraProcessor extends AudioWorkletProcessor {
  constructor(options) {
    super();
    this.ready = false;
    this.bypass = false;
    this.handle = 0;
    this.capacity = 0;
    this.pendingMessages = [];

    const opts = (options && options.processorOptions) || {};
    this.moduleUrl = opts.moduleUrl || './hydra_dsp.js';
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
      this.module = await loadHydraRuntime(this.moduleUrl, this.wasmUrl);
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

  onMessage(msg) {
    if (!msg) return;

    if (!this.ready) {
      if (msg.type === 'bypass') this.bypass = !!msg.enabled;
      else this.pendingMessages.push(msg);
      return;
    }

    if (msg.type === 'bypass') {
      this.bypass = !!msg.enabled;
    } else if (msg.type === 'reset') {
      this.api.reset(this.handle);
    } else if (msg.type === 'param') {
      this.api.setParameter(this.handle, msg.paramId, msg.value);
      this.api.commit(this.handle);
    }
  }

  process(inputs, outputs) {
    const input = inputs[0] || [];
    const output = outputs[0] || [];
    const frames = output[0]?.length || 128;

    const inL = input[0] || new Float32Array(frames);
    const inR = input[1] || inL;

    if (!output[0]) return true;

    if (!this.ready || this.bypass) {
      output[0].set(inL);
      if (output[1]) output[1].set(inR);
      return true;
    }

    this.ensureBuffers(frames);

    this.module.HEAPF32.set(inL, this.inL >> 2);
    this.module.HEAPF32.set(inR, this.inR >> 2);
    this.api.process(this.handle, this.inL, this.inR, this.outL, this.outR, frames);

    output[0].set(this.module.HEAPF32.subarray(this.outL >> 2, (this.outL >> 2) + frames));
    if (output[1]) {
      output[1].set(this.module.HEAPF32.subarray(this.outR >> 2, (this.outR >> 2) + frames));
    }

    return true;
  }
}

registerProcessor('hydra-processor', HydraProcessor);
