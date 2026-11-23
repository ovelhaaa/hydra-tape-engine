import test from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { readFile } from 'node:fs/promises';
import { performance } from 'node:perf_hooks';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

const build = process.env.HYDRA_WEB_BUILD;
const fs = 48000;
const block = 128;

function rms(samples, start = 0, end = samples.length) {
  let sum = 0;
  const count = Math.max(1, end - start);
  for (let i = start; i < end; i++) {
    sum += samples[i] * samples[i];
  }
  return Math.sqrt(sum / count);
}

function sha256Float32(samples) {
  const bytes = Buffer.from(samples.buffer, samples.byteOffset, samples.byteLength);
  return createHash('sha256').update(bytes).digest('hex');
}

test('Classic C WASM processes audible audio, tails, parameter changes, reset, and meets 48kHz budget',
  { skip: build ? false : 'Set HYDRA_WEB_BUILD to the generated web runtime directory', timeout: 20000 },
  async () => {
    const buildInfo = JSON.parse(await readFile(resolve(build, 'hydra-build-info.json'), 'utf8'));
    assert.equal(buildInfo.feedbackRouting, 'PHASE72_CLASSIC_C', 'Web build must be Classic C');

    let Processor;
    globalThis.sampleRate = fs;
    globalThis.AudioWorkletProcessor = class {
      constructor() {
        this.readyMessage = new Promise((resolveReady) => {
          this.port = {
            postMessage: (message) => {
              if (message.type === 'ready' || message.type === 'error') resolveReady(message);
            }
          };
        });
      }
    };
    globalThis.registerProcessor = (_name, processor) => { Processor = processor; };
    await import(pathToFileURL(resolve(build, 'hydra-processor.js')).href);
    assert.ok(Processor);
    const wasmBinary = await readFile(resolve(build, 'hydra_dsp.wasm'));

    async function runScenario({
      blocks = 375, // 1.0s at 48kHz
      impulseBlock = 2,
      activeBlocks = 40,
      params = {},
      commands = {},
      resetAtBlock = -1
    } = {}) {
      const worklet = new Processor({ processorOptions: { wasmBinary } });
      assert.deepEqual(await worklet.readyMessage, { type: 'ready' });
      try {
        worklet.port.onmessage({ data: { type: 'command', command: 'delayActive', value: 1 } });
        for (const [command, value] of Object.entries(commands)) {
          worklet.port.onmessage({ data: { type: 'command', command, value } });
        }

        const audioParams = {
          p_0: [10], p_1: [8], p_2: [0], p_3: [40], p_4: [0],
          p_5: [50], p_6: [40], p_7: [30], p_8: [10], p_9: [6.0], p_10: [0.8],
          p_12: [180], p_13: [55], p_14: [100], p_15: [100], p_17: [120], p_20: [50],
          ...params
        };

        const totalSamples = blocks * block;
        const dryL = new Float32Array(totalSamples);
        const outAllL = new Float32Array(totalSamples);
        const outAllR = new Float32Array(totalSamples);
        const inL = new Float32Array(block);
        const inR = new Float32Array(block);
        const outL = new Float32Array(block);
        const outR = new Float32Array(block);
        const blockTimesMs = [];

        for (let b = 0; b < blocks; b++) {
          if (b === resetAtBlock) {
            worklet.port.onmessage({ data: { type: 'reset', requestId: 1 } });
          }
          for (let i = 0; i < block; i++) {
            const frame = b * block + i;
            let sample = 0;
            if (b === impulseBlock && i < 32) {
              sample = 0.6 * Math.sin((2 * Math.PI * 880 * i) / fs);
            } else if (b > impulseBlock && b < activeBlocks) {
              sample = 0.25 * Math.sin((2 * Math.PI * 220 * frame) / fs);
            }
            inL[i] = sample;
            inR[i] = sample;
            dryL[frame] = sample;
          }
          const t0 = performance.now();
          assert.equal(worklet.process([[inL, inR]], [[outL, outR]], audioParams), true);
          blockTimesMs.push(performance.now() - t0);
          outAllL.set(outL, b * block);
          outAllR.set(outR, b * block);
        }

        for (let i = 0; i < totalSamples; i++) {
          assert.ok(Number.isFinite(outAllL[i]), `Non-finite L sample at ${i}`);
          assert.ok(Number.isFinite(outAllR[i]), `Non-finite R sample at ${i}`);
        }

        return { dryL, outAllL, outAllR, blockTimesMs };
      } finally {
        worklet.port.onmessage({ data: { type: 'destroy' } });
      }
    }

    // 1. Baseline wet processing vs dry
    const base = await runScenario();
    const dryTailStart = 60 * block; // after input ends at block 40
    assert.equal(rms(base.dryL, dryTailStart), 0, 'Dry input must be silent in tail region');
    const tailRms = rms(base.outAllL, dryTailStart);
    assert.ok(tailRms > 1e-4, `Classic C must produce audible delay tail after input ends (rms=${tailRms})`);

    let diffSum = 0;
    for (let i = 0; i < base.dryL.length; i++) {
      const d = base.outAllL[i] - base.dryL[i];
      diffSum += d * d;
    }
    assert.ok(Math.sqrt(diffSum / base.dryL.length) > 1e-3, 'Processed output must differ from dry input');

    // 2. Controlled parameter changes must alter the processed waveform
    const controlsToVerify = [
      { label: 'feedback', params: { p_13: [85] } },
      { label: 'tapeAge', params: { p_6: [95] } },
      { label: 'tone', params: { p_20: [10] } },
      { label: 'activeHeads', commands: { activeHeads: 7 } },
      { label: 'reverse', commands: { reverse: 1 } },
      { label: 'spring', commands: { spring: 1 } },
      { label: 'guitarFocus', commands: { guitarFocus: 1 } }
    ];

    const controlMetrics = {};
    for (const ctrl of controlsToVerify) {
      const variant = await runScenario({ params: ctrl.params || {}, commands: ctrl.commands || {} });
      let deltaSq = 0;
      for (let i = 0; i < base.outAllL.length; i++) {
        const dL = variant.outAllL[i] - base.outAllL[i];
        const dR = variant.outAllR[i] - base.outAllR[i];
        deltaSq += dL * dL + dR * dR;
      }
      const diffRms = Math.sqrt(deltaSq / (base.outAllL.length * 2));
      assert.ok(diffRms > 1e-4, `Control ${ctrl.label} must audibly change DSP output (diffRms=${diffRms})`);
      controlMetrics[ctrl.label] = {
        diffRms: Number(diffRms.toFixed(6)),
        hashL: sha256Float32(variant.outAllL).slice(0, 16)
      };
    }

    // 3. Reset behavior: resetting after input ends clears the delay tail
    const resetRun = await runScenario({ resetAtBlock: 60 });
    const postResetTailRms = rms(resetRun.outAllL, 65 * block);
    assert.ok(postResetTailRms < tailRms * 0.1, `Reset must clear delay tail (before=${tailRms}, after=${postResetTailRms})`);

    // 4. Web block processing performance at 48 kHz (128-frame budget = 2.667 ms)
    const sortedTimes = [...base.blockTimesMs].sort((a, b) => a - b);
    const meanMs = base.blockTimesMs.reduce((a, b) => a + b, 0) / base.blockTimesMs.length;
    const p95Ms = sortedTimes[Math.floor(sortedTimes.length * 0.95)];
    const maxMs = sortedTimes[sortedTimes.length - 1];
    assert.ok(meanMs < 2.667, `Mean block time ${meanMs.toFixed(3)}ms exceeds 48kHz budget 2.667ms`);

    console.log(JSON.stringify({
      architecture: buildInfo.feedbackRouting,
      gitCommit: buildInfo.gitCommit,
      baselineTailRms: Number(tailRms.toFixed(6)),
      postResetTailRms: Number(postResetTailRms.toFixed(6)),
      baselineHashL: sha256Float32(base.outAllL).slice(0, 16),
      baselineHashR: sha256Float32(base.outAllR).slice(0, 16),
      perfMs: {
        budgetMs: 2.667,
        meanMs: Number(meanMs.toFixed(4)),
        p95Ms: Number(p95Ms.toFixed(4)),
        maxMs: Number(maxMs.toFixed(4))
      },
      controlMetrics
    }, null, 2));
  });
