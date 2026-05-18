import createHydraModule from './hydra_dsp.js';

const HYDRA_SHARED = globalThis.__hydraShared || {
  runtimes: new Map()
};
if (!(HYDRA_SHARED.runtimes instanceof Map)) {
  HYDRA_SHARED.runtimes = new Map();
}
globalThis.__hydraShared = HYDRA_SHARED;

function loadHydraRuntime(wasmBinary) {
  const cached = HYDRA_SHARED.runtimes.get('hydra');
  if (cached) return cached;

  const runtimePromise = (async () => {
    const module = await createHydraModule({
      wasmBinary: wasmBinary,
      locateFile: (path) => path
    });
    return module;
  })();
  HYDRA_SHARED.runtimes.set('hydra', runtimePromise);

  return runtimePromise;
}

const CONTINUOUS_PARAM_SPECS = [
  { name: 'p_0', defaultValue: 14, minValue: 0, maxValue: 45 },    // flutterDepth (musical web range)
  { name: 'p_1', defaultValue: 10, minValue: 0, maxValue: 35 },    // wowDepth (musical web range)
  { name: 'p_2', defaultValue: 5, minValue: 0, maxValue: 40 },     // dropoutSeverity (less frequent/intense)
  { name: 'p_3', defaultValue: 40, minValue: 0, maxValue: 100 },   // drive
  { name: 'p_4', defaultValue: 30, minValue: 0, maxValue: 100 },   // noise
  { name: 'p_5', defaultValue: 50, minValue: 0, maxValue: 100 },   // tapeSpeed
  { name: 'p_6', defaultValue: 40, minValue: 0, maxValue: 100 },   // tapeAge
  { name: 'p_7', defaultValue: 30, minValue: 0, maxValue: 100 },   // headBumpAmount
  { name: 'p_8', defaultValue: 10, minValue: 0, maxValue: 100 },   // azimuthError
  { name: 'p_9', defaultValue: 6.0, minValue: 0.1, maxValue: 20.0 }, // flutterRate
  { name: 'p_10', defaultValue: 0.8, minValue: 0.1, maxValue: 5.0 }, // wowRate
  { name: 'p_12', defaultValue: 500, minValue: 10, maxValue: 2000 }, // delayTimeMs
  { name: 'p_13', defaultValue: 40, minValue: 0, maxValue: 100 },  // feedback
  { name: 'p_14', defaultValue: 50, minValue: 0, maxValue: 100 },  // dryWet
  { name: 'p_16', defaultValue: 120, minValue: 30, maxValue: 300 }, // bpm
  { name: 'p_19', defaultValue: 50, minValue: 0, maxValue: 100 },  // tone
  { name: 'p_25', defaultValue: 60, minValue: 0, maxValue: 100 },  // springDecay
  { name: 'p_26', defaultValue: 45, minValue: 0, maxValue: 100 },  // springDamping
  { name: 'p_27', defaultValue: 50, minValue: 0, maxValue: 100 }   // springMix
];

class HydraProcessor extends AudioWorkletProcessor {
  static get parameterDescriptors() {
    return CONTINUOUS_PARAM_SPECS.map(spec => ({
      ...spec,
      automationRate: 'a-rate'
    }));
  }

  constructor(options) {
    super();
    this.ready = false;
    this.bypass = false;
    this.handle = 0;
    this.capacity = 0;
    this.pendingMessages = [];
    
    // Tracks last smoothed parameter values to perform linear interpolation
    this.paramState = new Map();
    for (const spec of CONTINUOUS_PARAM_SPECS) {
      const id = parseInt(spec.name.slice(2), 10);
      this.paramState.set(id, spec.defaultValue);
    }

    this.bypassMixCurrent = 1.0;
    this.bypassMixTarget = 1.0;
    this.bypassRampSamples = Math.max(1, Math.round(sampleRate * 0.01)); // 10ms clickless transition

    const opts = (options && options.processorOptions) || {};
    if (!opts.wasmBinary) {
      throw new Error('HydraProcessor expects a pre-fetched wasmBinary passed via processorOptions.');
    }

    this.port.onmessage = (event) => this.onMessage(event.data);
    this.initWasm(opts.wasmBinary);
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

  async initWasm(wasmBinary) {
    try {
      this.module = await loadHydraRuntime(wasmBinary);
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
      const res = this.api.create(sampleRate, 4000.0, handlePtr);
      if (res !== 0) throw new Error(`DSP instantiation error: ${res}`);

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

  applyContinuousParams(parameters) {
    let anyChanged = false;
    const frames = 128; // standard Web Audio block size
    const smoothingWindowFrames = Math.max(1, Math.floor(sampleRate * 0.004)); // 4ms block smoothing
    const alpha = Math.min(1, frames / smoothingWindowFrames);

    for (const spec of CONTINUOUS_PARAM_SPECS) {
      const paramId = parseInt(spec.name.slice(2), 10);
      const values = parameters[spec.name];
      if (!values || values.length === 0) continue;

      // If values vary within block (automation), use last value as block target; 
      // otherwise take the single static value
      const target = values.length > 1 ? values[values.length - 1] : values[0];
      const current = this.paramState.get(paramId) ?? target;
      
      // Perform smooth ramp to target to mitigate zipper noise on sliders
      const next = current + (target - current) * alpha;
      this.paramState.set(paramId, next);
      this.api.setParameter(this.handle, paramId, next);
      anyChanged = true;
    }
    
    if (anyChanged) {
      this.api.commit(this.handle);
    }
  }

  cleanup() {
    this.ready = false;
    if (this.handle && this.api) {
      this.api.destroy(this.handle);
      this.handle = 0;
    }
    if (this.module) {
      if (this.inL) this.module._free(this.inL);
      if (this.inR) this.module._free(this.inR);
      if (this.outL) this.module._free(this.outL);
      if (this.outR) this.module._free(this.outR);
      this.inL = this.inR = this.outL = this.outR = 0;
    }
  }

  onMessage(msg) {
    if (!msg) return;

    if (msg.type === 'destroy') {
      this.cleanup();
      return;
    }

    if (!this.ready) {
      if (msg.type === 'bypass') {
        this.bypass = !!msg.enabled;
        this.bypassMixTarget = this.bypass ? 0 : 1;
        this.bypassMixCurrent = this.bypassMixTarget;
      } else {
        this.pendingMessages.push(msg);
      }
      return;
    }

    if (msg.type === 'bypass') {
      this.bypass = !!msg.enabled;
      this.bypassMixTarget = this.bypass ? 0 : 1;
      this.port.postMessage({ type: 'stateAck', key: 'bypass', value: this.bypass ? 1 : 0 });
    } else if (msg.type === 'reset') {
      this.api.reset(this.handle);
      this.port.postMessage({ type: 'stateAck', key: 'reset', value: 1 });
    } else if (msg.type === 'command') {
      const { command, value } = msg;
      
      // Map discrete string commands to their C++ engine enum IDs
      const commandMap = {
        delayActive: 11,
        activeHeads: 15,
        headsMusical: 17,
        guitarFocus: 18,
        pingPong: 20,
        freeze: 21,
        reverse: 22,
        reverseSmear: 23,
        spring: 24
      };

      const paramId = commandMap[command];
      if (paramId !== undefined) {
        this.api.setParameter(this.handle, paramId, Number(value));
        this.api.commit(this.handle);
        this.port.postMessage({ type: 'stateAck', key: command, value });
      }
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
      // Dry passthrough during initialization
      output[0].set(inL);
      if (output[1]) output[1].set(inR);
      return true;
    }

    this.ensureBuffers(frames);
    this.applyContinuousParams(parameters);

    // Transfer audio blocks to WASM heap
    this.module.HEAPF32.set(inL, this.inL >> 2);
    this.module.HEAPF32.set(inR, this.inR >> 2);

    // Execute DSP routine
    this.api.process(this.handle, this.inL, this.inR, this.outL, this.outR, frames);

    // Read back from WASM heap
    const dspL = new Float32Array(this.module.HEAPF32.buffer, this.outL, frames);
    const dspR = new Float32Array(this.module.HEAPF32.buffer, this.outR, frames);

    const outL = output[0];
    const outR = output[1] || outL;

    // Apply click-free smooth bypass crossfade
    for (let i = 0; i < frames; i++) {
      if (this.bypassMixCurrent !== this.bypassMixTarget) {
        const delta = this.bypassMixTarget - this.bypassMixCurrent;
        const step = 1.0 / this.bypassRampSamples;
        this.bypassMixCurrent += Math.sign(delta) * Math.min(Math.abs(delta), step);
      }
      const wet = this.bypassMixCurrent;
      const dry = 1.0 - wet;
      outL[i] = dspL[i] * wet + inL[i] * dry;
      outR[i] = dspR[i] * wet + inR[i] * dry;
    }

    return true;
  }
}

registerProcessor('hydra-processor', HydraProcessor);
