class HydraProcessor extends AudioWorkletProcessor {
  constructor(options) {
    super();
    this.ready = false;
    this.bypass = false;
    this.handle = 0;
    this.blockSize = 128;

    const opts = (options && options.processorOptions) || {};
    this.moduleUrl = opts.moduleUrl || './hydra_dsp.js';
    this.wasmUrl = opts.wasmUrl || './hydra_dsp.wasm';

    this.port.onmessage = (event) => this.onMessage(event.data);
    this.initWasm();
  }

  initWasm() {
    globalThis.Module = {
      locateFile: (path) => (path.endsWith('.wasm') ? this.wasmUrl : path),
      onRuntimeInitialized: () => {
        this.api = {
          create: Module.cwrap('hydra_dsp_create', 'number', ['number', 'number', 'number']),
          destroy: Module.cwrap('hydra_dsp_destroy', null, ['number']),
          prepare: Module.cwrap('hydra_dsp_prepare', 'number', ['number', 'number', 'number']),
          reset: Module.cwrap('hydra_dsp_reset', null, ['number']),
          setParameter: Module.cwrap('hydra_dsp_set_parameter', 'number', ['number', 'number', 'number']),
          commit: Module.cwrap('hydra_dsp_commit', 'number', ['number']),
          process: Module.cwrap('hydra_dsp_process', 'number', ['number', 'number', 'number', 'number', 'number', 'number'])
        };

        const hPtrPtr = Module._malloc(4);
        this.api.create(sampleRate, 4000.0, hPtrPtr);
        this.handle = Module.HEAP32[hPtrPtr >> 2];
        Module._free(hPtrPtr);
        this.api.prepare(this.handle, this.blockSize, 2);

        this.inL = Module._malloc(this.blockSize * 4);
        this.inR = Module._malloc(this.blockSize * 4);
        this.outL = Module._malloc(this.blockSize * 4);
        this.outR = Module._malloc(this.blockSize * 4);

        this.ready = true;
        this.port.postMessage({ type: 'ready' });
      }
    };

    importScripts(this.moduleUrl);
  }

  onMessage(msg) {
    if (!msg || !this.ready) {
      if (msg?.type === 'bypass') this.bypass = !!msg.enabled;
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
    const input = inputs[0];
    const output = outputs[0];
    const frames = output[0]?.length || 128;

    const inL = input?.[0] || new Float32Array(frames);
    const inR = input?.[1] || inL;

    if (!this.ready || this.bypass) {
      output[0].set(inL);
      if (output[1]) output[1].set(inR);
      return true;
    }

    Module.HEAPF32.set(inL, this.inL >> 2);
    Module.HEAPF32.set(inR, this.inR >> 2);
    this.api.process(this.handle, this.inL, this.inR, this.outL, this.outR, frames);

    output[0].set(Module.HEAPF32.subarray(this.outL >> 2, (this.outL >> 2) + frames));
    if (output[1]) {
      output[1].set(Module.HEAPF32.subarray(this.outR >> 2, (this.outR >> 2) + frames));
    }

    return true;
  }
}

registerProcessor('hydra-processor', HydraProcessor);
