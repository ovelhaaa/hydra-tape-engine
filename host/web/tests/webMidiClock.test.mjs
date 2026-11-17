import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);
const webDir = join(__dirname, '..');

test('index.html contém todos os controles e indicadores para MIDI Clock e Tempo Source', () => {
  const html = readFileSync(join(webDir, 'index.html'), 'utf8');

  assert.match(html, /id="tempoSourceGroup"/, 'deve conter o grupo seletor de origem de andamento');
  assert.match(html, /id="tempoSourceInternalBtn"/, 'deve conter botão de fonte INTERNAL');
  assert.match(html, /id="tempoSourceTapBtn"/, 'deve conter botão de fonte TAP');
  assert.match(html, /id="tempoSourceMidiBtn"/, 'deve conter botão de fonte MIDI EXT');
  assert.match(html, /id="midiClockStatusBadge"/, 'deve conter badge de status do MIDI clock');
  assert.match(html, /id="midiInputSelect"/, 'deve conter seletor de porta Web MIDI');
  assert.match(html, /id="midiClockResetBtn"/, 'deve conter botão de reset do estimador MIDI');
  assert.match(html, /id="midiClockDetail"/, 'deve conter painel de detalhes (BPM, fase, jitter)');
});

test('hydra-processor.js expõe comandos e telemetria para MIDI Clock e Tempo Source', () => {
  const processorCode = readFileSync(join(webDir, 'hydra-processor.js'), 'utf8');

  assert.match(processorCode, /midiClockEvent:/, 'deve fazer cwrap de hydra_dsp_midi_clock_event');
  assert.match(processorCode, /midiClockGetState:/, 'deve fazer cwrap de hydra_dsp_midi_clock_get_state');
  assert.match(processorCode, /midiClockReset:/, 'deve fazer cwrap de hydra_dsp_midi_clock_reset');
  assert.match(processorCode, /setTempoSource:/, 'deve fazer cwrap de hydra_dsp_set_tempo_source');
  assert.match(processorCode, /msg\.type === 'midiClockEvent'/, 'deve tratar mensagem midiClockEvent');
  assert.match(processorCode, /msg\.type === 'midiClockReset'/, 'deve tratar mensagem midiClockReset');
  assert.match(processorCode, /msg\.type === 'setTempoSource'/, 'deve tratar mensagem setTempoSource');
  assert.match(processorCode, /midiClock\s*[:}]/, 'deve incluir telemetria de midiClock no postMessage');
});

test('main.js inclui handlers Web MIDI, seletor de porta e atualização de telemetria MIDI Clock', () => {
  const mainCode = readFileSync(join(webDir, 'main.js'), 'utf8');

  assert.match(mainCode, /tempoSourceInternalBtn/, 'deve registrar listener para botão INTERNAL');
  assert.match(mainCode, /tempoSourceTapBtn/, 'deve registrar listener para botão TAP');
  assert.match(mainCode, /tempoSourceMidiBtn/, 'deve registrar listener para botão MIDI');
  assert.match(mainCode, /requestMIDIAccess/, 'deve inicializar Web MIDI API');
  assert.match(mainCode, /0xF8/, 'deve capturar mensagens Timing Clock 0xF8');
  assert.match(mainCode, /0xFA/, 'deve capturar mensagens Start 0xFA');
  assert.match(mainCode, /0xFB/, 'deve capturar mensagens Continue 0xFB');
  assert.match(mainCode, /0xFC/, 'deve capturar mensagens Stop 0xFC');
  assert.match(mainCode, /midiClockStatusBadge/, 'deve atualizar badge de status MIDI Clock');
  assert.match(mainCode, /midiClockResetBtn/, 'deve permitir reset do estimador MIDI');
});
