export const STATES = ['NOT REVIEWED', 'PASS', 'NEEDS ATTENTION', 'FAIL'];
export const CRITERIA = ['Rhythmic clarity', 'Natural repeats', 'Continuity', 'Clicks', 'Tail preservation', 'Feedback stability', 'Reverse musicality', 'Tape character'];
export const BIAS_CRITERIA = ['Residual whine', 'Artificial harshness', 'Natural highs', 'Magnetic compression', 'Musical harmonics', 'Transients', 'Tape character', 'Feedback stability', 'Reverse musicality', 'Stereo coherence'];
export const isBiasPackage = manifest => manifest?.suite === 'Hydra Phase 6.2 Bias A/B';
const SUITE_SCENARIO_COUNTS = {'Hydra Phase 6.2 Bias A/B': 60, 'Hydra Phase 6.3 Reverse A/B': 10, 'Hydra Phase 6.4 Final A/B': 10};
export const reviewCriteria = manifest => isBiasPackage(manifest) ? BIAS_CRITERIA : CRITERIA;
export const reviewStorageKey = manifest => `hydra-lab:${manifest.reference_commit}:${manifest.suite ?? 'musical-validation'}`;
export function auditionDetails(manifest, scenario) {
  if (!isBiasPackage(manifest)) return {a:'Original', b:'Processed', group:scenario.group, title:scenario.name};
  const variants = {bias_off:'Bias off (diagnostic)', sine:'Sine bias', bandlimited:'Band-limited bias', oversampled:'Local 2× oversampling', averaged:'Phase average (candidate)'};
  const variant = variants[scenario.comparison_variant] ?? scenario.comparison_variant;
  return {a:'Triangular baseline', b:variant, group:scenario.comparison_title ?? scenario.name.split(': baseline vs ')[0], title:variant};
}

export function validateManifest(manifest) {
  if (manifest.schema_version !== 1 || !/^[a-f0-9]{40}$/.test(manifest.reference_commit ?? '')) throw Error('Invalid manifest provenance/schema');
  const entries = Object.entries(manifest.scenarios ?? {});
  if (!(manifest.sample_rate_hz > 0)) throw Error('Missing package sample rate');
  const expected = SUITE_SCENARIO_COUNTS[manifest.suite] ?? 20;
  if (entries.length !== expected || entries.some(([id], i) => Number(id.slice(0, 2)) !== i + 1)) throw Error(`Expected ${expected} ordered scenarios`);
  for (const [, s] of entries) {
    if (!(s.duration_sec > 0 && s.sample_rate_hz === manifest.sample_rate_hz) || !s.dsp_params || !s.analysis_metrics || !Array.isArray(s.markers)) throw Error('Invalid scenario metadata');
    for (const kind of ['original_wav', 'processed_wav', 'telemetry_csv']) {
      const path = s[`${kind}_file`];
      if (typeof path !== 'string' || path.includes('..') || /[:\\]/.test(path) || path.startsWith('/') || !/^[a-f0-9]{64}$/.test(s[`${kind}_sha256`] ?? '')) throw Error('Invalid file path/hash');
    }
  }
  return manifest;
}

export async function verifiedFile(file, expected, cryptoAPI = globalThis.crypto) {
  if (!file || file.size === 0) throw Error('Missing or empty package file');
  const bytes = await file.arrayBuffer();
  const hash = Array.from(new Uint8Array(await cryptoAPI.subtle.digest('SHA-256', bytes)), b => b.toString(16).padStart(2, '0')).join('');
  if (hash !== expected) throw Error(`SHA-256 mismatch: ${file.name}`);
  return bytes;
}

export function parseTelemetry(text) {
  const lines = text.trim().split(/\r?\n/).map(line => line.split(','));
  if (!lines[0].includes('time_sec') || lines.length < 2) throw Error('Invalid telemetry CSV');
  const rows = lines.slice(1).map(values => Object.fromEntries(lines[0].map((key, i) => [key, Number(values[i])])));
  if (rows.some((r, i) => Object.values(r).some(v => !Number.isFinite(v)) || (i && r.time_sec <= rows[i - 1].time_sec))) throw Error('Invalid telemetry values/timestamps');
  return rows;
}

export function telemetryEvents(rows) {
  const events = [];
  const labels = {tempo_source: ['Internal', 'Tap Tempo', 'MIDI Clock'], midi_status: ['Disconnected', 'Acquiring', 'Locked', 'Holdover', 'Lost'], is_playing: ['Stopped', 'Playing']};
  const names = {tempo_source: 'Tempo source', midi_status: 'MIDI Clock', is_playing: 'Transport'};
  for (let i = 1; i < rows.length; i++) {
    for (const key of ['tempo_source', 'midi_status', 'is_playing']) {
      if (rows[i][key] !== rows[i - 1][key]) events.push({time: rows[i].time_sec, label: `${names[key]}: ${labels[key][rows[i - 1][key]] ?? rows[i - 1][key]} → ${labels[key][rows[i][key]] ?? rows[i][key]} (telemetry)`});
    }
  }
  return events;
}

// Both buffers always share a start time, offset and loop boundaries. A/B only changes gains.
export class Audition {
  constructor(context) {
    this.context = context; this.buffers = []; this.sources = []; this.offset = 0; this.playing = false;
    this.version = 0; this.loop = false; this.start = 0; this.end = 0; this.generation = 0;
    this.master = context.createGain(); this.master.gain.value = 0.35; this.master.connect(context.destination);
    this.gains = [context.createGain(), context.createGain()];
    this.gains.forEach((g, i) => { g.gain.value = i === 0 ? 1 : 0; g.connect(this.master); });
  }
  get duration() { return this.buffers[0]?.duration ?? 0; }
  get position() {
    let p = this.offset + (this.playing ? Math.max(0, this.context.currentTime - this.started) : 0);
    if (this.loop && this.end > this.start && p >= this.end) p = this.start + (p - this.start) % (this.end - this.start);
    return Math.min(this.duration, p);
  }
  load(buffers) {
    this.stop(); this.buffers = [];
    if (buffers.length !== 2 || buffers[0].length !== buffers[1].length || buffers[0].sampleRate !== buffers[1].sampleRate) throw Error('A/B buffers must share frame count and sample rate');
    this.buffers = buffers; this.start = 0; this.end = this.duration; this.loop = false;
  }
  async play() {
    if (!this.duration || this.playing) return;
    const generation = this.generation;
    await this.context.resume();
    if (generation !== this.generation || this.playing || !this.duration) return;
    if (this.offset >= this.duration) this.offset = 0;
    if (this.loop && (this.offset < this.start || this.offset >= this.end)) this.offset = this.start;
    this.started = this.context.currentTime + 0.015;
    this.playing = true;
    this.sources = this.buffers.map((buffer, i) => {
      const source = this.context.createBufferSource(); source.buffer = buffer;
      source.loop = this.loop; source.loopStart = this.start; source.loopEnd = this.end;
      source.connect(this.gains[i]); source.start(this.started, this.offset); return source;
    });
    const first = this.sources[0];
    first.onended = () => { if (this.sources[0] === first && !this.loop) { this.pause(); this.offset = this.duration; } };
  }
  pause() {
    this.generation++;
    this.offset = this.position; this.playing = false;
    this.sources.forEach(s => { s.onended = null; s.stop(); s.disconnect(); }); this.sources = [];
  }
  stop() { this.pause(); this.offset = 0; }
  seek(position) { const active = this.playing; this.pause(); this.offset = Math.max(0, Math.min(this.duration, position)); if (active) return this.play(); }
  setLoop(enabled, start = 0, end = this.duration) {
    if (!(start >= 0 && end <= this.duration && end > start)) throw Error('Invalid loop bounds');
    const active = this.playing; this.pause(); this.loop = enabled; this.start = start; this.end = end;
    if (active) return this.play();
  }
  select(version) {
    this.version = version;
    const now = this.context.currentTime;
    // Complementary linear ramps sum to unity: no equal-power boost or hidden normalization.
    this.gains.forEach((gain, i) => { gain.gain.cancelAndHoldAtTime(now); gain.gain.linearRampToValueAtTime(i === version ? 1 : 0, now + 0.008); });
  }
}

export function exportReviews(manifest, reviews) {
  return JSON.stringify({schema_version: 1, reference_commit: manifest.reference_commit, package_hashes: packageHashes(manifest), reviews}, null, 2);
}
function packageHashes(manifest) {
  return Object.fromEntries(Object.entries(manifest.scenarios).map(([id, s]) => [id, ['original_wav','processed_wav','telemetry_csv'].map(kind => s[`${kind}_sha256`] ?? null)]));
}
export function importReviews(text, manifest) {
  const data = JSON.parse(text);
  if (data.schema_version !== 1 || data.reference_commit !== manifest.reference_commit || !data.reviews || Array.isArray(data.reviews)) throw Error('Reviews belong to another commit or invalid schema');
  if (JSON.stringify(data.package_hashes) !== JSON.stringify(packageHashes(manifest))) throw Error('Reviews belong to different audio/telemetry content');
  for (const [id, review] of Object.entries(data.reviews)) {
    if (!manifest.scenarios[id] || !STATES.includes(review.state) || typeof review.comments !== 'string' || !Array.isArray(review.notes) || !review.criteria || typeof review.criteria !== 'object') throw Error('Invalid review');
    for (const [criterion, state] of Object.entries(review.criteria)) if (![...CRITERIA, ...reviewCriteria(manifest)].includes(criterion) || !['N/A', ...STATES].includes(state)) throw Error('Invalid criterion');
    for (const note of review.notes) if (typeof note.description !== 'string' || !Number.isFinite(note.time) || note.time < 0 || note.time > manifest.scenarios[id].duration_sec || !['low', 'medium', 'high'].includes(note.severity)) throw Error('Invalid timestamped note');
  }
  return data.reviews;
}
