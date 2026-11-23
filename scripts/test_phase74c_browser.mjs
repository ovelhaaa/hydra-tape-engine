import assert from 'node:assert/strict';
import http from 'node:http';
import fs from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const ROOT = path.resolve(__dirname, '..');
const WEB_ROOT = path.resolve(process.env.HYDRA_WEB_BUILD || path.join(ROOT, 'build-web', 'web'));

const MIME_TYPES = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'application/javascript; charset=utf-8',
  '.mjs': 'application/javascript; charset=utf-8',
  '.wasm': 'application/wasm',
  '.json': 'application/json; charset=utf-8',
  '.css': 'text/css; charset=utf-8'
};

function createTestWavBuffer(sampleRate = 48000, durationSec = 1.0) {
  const numSamples = Math.floor(sampleRate * durationSec);
  const dataSize = numSamples * 2 * 2; // stereo 16-bit
  const buf = Buffer.alloc(44 + dataSize);
  buf.write('RIFF', 0);
  buf.writeUInt32LE(36 + dataSize, 4);
  buf.write('WAVE', 8);
  buf.write('fmt ', 12);
  buf.writeUInt32LE(16, 16);
  buf.writeUInt16LE(1, 20);
  buf.writeUInt16LE(2, 22);
  buf.writeUInt32LE(sampleRate, 24);
  buf.writeUInt32LE(sampleRate * 4, 28);
  buf.writeUInt16LE(4, 32);
  buf.writeUInt16LE(16, 34);
  buf.write('data', 36);
  buf.writeUInt32LE(dataSize, 40);
  for (let i = 0; i < numSamples; i++) {
    const t = i / sampleRate;
    const env = Math.exp(-t * 2.5);
    const s = Math.sin(2 * Math.PI * 220 * t) * 0.45 * env;
    const pcm = Math.max(-32767, Math.min(32767, Math.round(s * 32767)));
    buf.writeInt16LE(pcm, 44 + i * 4);
    buf.writeInt16LE(pcm, 44 + i * 4 + 2);
  }
  return buf;
}

async function startServer() {
  const server = http.createServer(async (req, res) => {
    const reqPath = (req.url || '/').split('?')[0];
    const rel = reqPath === '/' ? 'index.html' : reqPath.replace(/^\/+/, '');
    const fullPath = path.resolve(WEB_ROOT, rel);
    if (!fullPath.startsWith(WEB_ROOT)) {
      res.writeHead(403);
      res.end('Forbidden');
      return;
    }
    try {
      const data = await fs.readFile(fullPath);
      const ext = path.extname(fullPath);
      res.writeHead(200, { 'Content-Type': MIME_TYPES[ext] || 'application/octet-stream' });
      res.end(data);
    } catch {
      res.writeHead(404);
      res.end('Not found');
    }
  });
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  const addr = server.address();
  return { server, url: `http://127.0.0.1:${addr.port}` };
}

const playwrightPath = pathToFileURL(
  path.join(ROOT, 'tools', 'phase6-listening-lab', 'node_modules', 'playwright', 'index.mjs')
).href;
const { chromium } = await import(playwrightPath);

const { server, url } = await startServer();
const browser = await chromium.launch({
  headless: true,
  args: ['--autoplay-policy=no-user-gesture-required']
});

try {
  const page = await browser.newPage({ viewport: { width: 1280, height: 800 } });
  const consoleErrors = [];
  const pageErrors = [];
  page.on('console', (msg) => {
    if (msg.type() === 'error') consoleErrors.push(msg.text());
  });
  page.on('pageerror', (err) => pageErrors.push(err.message));

  await page.goto(url, { waitUntil: 'networkidle' });

  // 1. Verify binary identity badge and dynamic parameter count
  const archText = await page.locator('#architectureBadge').textContent();
  assert.match(archText || '', /Classic C \+ Unified Motion/);
  const paramSummaryText = await page.locator('#dspParamCountSummary').textContent();
  assert.match(paramSummaryText || '', /47 Parameters Mapped/);

  // 2. Verify all 4 mode buttons + keyboard navigation + advanced panel visibility
  const modes = [
    { idx: 0, label: 'CLASSIC', summary: 'Advanced — Classic' },
    { idx: 1, label: 'MOTOR', summary: 'Advanced — Motor Drift' },
    { idx: 2, label: 'CAPSTAN', summary: 'Advanced — Capstan Flutter' },
    { idx: 3, label: 'WORN', summary: 'Advanced — Worn Transport' }
  ];

  // Expand Advanced section
  await page.locator('#motionAdvancedSummary').click();

  for (const m of modes) {
    const btn = page.locator(`.motion-mode-btn[data-motion-mode="${m.idx}"]`);
    await btn.click();
    assert.equal(await btn.getAttribute('aria-checked'), 'true');
    assert.equal(await page.locator('#motionMode').inputValue(), String(m.idx));
    assert.equal(await page.locator('#motionAdvancedSummary').textContent(), m.summary);
    for (let k = 0; k < 4; k++) {
      const panelState = await page.locator(`.motion-advanced-panel[data-motion-adv="${k}"]`).evaluate((el) => {
        const cs = getComputedStyle(el);
        return { hidden: el.hidden, display: cs.display, height: el.getBoundingClientRect().height };
      });
      assert.equal(panelState.hidden, k !== m.idx, `Panel ${k} hidden attr mismatch in mode ${m.idx}`);
      if (k !== m.idx) {
        assert.equal(panelState.display, 'none', `Panel ${k} computed display must be none in mode ${m.idx}`);
        assert.equal(panelState.height, 0, `Panel ${k} must reserve zero height when hidden in mode ${m.idx}`);
      } else {
        assert.notEqual(panelState.display, 'none', `Panel ${k} must be visible in mode ${m.idx}`);
      }
    }
  }

  // Set mode-specific advanced settings and verify preservation across switches
  await page.locator('.motion-mode-btn[data-motion-mode="1"]').click();
  await page.locator('#motionInertia').fill('82');
  await page.locator('#motionInertia').dispatchEvent('input');
  await page.locator('.motion-mode-btn[data-motion-mode="3"]').click();
  await page.locator('#motionInstability').fill('77');
  await page.locator('#motionInstability').dispatchEvent('input');
  await page.locator('.motion-mode-btn[data-motion-mode="1"]').click();
  assert.equal(await page.locator('#motionInertia').inputValue(), '82');

  // Keyboard ArrowRight navigation from mode 1 -> 2 -> 3
  const btn1 = page.locator('.motion-mode-btn[data-motion-mode="1"]');
  await btn1.focus();
  await page.keyboard.press('ArrowRight');
  assert.equal(await page.locator('#motionMode').inputValue(), '2');
  await page.keyboard.press('ArrowRight');
  assert.equal(await page.locator('#motionMode').inputValue(), '3');

  // 3. Load WAV, start audio, switch modes under high feedback, verify no restart & real telemetry
  const wavBuf = createTestWavBuffer(48000, 1.5);
  await page.locator('#fileInput').setInputFiles({
    name: 'test_guitar.wav',
    mimeType: 'audio/wav',
    buffer: wavBuf
  });
  await page.locator('#feedback').fill('85');
  await page.locator('#feedback').dispatchEvent('input');
  await page.locator('#startBtn').click();
  await page.waitForTimeout(250);
  await page.locator('#playBtn').click();
  await page.waitForTimeout(250);

  // Switch modes live during playback
  for (const m of [0, 1, 2, 3, 0]) {
    await page.locator(`.motion-mode-btn[data-motion-mode="${m}"]`).click();
    await page.waitForTimeout(120);
  }
  const telemetryL = await page.locator('#motionTelemetryValueL').textContent();
  const telemetryR = await page.locator('#motionTelemetryValueR').textContent();
  assert.match(telemetryL || '', /smp$/);
  assert.match(telemetryR || '', /smp$/);
  const modeStatusText = await page.locator('#motionModeStatus').textContent();
  assert.match(modeStatusText || '', /^DSP: CLASSIC$/);

  // 4. Offline WAV export with Worn Transport (3)
  await page.locator('.motion-mode-btn[data-motion-mode="3"]').click();
  await page.locator('#offlineBtn').click();
  await page.locator('#downloadLink').waitFor({ state: 'visible', timeout: 10000 });
  const downloadHref = await page.locator('#downloadLink').getAttribute('href');
  assert.ok(downloadHref && downloadHref.startsWith('blob:'), 'Offline export should produce blob WAV');

  // 5. Preset import (V2 with motionMode=2, then legacy V1 -> Classic 0)
  const v2Preset = JSON.stringify({
    version: 2,
    name: 'Capstan Test',
    engineParams: { motionMode: 2, motionAmount: 80, stereoMotion: 60, motionTexture: 70 }
  });
  await page.locator('#importPresetInput').setInputFiles({
    name: 'capstan.json',
    mimeType: 'application/json',
    buffer: Buffer.from(v2Preset, 'utf8')
  });
  await page.waitForTimeout(100);
  assert.equal(await page.locator('#motionMode').inputValue(), '2');
  assert.equal(await page.locator('#motionAmount').inputValue(), '80');

  const v1Preset = JSON.stringify({
    version: 1,
    name: 'Legacy V1',
    engineParams: { drive: 50, wowDepth: 12, flutterDepth: 16 },
    fxChain: [{ type: 'tapeDelay', enabled: true, params: { delayTimeMs: 400, feedback: 35, dryWet: 50 } }]
  });
  await page.locator('#importPresetInput').setInputFiles({
    name: 'legacy_v1.json',
    mimeType: 'application/json',
    buffer: Buffer.from(v1Preset, 'utf8')
  });
  await page.waitForTimeout(100);
  assert.equal(await page.locator('#motionMode').inputValue(), '0');

  // 6. Mobile viewport responsiveness check: 2x2 button grid and no internal card overflow
  await page.setViewportSize({ width: 390, height: 844 });
  const overflowCheck = await page.locator('#tapeMotionCard').evaluate((el) => ({
    scrollW: el.scrollWidth,
    clientW: el.clientWidth
  }));
  assert.ok(overflowCheck.scrollW <= overflowCheck.clientW + 1,
    `Card internal overflow: scrollWidth=${overflowCheck.scrollW} > clientWidth=${overflowCheck.clientW}`);
  const box0 = await page.locator('.motion-mode-btn[data-motion-mode="0"]').boundingBox();
  const box2 = await page.locator('.motion-mode-btn[data-motion-mode="2"]').boundingBox();
  assert.ok(box0 && box2 && box2.y > box0.y, 'Mode buttons should wrap to 2x2 grid on 390px mobile');

  assert.deepEqual(consoleErrors, [], `Console errors: ${consoleErrors.join('; ')}`);
  assert.deepEqual(pageErrors, [], `Page errors: ${pageErrors.join('; ')}`);
  console.log('Phase 7.4C Playwright browser test PASSED');
} finally {
  await browser.close();
  server.close();
}
