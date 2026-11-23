import { chromium } from 'playwright';
import { createServer } from 'node:http';
import { readFile, mkdir, writeFile } from 'node:fs/promises';
import { resolve, extname } from 'node:path';
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import assert from 'node:assert/strict';

const root = resolve(import.meta.dirname, '../../..');
const build = resolve(root, process.env.HYDRA_WEB_BUILD || 'build-web/web');
const output = resolve(root, process.env.HYDRA_BROWSER_AUDITION_REPORT || 'artifacts/classic-c-browser');
await mkdir(output, { recursive: true });

const mimeTypes = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.wasm': 'application/wasm',
  '.css': 'text/css; charset=utf-8'
};

function encodeTestWav(sampleRate = 48000, durationSec = 1.5) {
  const frames = Math.floor(sampleRate * durationSec);
  const dataBytes = frames * 4;
  const buf = Buffer.alloc(44 + dataBytes);
  buf.write('RIFF', 0);
  buf.writeUInt32LE(36 + dataBytes, 4);
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
  buf.writeUInt32LE(dataBytes, 40);

  // First 0.25s: crisp harmonic burst; remaining 1.25s: complete silence
  const burstEnd = Math.floor(sampleRate * 0.25);
  for (let i = 0; i < frames; i++) {
    let sample = 0;
    if (i < burstEnd) {
      const env = Math.sin(Math.PI * (i / burstEnd));
      sample = 0.45 * env * (Math.sin((2 * Math.PI * 330 * i) / sampleRate) + 0.5 * Math.sin((2 * Math.PI * 660 * i) / sampleRate));
    }
    const pcm = Math.max(-32767, Math.min(32767, Math.round(sample * 32767)));
    buf.writeInt16LE(pcm, 44 + i * 4);
    buf.writeInt16LE(pcm, 44 + i * 4 + 2);
  }
  return buf;
}

function parseWavTailRms(wavBytes, sampleRate = 48000, tailStartSec = 0.4) {
  const view = new DataView(wavBytes.buffer, wavBytes.byteOffset, wavBytes.byteLength);
  const channels = view.getUint16(22, true);
  const sr = view.getUint32(24, true);
  const dataSize = view.getUint32(40, true);
  const frames = Math.floor(dataSize / (channels * 2));
  const startFrame = Math.floor(tailStartSec * sampleRate);
  let sumSq = 0;
  let count = 0;
  let peak = 0;
  for (let i = startFrame; i < frames; i++) {
    const l = view.getInt16(44 + i * 4, true) / 32768;
    const r = view.getInt16(44 + i * 4 + 2, true) / 32768;
    sumSq += l * l + r * r;
    count += 2;
    peak = Math.max(peak, Math.abs(l), Math.abs(r));
  }
  return { channels, sampleRate: sr, frames, tailRms: Math.sqrt(sumSq / Math.max(1, count)), tailPeak: peak };
}

const server = createServer(async (req, res) => {
  try {
    const reqPath = (req.url || '/').split('?')[0];
    if (reqPath === '/favicon.ico') {
      res.statusCode = 204;
      res.end();
      return;
    }
    const rel = reqPath === '/' ? 'index.html' : reqPath.replace(/^\/+/, '');
    const fullPath = resolve(build, rel);
    if (!fullPath.startsWith(build)) throw new Error('Forbidden');
    const data = await readFile(fullPath);
    res.setHeader('Content-Type', mimeTypes[extname(fullPath)] || 'application/octet-stream');
    res.setHeader('Cache-Control', 'no-store');
    res.end(data);
  } catch {
    res.statusCode = 404;
    res.end('Not found');
  }
});

await new Promise((r) => server.listen(0, '127.0.0.1', r));
const browser = await chromium.launch({
  headless: true,
  ...(process.env.HYDRA_BROWSER_CHANNEL ? { channel: process.env.HYDRA_BROWSER_CHANNEL } : {}),
  args: ['--autoplay-policy=no-user-gesture-required']
});

const consoleErrors = [];
const pageErrors = [];

try {
  const page = await browser.newPage();
  page.on('console', (msg) => {
    if (msg.type() === 'error') consoleErrors.push(msg.text());
  });
  page.on('pageerror', (err) => pageErrors.push(String(err)));

  await page.goto(`http://127.0.0.1:${server.address().port}/`, { waitUntil: 'networkidle' });

  // 1. Verify architecture badge
  await page.waitForFunction(() => {
    const el = document.getElementById('architectureBadge');
    return el && el.textContent && !el.textContent.includes('checking build');
  });
  const archText = await page.textContent('#architectureBadge');
  assert.equal(archText?.trim(), 'DSP: Classic C (WASM)');

  // 2. Upload test WAV file
  const wavBuf = encodeTestWav(48000, 1.5);
  const dryStats = parseWavTailRms(wavBuf, 48000, 0.4);
  assert.equal(dryStats.tailRms, 0, 'Dry input tail must be silent');

  await page.setInputFiles('#fileInput', {
    name: 'phase73_probe.wav',
    mimeType: 'audio/wav',
    buffer: wavBuf
  });
  await page.waitForFunction(() => {
    const st = document.getElementById('status');
    return st && st.textContent && st.textContent.includes('Loaded: phase73_probe.wav');
  });

  // 3. Start AudioContext & AudioWorklet + Play
  await page.click('#startBtn');
  await page.waitForFunction(() => {
    const st = document.getElementById('status');
    return st && st.textContent && (st.textContent.includes('Audio engine started') || st.textContent.includes('WASM initialized'));
  });

  await page.click('#playBtn');
  await page.waitForFunction(() => {
    const st = document.getElementById('status');
    const badge = document.getElementById('previewBadge');
    return (badge && badge.textContent === 'Preview Active') || (st && st.textContent && st.textContent.includes('Playback active.'));
  });

  // 4. Exercise key controls in UI
  const setRange = async (selector, value) => {
    await page.$eval(selector, (el, v) => {
      el.value = String(v);
      el.dispatchEvent(new Event('input', { bubbles: true }));
      el.dispatchEvent(new Event('change', { bubbles: true }));
    }, value);
  };
  await setRange('#delayTimeMs', 220);
  await setRange('#feedback', 65);
  await setRange('#dryWet', 70);
  await setRange('#delayWet', 80);
  await setRange('#tapeAge', 55);
  await setRange('#tone', 45);
  await setRange('#activeHeads', 4);

  // Toggle discrete switches and restore after verifying no error
  await page.click('#guitarFocus');
  await page.click('#spring');
  await page.click('#reverse');
  await page.waitForTimeout(80);
  await page.click('#reverse');

  // 5. Execute offline render and inspect exported WAV
  await page.click('#offlineBtn');
  await page.waitForFunction(() => {
    const link = document.getElementById('downloadLink');
    return link && link.style.display !== 'none' && link.href && link.href.startsWith('blob:');
  }, { timeout: 20000 });

  const exportedBytes = await page.evaluate(async () => {
    const href = document.getElementById('downloadLink').href;
    const res = await fetch(href);
    const ab = await res.arrayBuffer();
    return Array.from(new Uint8Array(ab));
  });
  const exportedBuf = Buffer.from(exportedBytes);
  const wetStats = parseWavTailRms(exportedBuf, 48000, 0.4);

  assert.equal(wetStats.sampleRate, 48000);
  assert.equal(wetStats.channels, 2);
  assert.ok(wetStats.tailRms > 1e-3, `Offline export must contain wet Classic C delay tail (tailRms=${wetStats.tailRms})`);
  assert.equal(consoleErrors.length, 0, `Console errors: ${consoleErrors.join('; ')}`);
  assert.equal(pageErrors.length, 0, `Page errors: ${pageErrors.join('; ')}`);

  const hash = (p) => readFile(p).then((d) => createHash('sha256').update(d).digest('hex'));
  const report = {
    status: 'PASS',
    checkoutSha: execFileSync('git', ['rev-parse', 'HEAD'], { cwd: root, encoding: 'utf8' }).trim(),
    browser: browser.version(),
    architectureBadge: archText?.trim(),
    wasmSha256: await hash(resolve(build, 'hydra_dsp.wasm')),
    dryTailRms: dryStats.tailRms,
    offlineWetTailRms: Number(wetStats.tailRms.toFixed(6)),
    offlineWetTailPeak: Number(wetStats.tailPeak.toFixed(6)),
    offlineSampleRate: wetStats.sampleRate,
    offlineFrames: wetStats.frames,
    consoleErrors,
    pageErrors
  };
  await writeFile(resolve(output, 'result.json'), JSON.stringify(report, null, 2));
  console.log(JSON.stringify(report, null, 2));
} catch (error) {
  await writeFile(resolve(output, 'result.json'), JSON.stringify({
    status: 'FAIL',
    browser: browser.version(),
    consoleErrors,
    pageErrors,
    error: error.stack || String(error)
  }, null, 2));
  throw error;
} finally {
  await browser.close();
  server.close();
}
