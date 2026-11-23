// Calibrated Quadrature Demodulator for Phase 7.4B Wow/Flutter Analysis

export function demodulatePitchCents(signal, {
  sampleRate = 48000,
  carrierHz = 1000.0,
  settleSec = 0.60,
  filterWarmupSec = 0.25,
  lpfCutoffHz = 35.0,
  minAmplitude = 1e-4
} = {}) {
  const frames = signal.length;
  const decimate = Math.max(1, Math.round(sampleRate / 1000));
  const demodRate = sampleRate / decimate;

  // 2-stage cascaded 1st-order IIR lowpass (-12 dB/oct) to suppress 2*carrierHz ripple
  const alpha = 1.0 - Math.exp((-2.0 * Math.PI * lpfCutoffHz) / sampleRate);

  const settleSamples = Math.floor(settleSec * sampleRate);
  const warmupSamples = Math.floor(filterWarmupSec * sampleRate);
  const analysisStart = settleSamples + warmupSamples;
  if (analysisStart >= frames - decimate * 16) {
    throw new Error(`Signal too short (${frames} samples) for settle=${settleSec}s + warmup=${filterWarmupSec}s`);
  }

  let i1 = 0, q1 = 0, i2 = 0, q2 = 0;
  const phases = [];
  const validFlags = [];

  for (let i = settleSamples; i < frames; i++) {
    const arg = (2.0 * Math.PI * carrierHz * i) / sampleRate;
    const iMix = signal[i] * Math.cos(arg);
    const qMix = -signal[i] * Math.sin(arg);

    i1 += alpha * (iMix - i1);
    q1 += alpha * (qMix - q1);
    i2 += alpha * (i1 - i2);
    q2 += alpha * (q1 - q2);

    if (i >= analysisStart && ((i - analysisStart) % decimate === 0)) {
      const mag = Math.hypot(i2, q2);
      if (Number.isFinite(mag) && mag >= minAmplitude) {
        phases.push(Math.atan2(q2, i2));
        validFlags.push(true);
      } else {
        phases.push(0);
        validFlags.push(false);
      }
    }
  }

  const centsList = [];
  const phaseUnwrapped = [];
  let acc = 0;
  let hasPrev = false;
  let prevPhase = 0;
  let invalidSamples = 0;

  for (let k = 0; k < phases.length; k++) {
    if (!validFlags[k]) {
      invalidSamples++;
      hasPrev = false;
      continue;
    }
    if (!hasPrev) {
      prevPhase = phases[k];
      acc = prevPhase;
      phaseUnwrapped.push(acc);
      hasPrev = true;
      continue;
    }
    let dp = phases[k] - prevPhase;
    while (dp > Math.PI) dp -= 2.0 * Math.PI;
    while (dp < -Math.PI) dp += 2.0 * Math.PI;
    prevPhase = phases[k];
    acc += dp;
    phaseUnwrapped.push(acc);

    const dfHz = (dp * demodRate) / (2.0 * Math.PI);
    const ratio = 1.0 + dfHz / carrierHz;
    if (ratio > 0.01 && Number.isFinite(ratio)) {
      centsList.push(1200.0 * Math.log2(ratio));
    } else {
      invalidSamples++;
    }
  }

  return {
    cents: Float64Array.from(centsList),
    phaseUnwrapped: Float64Array.from(phaseUnwrapped),
    demodRate,
    decimate,
    invalidSamples,
    effectiveSamples: centsList.length,
    effectiveDurationSec: centsList.length / demodRate
  };
}

export function analyzeModulation(left, right, options = {}) {
  const sampleRate = options.sampleRate ?? 48000;
  const carrierHz = options.carrierHz ?? 1000.0;
  const L = demodulatePitchCents(left, options);
  const R = demodulatePitchCents(right, options);

  const cL = L.cents;
  const cR = R.cents;
  const n = Math.min(cL.length, cR.length);
  if (n < 64) {
    throw new Error(`Insufficient valid demodulated samples: ${n}`);
  }

  let sumL = 0, sumR = 0;
  for (let i = 0; i < n; i++) {
    sumL += cL[i];
    sumR += cR[i];
  }
  const meanL = sumL / n;
  const meanR = sumR / n;

  let sumSq = 0, maxAbs = 0, sumLR = 0, sumL2 = 0, sumR2 = 0;
  const absSorted = new Float64Array(n);
  for (let i = 0; i < n; i++) {
    const vL = cL[i] - meanL;
    const vR = cR[i] - meanR;
    sumSq += vL * vL;
    maxAbs = Math.max(maxAbs, Math.abs(vL));
    absSorted[i] = Math.abs(vL);
    sumLR += vL * vR;
    sumL2 += vL * vL;
    sumR2 += vR * vR;
  }
  absSorted.sort();

  const rmsCents = Math.sqrt(sumSq / n);
  const p50Cents = absSorted[Math.floor(n * 0.50)];
  const p90Cents = absSorted[Math.floor(n * 0.90)];
  const p99Cents = absSorted[Math.floor(n * 0.99)];
  const modCorrelationLR = (sumL2 > 1e-12 && sumR2 > 1e-12) ? sumLR / Math.sqrt(sumL2 * sumR2) : 1.0;

  let minP = Infinity, maxP = -Infinity;
  for (let i = 0; i < L.phaseUnwrapped.length; i++) {
    minP = Math.min(minP, L.phaseUnwrapped[i]);
    maxP = Math.max(maxP, L.phaseUnwrapped[i]);
  }
  const maxExcursionMs = ((maxP - minP) * 0.5 / (2.0 * Math.PI * carrierHz)) * 1000.0;

  // Perturbation events (> max(2.0, 2.5 * rmsCents))
  const thresh = Math.max(2.0, 2.5 * rmsCents);
  let eventCount = 0, inEvent = false, curDur = 0, maxDurMs = 0;
  const msPerStep = 1000.0 / L.demodRate;
  for (let i = 0; i < n; i++) {
    if (Math.abs(cL[i] - meanL) > thresh) {
      if (!inEvent) { eventCount++; inEvent = true; curDur = 1; }
      else curDur++;
    } else if (inEvent) {
      maxDurMs = Math.max(maxDurMs, curDur * msPerStep);
      inEvent = false;
    }
  }
  if (inEvent) {
    maxDurMs = Math.max(maxDurMs, curDur * msPerStep);
  }

  // Hann-windowed DFT for wow (0.1-3 Hz) and flutter (3-25 Hz) bands
  const fftN = Math.min(n, 8192);
  let winSqSum = 0;
  const windowed = new Float64Array(fftN);
  for (let t = 0; t < fftN; t++) {
    const w = 0.5 * (1.0 - Math.cos((2.0 * Math.PI * t) / (fftN - 1)));
    windowed[t] = (cL[t] - meanL) * w;
    winSqSum += w * w;
  }

  let wowEnergy = 0, flutterEnergy = 0;
  let peakFreqHz = 0, peakPower = -1;
  const maxBin = Math.floor((25.0 * fftN) / L.demodRate);
  for (let k = 1; k <= maxBin; k++) {
    const freq = (k * L.demodRate) / fftN;
    let re = 0, im = 0;
    for (let t = 0; t < fftN; t++) {
      const ang = (2.0 * Math.PI * k * t) / fftN;
      re += windowed[t] * Math.cos(ang);
      im -= windowed[t] * Math.sin(ang);
    }
    // Two-sided to one-sided power normalization (factor of 2 for positive frequencies)
    const pow = (2.0 * (re * re + im * im)) / (fftN * winSqSum);
    if (pow > peakPower) {
      peakPower = pow;
      peakFreqHz = freq;
    }
    if (freq >= 0.1 && freq < 3.0) wowEnergy += pow;
    else if (freq >= 3.0 && freq <= 25.0) flutterEnergy += pow;
  }

  return {
    sampleRate,
    demodRate: L.demodRate,
    spectralResolutionHz: L.demodRate / fftN,
    effectiveWindowSec: fftN / L.demodRate,
    invalidSamples: L.invalidSamples + R.invalidSamples,
    meanOffsetCents: meanL,
    rmsCents,
    maxAbsCents: maxAbs,
    p50Cents,
    p90Cents,
    p99Cents,
    maxExcursionMs,
    wowBandRmsCents: Math.sqrt(wowEnergy),
    flutterBandRmsCents: Math.sqrt(flutterEnergy),
    peakModFreqHz: peakFreqHz,
    transientEventCount: eventCount,
    maxEventDurationMs: maxDurMs,
    modCorrelationLR
  };
}
