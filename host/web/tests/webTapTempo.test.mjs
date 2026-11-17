import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);
const webDir = join(__dirname, '..');

test('index.html contém todos os controles e indicadores para Tap Tempo', () => {
  const html = readFileSync(join(webDir, 'index.html'), 'utf8');

  assert.match(html, /id="transportTapBtn"/, 'deve conter o botão principal de TAP');
  assert.match(html, /id="transportTapResetBtn"/, 'deve conter o botão de reset do estimador');
  assert.match(html, /id="tapTempoStatusBadge"/, 'deve conter badge de status do tap tempo');
  assert.match(html, /id="tapTempoDetail"/, 'deve conter container de detalhes do tap tempo (Est, Taps, Conf)');
});

test('hydra-processor.js expõe comandos e telemetria para Tap Tempo', () => {
  const processorCode = readFileSync(join(webDir, 'hydra-processor.js'), 'utf8');

  assert.match(processorCode, /tapTempoEvent:/, 'deve fazer cwrap de hydra_dsp_tap_tempo_event');
  assert.match(processorCode, /tapTempoGetState:/, 'deve fazer cwrap de hydra_dsp_tap_tempo_get_state');
  assert.match(processorCode, /tapTempoReset:/, 'deve fazer cwrap de hydra_dsp_tap_tempo_reset');
  assert.match(processorCode, /msg\.type === 'tapTempoEvent'/, 'deve tratar mensagem tapTempoEvent');
  assert.match(processorCode, /msg\.type === 'tapTempoReset'/, 'deve tratar mensagem tapTempoReset');
  assert.match(processorCode, /tapTempo\s*[:}]/, 'deve incluir telemetria de tapTempo no postMessage');
});

test('main.js inclui handlers de toque, atalho de teclado e atualização de UI do Tap Tempo', () => {
  const mainCode = readFileSync(join(webDir, 'main.js'), 'utf8');

  assert.match(mainCode, /triggerTapTempo/, 'deve definir função para acionar tap tempo');
  assert.match(mainCode, /tapTempoEvent/, 'deve postar mensagem tapTempoEvent para o worklet');
  assert.match(mainCode, /tapTempoReset/, 'deve postar mensagem tapTempoReset para o worklet');
  assert.match(mainCode, /tapTempoStatusBadge/, 'deve atualizar badge de status');
  assert.match(mainCode, /key === 't' \|\| e\.key === 'T'/, 'deve suportar atalho de teclado T');
});
