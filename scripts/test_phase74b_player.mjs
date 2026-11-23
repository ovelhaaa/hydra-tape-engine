import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import vm from 'node:vm';

const root = resolve(import.meta.dirname, '..');
const outRoot = resolve(root, process.env.HYDRA_PHASE74B_OUT || 'artifacts/phase7-4b');
const htmlPath = resolve(outRoot, 'comparisons/index.html');

function createMockElement(id = '') {
  const classes = new Set();
  return {
    id,
    value: '0',
    textContent: '',
    innerHTML: '',
    checked: true,
    style: {},
    dataset: {},
    children: [],
    classList: {
      add: (c) => classes.add(c),
      remove: (c) => classes.delete(c),
      contains: (c) => classes.has(c)
    },
    appendChild(child) {
      this.children.push(child);
      return child;
    },
    onclick: null,
    onchange: null
  };
}

async function main() {
  const html = await readFile(htmlPath, 'utf8');
  const scriptMatch = html.match(/<script>([\s\S]*)<\/script>/);
  assert.ok(scriptMatch, 'Missing <script> block in comparisons/index.html');
  const scriptCode = scriptMatch[1];

  const elements = new Map();
  const getEl = (id) => {
    if (!elements.has(id)) elements.set(id, createMockElement(id));
    return elements.get(id);
  };
  for (const id of [
    'scenarioSelect', 'variantBtns', 'playBtn', 'stopBtn', 'loopChk',
    'blindBtn', 'revealBtn', 'statusText', 'scenarioMeta', 'probeTable'
  ]) {
    getEl(id);
  }

  const consoleErrors = [];
  const createdSources = [];
  const createdGains = [];

  class MockAudioContext {
    constructor() {
      this.currentTime = 1.0;
      this.state = 'running';
      this.destination = { kind: 'destination' };
    }
    resume() { this.state = 'running'; return Promise.resolve(); }
    async decodeAudioData(arr) {
      assert.ok(arr.byteLength > 44, 'WAV buffer must be > 44 bytes');
      return { duration: 5.0, numberOfChannels: 2, sampleRate: 48000, byteLength: arr.byteLength };
    }
    createBufferSource() {
      const src = {
        buffer: null,
        loop: false,
        startedAt: null,
        stopped: false,
        connect(dest) { src.connectedTo = dest; return dest; },
        start(when) { src.startedAt = when; },
        stop() { src.stopped = true; }
      };
      createdSources.push(src);
      return src;
    }
    createGain() {
      const g = {
        gain: {
          value: 1.0,
          setValueAtTime(v) { this.value = v; },
          setTargetAtTime(v) { this.value = v; }
        },
        connect(dest) { return dest; }
      };
      createdGains.push(g);
      return g;
    }
  }

  const windowObj = {
    AudioContext: MockAudioContext
  };

  const sandbox = {
    window: windowObj,
    document: {
      getElementById: (id) => getEl(id),
      createElement: () => createMockElement()
    },
    fetch: async (relUrl) => {
      const clean = relUrl.replace(/^\.\.\//, '');
      const buf = await readFile(resolve(outRoot, clean));
      return {
        arrayBuffer: async () => buf.buffer.slice(buf.byteOffset, buf.byteOffset + buf.byteLength)
      };
    },
    console: {
      log: () => {},
      error: (...args) => consoleErrors.push(args.join(' '))
    },
    Math
  };

  vm.createContext(sandbox);
  vm.runInContext(scriptCode, sandbox);

  // Wait for initial loadScenario(0)
  await new Promise(r => setTimeout(r, 80));

  const getState = () => windowObj.__hydraLabState();
  const assertGainsMatchActiveSlot = (ctxMsg) => {
    const st = getState();
    for (let i = 0; i < st.slotOrder.length; i++) {
      const k = st.slotOrder[i];
      const expected = (i === st.activeSlot) ? 1.0 : 0.0;
      assert.equal(st.gainTargets[k], expected, `${ctxMsg}: slot ${i} (${k}) gain=${st.gainTargets[k]} expected=${expected}`);
    }
  };

  // 1. Enable blind mode BEFORE Play, then Play
  getEl('blindBtn').onclick();
  assert.equal(getState().blindMode, true);
  await getEl('playBtn').onclick();
  assert.equal(getState().isPlaying, true);
  assertGainsMatchActiveSlot('blind before play');

  // 2. Rapidly switch A/B/C/D
  const btns = () => getEl('variantBtns').children;
  for (const idx of [1, 3, 0, 2, 1]) {
    btns()[idx].onclick();
    assert.equal(getState().activeSlot, idx);
    assertGainsMatchActiveSlot(`switch to slot ${idx}`);
  }

  // 3. Reveal identities
  getEl('revealBtn').onclick();
  assert.equal(getState().revealed, true);
  assertGainsMatchActiveSlot('after reveal');

  // 4. Disable blind mode DURING Play
  getEl('blindBtn').onclick();
  assert.equal(getState().blindMode, false);
  assert.deepEqual(Array.from(getState().slotOrder), ['m0', 'm1', 'm2', 'm3']);
  assert.equal(getState().isPlaying, true, 'should remain playing without interruption');
  assertGainsMatchActiveSlot('disable blind during play');

  // 5. Enable blind mode DURING Play
  getEl('blindBtn').onclick();
  assert.equal(getState().blindMode, true);
  assert.equal(getState().isPlaying, true, 'should remain playing when enabling blind mode');
  assertGainsMatchActiveSlot('enable blind during play');

  // 6. Switch scenario (stops audio, loads scenario 1), then restart playback
  getEl('scenarioSelect').value = '1';
  await getEl('scenarioSelect').onchange();
  assert.equal(getState().isPlaying, false);
  await getEl('playBtn').onclick();
  assert.equal(getState().isPlaying, true);
  assertGainsMatchActiveSlot('after scenario switch & restart');

  // Verify synchronized start timestamps on last 4 created sources
  const last4 = createdSources.slice(-4);
  assert.equal(last4.length, 4);
  const t0 = last4[0].startedAt;
  assert.ok(last4.every(s => s.startedAt === t0 && s.loop === true), 'all 4 sources must start synchronized');

  assert.equal(consoleErrors.length, 0, `Unexpected console errors: ${consoleErrors.join('; ')}`);
  console.log('PASS Blind A/B Player simulation test (all 7 scenarios verified, 0 console errors)');
}

main().catch((err) => {
  console.error('Player test FAILED:', err);
  process.exit(1);
});
