/**
 * Hydra Tape Engine 2.0 — Phase 6 Web/WASM Performance Benchmark.
 *
 * Evaluates WASM DSP execution in AudioWorklet environment:
 *   - Sample rates: 44.1 kHz & 48.0 kHz
 *   - Block size: 128 frames
 *   - Scenarios:
 *       A: Tape Echo Base (Single Head, Tape saturation, Wow/Flutter)
 *       B: Multi-Head Delay (3 heads, stereo ping-pong)
 *       C: Reverse Tape Echo V2 (Rhythmic 1/4 note reverse)
 *       D: Full Stress Chain (Delay + Multi-head + Reverse V2 + Warp + Spring + MIDI Clock)
 *   - Metrics: Mean, P50, P95, P99, Peak execution time (µs), Budget Load %, Dropouts
 */

import { readFile } from 'node:fs/promises';
import { resolve, dirname } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import os from 'node:os';
import {execFileSync} from 'node:child_process';
import {createHash} from 'node:crypto';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);
const rootDir = resolve(__dirname, '..');
const webBuildDir = process.env.HYDRA_WEB_BUILD || resolve(rootDir, 'build-web/web');

const BLOCK = 128;
const NUM_BLOCKS = 3000; // ~8 seconds of audio per benchmark scenario

function calculatePercentiles(latenciesUs) {
  const sorted = [...latenciesUs].sort((a, b) => a - b);
  const n = sorted.length;
  const mean = sorted.reduce((a, b) => a + b, 0) / n;
  const p50 = sorted[Math.floor(n * 0.50)];
  const p95 = sorted[Math.floor(n * 0.95)];
  const p99 = sorted[Math.floor(n * 0.99)];
  const peak = sorted[n - 1];
  return { mean, p50, p95, p99, peak };
}

let CachedProcessor = globalThis.__hydraProcessorClass || null;

async function getProcessor() {
  if (CachedProcessor) return CachedProcessor;
  if (globalThis.__hydraProcessorClass) {
    CachedProcessor = globalThis.__hydraProcessorClass;
    return CachedProcessor;
  }

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

  globalThis.registerProcessor = (_name, proc) => {
    globalThis.__hydraProcessorClass = proc;
    CachedProcessor = proc;
  };

  const moduleUrl = pathToFileURL(resolve(webBuildDir, 'hydra-processor.js')).href;
  await import(moduleUrl);

  CachedProcessor = CachedProcessor || globalThis.__hydraProcessorClass;
  if (!CachedProcessor) {
    throw new Error("Failed to register HydraProcessor from hydra-processor.js");
  }
  return CachedProcessor;
}

async function runBenchmarkForRate(sampleRate) {
  const budgetUs = (BLOCK / sampleRate) * 1e6;
  console.log(`\n================================================================`);
  console.log(`WEB/WASM BENCHMARK @ ${sampleRate} Hz (Block Budget: ${budgetUs.toFixed(1)} µs)`);
  console.log(`================================================================`);

  globalThis.sampleRate = sampleRate;
  const Processor = await getProcessor();
  const wasmBinary = await readFile(resolve(webBuildDir, 'hydra_dsp.wasm'));

  const scenarios = [
    {
      id: "A_tape_echo_base",
      name: "A. Tape Echo Base (Single Head, Wow/Flutter, Tape Saturation)",
      commands: { delayActive: 1, reverse: 0, warpActive: 0, spring: 0, activeHeads: 4 },
      params: { p_11: [1], p_12: [500], p_13: [35], p_14: [50], p_15: [50], p_0: [10], p_1: [8], p_3: [25] }
    },
    {
      id: "B_multi_head_stereo",
      name: "B. Multi-Head Delay (3 Heads Musical + Ping-Pong)",
      commands: { delayActive: 1, reverse: 0, warpActive: 0, spring: 0, activeHeads: 7, headsMusical: 1, pingPong: 1 },
      params: { p_11: [1], p_12: [500], p_13: [40], p_14: [50], p_15: [50], p_16: [7], p_18: [1], p_21: [1] }
    },
    {
      id: "C_reverse_v2",
      name: "C. Reverse Tape Echo V2 (Rhythmic Quarter-Note Reverse)",
      commands: { delayActive: 1, reverse: 1, reverseMode: 0, reverseDivision: 0, warpActive: 0, spring: 0 },
      params: { p_11: [1], p_23: [1], p_13: [35], p_14: [50], p_15: [75], p_36: [0], p_37: [0] }
    },
    {
      id: "D_combined_stress_chain",
      name: "D. Full Stress Chain (Delay + Multi-Head + Reverse + Warp + Spring)",
      commands: { delayActive: 1, reverse: 1, reverseMode: 0, reverseDivision: 0, warpActive: 1, warpMode: 0, spring: 1, activeHeads: 7 },
      params: { p_11: [1], p_12: [400], p_13: [50], p_14: [50], p_23: [1], p_30: [1], p_31: [1.0], p_34: [40], p_25: [1], p_28: [35] }
    }
  ];

  const results = [];

  for (const scen of scenarios) {
    const worklet = new Processor({ processorOptions: { wasmBinary } });
    await worklet.readyMessage;

    // Set initial commands
    for (const [cmd, val] of Object.entries(scen.commands)) {
      worklet.port.onmessage({ data: { type: 'command', command: cmd, value: val } });
    }

    const inL = new Float32Array(BLOCK);
    const inR = new Float32Array(BLOCK);
    const outL = new Float32Array(BLOCK);
    const outR = new Float32Array(BLOCK);

    for (let i = 0; i < BLOCK; i++) {
      inL[i] = 0.3 * Math.sin(2.0 * Math.PI * 440.0 * i / sampleRate);
      inR[i] = 0.3 * Math.cos(2.0 * Math.PI * 440.0 * i / sampleRate);
    }

    // Warmup 100 blocks
    for (let b = 0; b < 100; b++) {
      worklet.process([[inL, inR]], [[outL, outR]], scen.params);
    }

    // Measured run
    const latenciesUs = [];
    let dropouts = 0;

    for (let b = 0; b < NUM_BLOCKS; b++) {
      // Simulate real-time parameter variations every 256 blocks
      if (b % 256 === 0) {
        worklet.port.onmessage({
          data: { type: 'command', command: 'delayTimeMs', value: 300 + (b % 400) }
        });
      }

      const t0 = performance.now();
      const ok = worklet.process([[inL, inR]], [[outL, outR]], scen.params);
      const dtUs = (performance.now() - t0) * 1000.0;
      latenciesUs.push(dtUs);

      if (!ok || dtUs > budgetUs) {
        dropouts++;
      }
    }

    const stats = calculatePercentiles(latenciesUs);
    const budgetPctMean = (stats.mean / budgetUs) * 100.0;
    const budgetPctP99 = (stats.p99 / budgetUs) * 100.0;

    results.push({
      scenario: scen.id,
      name: scen.name,
      sampleRate,
      budgetUs,
      stats,
      budgetPctMean,
      budgetPctP99,
      dropouts
    });

    console.log(`Scenario: ${scen.name}`);
    console.log(`  Mean: ${stats.mean.toFixed(2)} µs (${budgetPctMean.toFixed(2)}% budget)`);
    console.log(`  P50:  ${stats.p50.toFixed(2)} µs | P95: ${stats.p95.toFixed(2)} µs | P99: ${stats.p99.toFixed(2)} µs`);
    console.log(`  Peak: ${stats.peak.toFixed(2)} µs | Dropouts: ${dropouts} / ${NUM_BLOCKS}`);
  }

  return results;
}

async function main() {
  const r48 = await runBenchmarkForRate(48000);
  const r44 = await runBenchmarkForRate(44100);

  const report = {
    suite: "Hydra Tape Engine 2.0 — Web/WASM Performance Audit",
    timestamp: new Date().toISOString(),
    classification: 'B — WASM / Node.js; simulated AudioWorklet globals; not browser real-time evidence',
    environment: {
      node: process.version, os: `${os.platform()} ${os.release()} ${os.arch()}`,
      cpu: os.cpus()[0]?.model,
      emscripten: process.env.HYDRA_EMSCRIPTEN_VERSION || 'NOT RECORDED (set HYDRA_EMSCRIPTEN_VERSION from the build toolchain)',
      commit: execFileSync('git', ['rev-parse', 'HEAD'], {cwd: rootDir, encoding: 'utf8'}).trim(),
      wasm_sha256: createHash('sha256').update(await readFile(resolve(webBuildDir, 'hydra_dsp.wasm'))).digest('hex')
    },
    blocks: NUM_BLOCKS * 8,
    block_frames: BLOCK,
    simulated_seconds: {rate_48000: NUM_BLOCKS * 4 * BLOCK / 48000, rate_44100: NUM_BLOCKS * 4 * BLOCK / 44100},
    limitations: 'process() wall time and block-budget overruns in Node; no browser scheduler, device latency, I2S, or physical dropout measurement. No allocation instrumentation.',
    benchmark_48k: r48,
    benchmark_44k: r44
  };

  const outPath = resolve(rootDir, 'artifacts/listening-lab/phase6-musical-validation/wasm_performance_report.json');
  const { writeFile } = await import('node:fs/promises');
  await writeFile(outPath, JSON.stringify(report, null, 2));
  console.log(`\nWASM performance benchmark saved to ${outPath}`);
}

main().catch((err) => {
  console.error("Benchmark failed:", err);
  process.exit(1);
});
