#!/usr/bin/env node
/**
 * Accidental Warp Simulator Engine Runner.
 *
 * Dynamically extracts and executes the exact JavaScript AudioWorkletProcessor class
 * from docs/simulators/accidental_warp_simulator.html on raw Float32 audio buffers.
 */

const fs = require('fs');
const path = require('path');

const [,, inPath, paramsPath, outPath] = process.argv;
if (!inPath || !paramsPath || !outPath) {
  console.error('Usage: node run_simulator_worklet.js <in.f32> <params.json> <out.f32>');
  process.exit(1);
}

const htmlPath = path.resolve(__dirname, '../docs/simulators/accidental_warp_simulator.html');
const html = fs.readFileSync(htmlPath, 'utf8');
const match = html.match(/<script id="dsp-worklet-code"[^>]*>([\s\S]*?)<\/script>/);
if (!match) {
  console.error('Error: Could not locate <script id="dsp-worklet-code"> in', htmlPath);
  process.exit(1);
}

const params = JSON.parse(fs.readFileSync(paramsPath, 'utf8'));
const sampleRate = params.sampleRate || 48000;

globalThis.sampleRate = sampleRate;
globalThis.AudioWorkletProcessor = class {
  constructor() {
    this.port = {
      postMessage() {},
      onmessage: null
    };
  }
};

let RegisteredClass = null;
globalThis.registerProcessor = (name, cls) => {
  RegisteredClass = cls;
};

// Evaluate the original simulator worklet code in global scope
eval(match[1]);

if (!RegisteredClass) {
  console.error('Error: registerProcessor was not invoked by the simulator script.');
  process.exit(1);
}

const proc = new RegisteredClass();
proc.useExternalInput = true;
proc.melody.enabled = false;
proc.dryWet = 1.0;
proc.masterVolume = 1.0;

const mode = Number(params.mode !== undefined ? params.mode : 0);
const speed = Number(params.speed !== undefined ? params.speed : 1.0);
const grainMs = Number(params.grainMs !== undefined ? params.grainMs : 80.0);
const stereo = Number(params.stereo !== undefined ? params.stereo : 0.5);
const feedback = Number(params.feedback !== undefined ? params.feedback : 0.0);

proc.isMusical2Tap = (mode === 0 || mode === 3);
proc.rawIntCast = (mode === 2 || mode === 3);
proc.speedMultiplier = speed;
proc.stereoWarp = stereo;
proc.feedback = feedback * 0.01;

// Grain window timing
const beatSamples = (grainMs * 0.001) * sampleRate;
proc.delaySamples = Math.max(64, Math.floor(beatSamples));
proc.delaySize = proc.isMusical2Tap ? proc.delaySamples : (proc.delaySamples + 1000);
proc.writePos = 0.0;
if (proc.delayL.writeIdx >= proc.delaySize) proc.delayL.writeIdx = 0;
if (proc.delayR.writeIdx >= proc.delaySize) proc.delayR.writeIdx = 0;

// Read input raw floats (interleaved stereo L, R)
const inBuf = fs.readFileSync(inPath);
const totalFloats = inBuf.length / 4;
const totalFrames = totalFloats / 2;
const inFloats = new Float32Array(inBuf.buffer, inBuf.byteOffset, totalFloats);

const outFloats = new Float32Array(totalFloats);

const BLOCK = 128;
const inL = new Float32Array(BLOCK);
const inR = new Float32Array(BLOCK);
const outL = new Float32Array(BLOCK);
const outR = new Float32Array(BLOCK);

for (let frame = 0; frame < totalFrames; frame += BLOCK) {
  const curFrames = Math.min(BLOCK, totalFrames - frame);
  for (let i = 0; i < curFrames; i++) {
    inL[i] = inFloats[(frame + i) * 2];
    inR[i] = inFloats[(frame + i) * 2 + 1];
  }
  for (let i = curFrames; i < BLOCK; i++) {
    inL[i] = 0;
    inR[i] = 0;
  }

  proc.process([[inL, inR]], [[outL, outR]]);

  for (let i = 0; i < curFrames; i++) {
    outFloats[(frame + i) * 2] = outL[i];
    outFloats[(frame + i) * 2 + 1] = outR[i];
  }
}

fs.writeFileSync(outPath, Buffer.from(outFloats.buffer, outFloats.byteOffset, outFloats.byteLength));
