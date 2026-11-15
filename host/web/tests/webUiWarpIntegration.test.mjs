import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';

const PROCESSOR_PATH = new URL('../hydra-processor.js', import.meta.url);
const INDEX_PATH = new URL('../index.html', import.meta.url);
const MAIN_PATH = new URL('../main.js', import.meta.url);
const PRESET_PATH = new URL('../presetSerialization.js', import.meta.url);

function expectMatch(text, regex, label) {
  assert.match(text, regex, `missing ${label}`);
}

test('hydra-processor.js expõe parâmetros e comandos para Accidental Warp', async () => {
  const source = await readFile(PROCESSOR_PATH, 'utf8');

  // Parâmetros contínuos a-rate (AudioParams)
  expectMatch(source, /\{\s*name:\s*'p_31'[^\n]*\/\/\s*warpSpeed/, 'p_31 warpSpeed');
  expectMatch(source, /\{\s*name:\s*'p_32'[^\n]*\/\/\s*warpGrainMs/, 'p_32 warpGrainMs');
  expectMatch(source, /\{\s*name:\s*'p_33'[^\n]*\/\/\s*warpStereo/, 'p_33 warpStereo');
  expectMatch(source, /\{\s*name:\s*'p_34'[^\n]*\/\/\s*warpFeedback/, 'p_34 warpFeedback');

  // Comandos discretos via MessagePort (commandMap)
  expectMatch(source, /warpActive:\s*30/, 'commandMap.warpActive');
  expectMatch(source, /warpMode:\s*35/, 'commandMap.warpMode');
});

test('index.html contém todos os controles da interface para Accidental Warp com limites corretos', async () => {
  const html = await readFile(INDEX_PATH, 'utf8');

  // Toggle switch de ativação
  expectMatch(html, /<input\s+type="checkbox"\s+id="warpActive"\s*\/>/, 'warpActive checkbox');

  // Select dropdown com os 4 modos do Warp
  expectMatch(html, /<select\s+id="warpMode"[^>]*>/, 'warpMode select element');
  expectMatch(html, /<option\s+value="0">2-Taps Musical \(Hermite\)<\/option>/, 'warpMode 0 option');
  expectMatch(html, /<option\s+value="1">1-Tap Bug Cru \(Hermite\)<\/option>/, 'warpMode 1 option');
  expectMatch(html, /<option\s+value="2">1-Tap Bug Cru \(Raw Int Cast\)<\/option>/, 'warpMode 2 option');
  expectMatch(html, /<option\s+value="3">2-Taps Musical \(Raw Int Cast\)<\/option>/, 'warpMode 3 option');

  // Sliders com bounds e defaults
  expectMatch(html, /id="warpSpeed"\s+min="0"\s+max="2\.0"\s+step="0\.01"\s+value="1\.0"/, 'warpSpeed slider');
  expectMatch(html, /id="warpGrainMs"\s+min="20"\s+max="400"\s+step="1"\s+value="80"/, 'warpGrainMs slider');
  expectMatch(html, /id="warpStereo"\s+min="0"\s+max="1\.0"\s+step="0\.01"\s+value="0\.5"/, 'warpStereo slider');
  expectMatch(html, /id="warpFeedback"\s+min="0"\s+max="100"\s+step="0\.5"\s+value="0"/, 'warpFeedback slider');
});

test('main.js sincroniza todos os parâmetros de warp com o AudioWorklet e confirma estado', async () => {
  const source = await readFile(MAIN_PATH, 'utf8');

  expectMatch(source, /warpActive:\s*30,/, 'PARAM.warpActive');
  expectMatch(source, /warpSpeed:\s*31,/, 'PARAM.warpSpeed');
  expectMatch(source, /warpGrainMs:\s*32,/, 'PARAM.warpGrainMs');
  expectMatch(source, /warpStereo:\s*33,/, 'PARAM.warpStereo');
  expectMatch(source, /warpFeedback:\s*34,/, 'PARAM.warpFeedback');
  expectMatch(source, /warpMode:\s*35/, 'PARAM.warpMode');

  // Confirmação de que parâmetros contínuos estão no Set
  expectMatch(source, /'warpSpeed',\s*'warpGrainMs',\s*'warpStereo',\s*'warpFeedback'/, 'CONTINUOUS_PARAM_IDS warp entries');

  // Metadados registrados para mapeamento
  expectMatch(source, /warpActive:\s*\{\s*group:\s*'Warp Mode'/, 'PARAM_METADATA.warpActive');
  expectMatch(source, /warpMode:\s*\{\s*group:\s*'Warp Mode'/, 'PARAM_METADATA.warpMode');

  // Recepção de confirmação de estado (stateAck)
  expectMatch(source, /engineState\[event\.data\.key\]\s*=\s*event\.data\.value;/, 'stateAck confirmation');
});

test('presetSerialization.js inclui valores default e validação de schema para parâmetros Warp', async () => {
  const source = await readFile(PRESET_PATH, 'utf8');

  expectMatch(source, /warpActive:\s*0,/, 'default warpActive');
  expectMatch(source, /warpSpeed:\s*1\.0,/, 'default warpSpeed');
  expectMatch(source, /warpGrainMs:\s*80,/, 'default warpGrainMs');
  expectMatch(source, /warpStereo:\s*0\.5,/, 'default warpStereo');
  expectMatch(source, /warpFeedback:\s*0,/, 'default warpFeedback');
  expectMatch(source, /warpMode:\s*0/, 'default warpMode');

  // Validação de schemas
  expectMatch(source, /warpActive:\s*\{\s*min:\s*0,\s*max:\s*1,\s*isBoolean:\s*true\s*\}/, 'schema warpActive');
  expectMatch(source, /warpSpeed:\s*\{\s*min:\s*0\.0,\s*max:\s*2\.0\s*\}/, 'schema warpSpeed');
  expectMatch(source, /warpGrainMs:\s*\{\s*min:\s*20,\s*max:\s*400\s*\}/, 'schema warpGrainMs');
  expectMatch(source, /warpStereo:\s*\{\s*min:\s*0\.0,\s*max:\s*1\.0\s*\}/, 'schema warpStereo');
  expectMatch(source, /warpFeedback:\s*\{\s*min:\s*0,\s*max:\s*100\s*\}/, 'schema warpFeedback');
  expectMatch(source, /warpMode:\s*\{\s*min:\s*0,\s*max:\s*3\s*\}/, 'schema warpMode');
});
