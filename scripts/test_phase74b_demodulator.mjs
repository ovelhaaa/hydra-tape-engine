import assert from 'node:assert/strict';
import { analyzeModulation } from './phase74b_demodulator.mjs';

function synthFmCarrier({ sampleRate, durationSec, carrierHz = 1000.0, modFreqHz = 0, peakCents = 0, staticOffsetHz = 0, sagCents = 0, sagAtSec = 4.0, sagWidthSec = 0.08 }) {
  const frames = Math.floor(durationSec * sampleRate);
  const sig = new Float32Array(frames);
  let phase = 0;
  for (let i = 0; i < frames; i++) {
    const t = i / sampleRate;
    let cents = 0;
    if (peakCents !== 0 && modFreqHz > 0) {
      cents += peakCents * Math.sin(2.0 * Math.PI * modFreqHz * t);
    }
    if (sagCents !== 0) {
      const dt = (t - sagAtSec) / sagWidthSec;
      cents += sagCents * Math.exp(-0.5 * dt * dt);
    }
    const instFreq = (carrierHz + staticOffsetHz) * Math.pow(2.0, cents / 1200.0);
    phase += (2.0 * Math.PI * instFreq) / sampleRate;
    sig[i] = 0.35 * Math.sin(phase);
  }
  return sig;
}

for (const sr of [48000, 44100]) {
  // 1. Negative control: pure unmodulated 1000 Hz sine
  const pure = synthFmCarrier({ sampleRate: sr, durationSec: 9.0 });
  const resPure = analyzeModulation(pure, pure, { sampleRate: sr });
  assert.ok(resPure.rmsCents < 0.01, `SR=${sr}: pure sine rmsCents=${resPure.rmsCents} >= 0.01`);
  assert.ok(resPure.maxAbsCents < 0.02, `SR=${sr}: pure sine maxAbsCents=${resPure.maxAbsCents} >= 0.02`);

  // 2. Positive control: 0.8 Hz wow, 10 cents peak (7.071 cents RMS)
  const wow = synthFmCarrier({ sampleRate: sr, durationSec: 9.5, modFreqHz: 0.8, peakCents: 10.0 });
  const resWow = analyzeModulation(wow, wow, { sampleRate: sr });
  const expectedWowRms = 10.0 / Math.SQRT2;
  assert.ok(Math.abs(resWow.rmsCents - expectedWowRms) < 0.25, `SR=${sr}: wow rms=${resWow.rmsCents} vs ${expectedWowRms}`);
  assert.ok(Math.abs(resWow.peakModFreqHz - 0.8) < 0.15, `SR=${sr}: wow peakFreq=${resWow.peakModFreqHz}`);
  assert.ok(resWow.wowBandRmsCents > resWow.flutterBandRmsCents * 10.0, `SR=${sr}: wow leaked to flutter`);

  // 3. Positive control: 6.0 Hz flutter, 8 cents peak (5.657 cents RMS)
  const flt = synthFmCarrier({ sampleRate: sr, durationSec: 9.5, modFreqHz: 6.0, peakCents: 8.0 });
  const resFlt = analyzeModulation(flt, flt, { sampleRate: sr });
  const expectedFltRms = 8.0 / Math.SQRT2;
  assert.ok(Math.abs(resFlt.rmsCents - expectedFltRms) < 0.40, `SR=${sr}: flutter rms=${resFlt.rmsCents} vs ${expectedFltRms}`);
  assert.ok(Math.abs(resFlt.peakModFreqHz - 6.0) < 0.15, `SR=${sr}: flutter peakFreq=${resFlt.peakModFreqHz}`);
  assert.ok(resFlt.flutterBandRmsCents > resFlt.wowBandRmsCents * 10.0, `SR=${sr}: flutter leaked to wow`);

  // 4. Positive control: static pitch offset (+5 Hz => +8.634 cents)
  const offset = synthFmCarrier({ sampleRate: sr, durationSec: 9.0, staticOffsetHz: 5.0 });
  const resOff = analyzeModulation(offset, offset, { sampleRate: sr });
  const expectedOffsetCents = 1200.0 * Math.log2(1005.0 / 1000.0);
  assert.ok(Math.abs(resOff.meanOffsetCents - expectedOffsetCents) < 0.05, `SR=${sr}: offset=${resOff.meanOffsetCents} vs ${expectedOffsetCents}`);
  assert.ok(resOff.rmsCents < 0.02, `SR=${sr}: static offset should not inflate AC rmsCents (${resOff.rmsCents})`);

  // 5. Positive control: transient speed sag (-15 cents)
  const sag = synthFmCarrier({ sampleRate: sr, durationSec: 9.0, sagCents: -15.0, sagAtSec: 4.5, sagWidthSec: 0.08 });
  const resSag = analyzeModulation(sag, sag, { sampleRate: sr });
  assert.ok(resSag.maxAbsCents > 13.0 && resSag.maxAbsCents < 16.5, `SR=${sr}: sag maxAbs=${resSag.maxAbsCents}`);
  assert.ok(resSag.transientEventCount >= 1, `SR=${sr}: sag eventCount=${resSag.transientEventCount}`);

  console.log(`PASS demodulator calibration (${sr} Hz): pure=${resPure.rmsCents.toFixed(5)}c, wow=${resWow.rmsCents.toFixed(2)}c@${resWow.peakModFreqHz.toFixed(2)}Hz, flt=${resFlt.rmsCents.toFixed(2)}c@${resFlt.peakModFreqHz.toFixed(2)}Hz, offset=${resOff.meanOffsetCents.toFixed(3)}c, sagMax=${resSag.maxAbsCents.toFixed(2)}c`);
}
