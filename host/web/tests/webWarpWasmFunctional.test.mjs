import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

const build = process.env.HYDRA_WEB_BUILD;
const fs = 48000;
const block = 128;

test('web host routes Warp commands and AudioParams into the built WASM DSP',
  { skip: build ? false : 'Set HYDRA_WEB_BUILD to the generated web runtime directory', timeout: 10000 },
  async () => {
    let Processor;
    globalThis.sampleRate = fs;
    globalThis.AudioWorkletProcessor = class {
      constructor() {
        this.readyMessage = new Promise((resolveReady) => {
          this.port = { postMessage: (message) => {
            if (message.type === 'ready' || message.type === 'error') resolveReady(message);
          } };
        });
      }
    };
    globalThis.registerProcessor = (_name, processor) => { Processor = processor; };
    await import(pathToFileURL(resolve(build, 'hydra-processor.js')).href);
    assert.ok(Processor);
    const wasmBinary = await readFile(resolve(build, 'hydra_dsp.wasm'));

    async function render({ active, mode, speed, feedback, impulse = false }) {
      const worklet = new Processor({ processorOptions: { wasmBinary } });
      assert.deepEqual(await worklet.readyMessage, { type: 'ready' });
      try {
        for (const [command, value] of Object.entries({ delayActive: 1, warpActive: active, warpMode: mode })) {
          worklet.port.onmessage({ data: { type: 'command', command, value } });
        }
        const param = {
          p_4: [0], p_12: [80], p_13: [0], p_14: [100], p_15: [100],
          p_31: [speed], p_33: [0], p_34: [feedback]
        };
        for (const name of ['p_31', 'p_34']) {
          assert.ok(Processor.parameterDescriptors.some((spec) => spec.name === name));
        }

        const samples = new Float32Array(block * 256);
        const inL = new Float32Array(block);
        const outL = new Float32Array(block);
        const outR = new Float32Array(block);
        for (let start = 0; start < samples.length; start += block) {
          for (let i = 0; i < block; i++) {
            const frame = start + i;
            inL[i] = impulse ? (frame === 4096 ? 0.8 : 0) : 0.4 * Math.sin(2 * Math.PI * 440 * frame / fs);
          }
          assert.equal(worklet.process([[inL, inL]], [[outL, outR]], param), true);
          samples.set(outL, start);
        }
        assert.ok(samples.every(Number.isFinite));

        const ptr = worklet.module._malloc(4);
        try {
          for (const [id, expected] of [[30, active], [31, speed], [34, feedback], [35, mode]]) {
            assert.equal(worklet.module._hydra_dsp_get_parameter(worklet.handle, id, ptr), 0);
            assert.ok(Math.abs(worklet.module.HEAPF32[ptr >> 2] - expected) < 1e-5, `DSP parameter ${id}`);
          }
        } finally {
          worklet.module._free(ptr);
        }
        return samples;
      } finally {
        worklet.port.onmessage({ data: { type: 'destroy' } });
      }
    }

    function bandEnergy(samples, centerHz) {
      let energy = 0;
      for (let hz = centerHz - 15; hz <= centerHz + 15; hz += 5) {
        let real = 0, imaginary = 0;
        for (let i = 12288; i < samples.length; i++) {
          const angle = 2 * Math.PI * hz * i / fs;
          real += samples[i] * Math.cos(angle);
          imaginary += samples[i] * Math.sin(angle);
        }
        energy += real ** 2 + imaginary ** 2;
      }
      return energy;
    }

    const baseline = { active: 1, mode: 0, speed: 1, feedback: 0 };
    const inactive = await render({ ...baseline, active: 0 });
    const octave = await render(baseline);
    const fifth = await render({ ...baseline, speed: 0.5 });
    assert.ok(bandEnergy(inactive, 440) > 10 * bandEnergy(inactive, 880));
    assert.ok(bandEnergy(octave, 880) > 10 * bandEnergy(octave, 440));
    assert.ok(bandEnergy(fifth, 660) > 10 * bandEnergy(fifth, 880));

    const solo = await render({ ...baseline, mode: 2 });
    function energy(samples, start = 0) {
      return samples.subarray(start).reduce((sum, sample) => sum + sample ** 2, 0);
    }
    assert.ok(energy(solo) > 0.01, 'raw solo mode should produce wet audio');
    assert.ok(energy(solo.map((sample, i) => sample - octave[i])) > 0.01, 'mode should change the waveform');

    const impulse = await render({ ...baseline, speed: 0.5, impulse: true });
    const repeats = await render({ ...baseline, speed: 0.5, feedback: 80, impulse: true });
    assert.ok(energy(repeats, 13000) > 2 * energy(impulse, 13000), 'feedback should sustain the tail');
  });
