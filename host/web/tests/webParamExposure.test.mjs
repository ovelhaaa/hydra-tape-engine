import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';

const PROCESSOR_PATH = new URL('../hydra-processor.js', import.meta.url);
const INDEX_PATH = new URL('../index.html', import.meta.url);
const MAIN_PATH = new URL('../main.js', import.meta.url);

function expectMatch(text, regex, label) {
  assert.match(text, regex, `missing ${label}`);
}

test('web worklet expõe IDs C-API de wow/flutter/hiss como AudioParams contínuos', async () => {
  const source = await readFile(PROCESSOR_PATH, 'utf8');

  expectMatch(source, /\{\s*name:\s*'p_0'[^\n]*\/\/\s*flutterDepth/, 'p_0 flutterDepth');
  expectMatch(source, /\{\s*name:\s*'p_1'[^\n]*\/\/\s*wowDepth/, 'p_1 wowDepth');
  expectMatch(source, /\{\s*name:\s*'p_4'[^\n]*\/\/\s*noise/, 'p_4 noise');
  expectMatch(source, /\{\s*name:\s*'p_9'[^\n]*\/\/\s*flutterRate/, 'p_9 flutterRate');
  expectMatch(source, /\{\s*name:\s*'p_10'[^\n]*\/\/\s*wowRate/, 'p_10 wowRate');
});

test('controles da UI usam os mesmos limites de wow/flutter/hiss definidos para web host', async () => {
  const html = await readFile(INDEX_PATH, 'utf8');

  expectMatch(html, /id="wowDepth"\s+min="0"\s+max="35"\s+step="0\.5"\s+value="10"/, 'wowDepth slider bounds');
  expectMatch(html, /id="flutterDepth"\s+min="0"\s+max="45"\s+step="0\.5"\s+value="14"/, 'flutterDepth slider bounds');
  expectMatch(html, /id="noise"\s+min="0"\s+max="100"\s+step="0\.5"\s+value="30"/, 'noise slider bounds');
  expectMatch(html, /id="wowRate"\s+min="0\.1"\s+max="5\.0"\s+step="0\.05"\s+value="0\.8"/, 'wowRate slider bounds');
  expectMatch(html, /id="flutterRate"\s+min="0\.1"\s+max="20\.0"\s+step="0\.1"\s+value="6\.0"/, 'flutterRate slider bounds');
});

test('main.js roteia wow/flutter/hiss para IDs corretos da API C', async () => {
  const source = await readFile(MAIN_PATH, 'utf8');

  expectMatch(source, /flutterDepth:\s*0,/, 'PARAM.flutterDepth=0');
  expectMatch(source, /wowDepth:\s*1,/, 'PARAM.wowDepth=1');
  expectMatch(source, /noise:\s*4,/, 'PARAM.noise=4');
  expectMatch(source, /flutterRate:\s*9,/, 'PARAM.flutterRate=9');
  expectMatch(source, /wowRate:\s*10,/, 'PARAM.wowRate=10');
});
