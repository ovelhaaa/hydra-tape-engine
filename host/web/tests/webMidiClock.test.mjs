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
  assert.match(mainCode, /convertMidiTimestampToAudioFrames/, 'deve importar e utilizar conversor de timestamp Web MIDI');
});

test('convertMidiTimestampToAudioFrames preserva precisão temporal a 44.1 kHz e 48 kHz', async () => {
  const { convertMidiTimestampToAudioFrames } = await import('../webMidiTime.js');

  // Cenário 1: Evento ocorrido há 10ms (atraso de escalonamento da thread principal)
  // Host now = 5000.0 ms, event timestamp = 4990.0 ms (dt = 10ms = 0.01s)
  // Audio currentTime = 5.0s @ 48000 Hz -> audio frame at now = 240,000
  // Frame real do evento = (5.0 - 0.01) * 48000 = 4.99 * 48000 = 239,520 frames
  const f48 = convertMidiTimestampToAudioFrames(4990.0, 5000.0, 5.0, 48000.0);
  assert.equal(f48, 239520, 'deve mapear exatamente 10ms de latência para 480 frames a 48kHz');

  // A 44.1 kHz: 4.99 * 44100 = 220,059 frames
  const f44 = convertMidiTimestampToAudioFrames(4990.0, 5000.0, 5.0, 44100.0);
  assert.equal(f44, 220059, 'deve mapear exatamente 10ms de latência para 441 frames a 44.1kHz');
});

test('convertMidiTimestampToAudioFrames preserva intervalos exatos de 120 BPM e rejeita jitter do MessagePort', async () => {
  const { convertMidiTimestampToAudioFrames } = await import('../webMidiTime.js');

  // A 120 BPM, período de clock (24 PPQN) = 60 / (24 * 120) = 20.8333... ms
  // Suponha 3 pulsos recebidos pelo driver MIDI a cada 20.8333 ms:
  // p1 = 1000.00 ms
  // p2 = 1020.8333 ms
  // p3 = 1041.6666 ms
  // Mas a thread principal travou e entregou todos no MessagePort no instante now = 1060.0 ms!
  // Audio currentTime no momento da entrega = 2.0s @ 48000 Hz
  const sampleRate = 48000.0;
  const hostNow = 1060.0;
  const audioTime = 2.0;

  const f1 = convertMidiTimestampToAudioFrames(1000.0, hostNow, audioTime, sampleRate);
  const f2 = convertMidiTimestampToAudioFrames(1020.833333, hostNow, audioTime, sampleRate);
  const f3 = convertMidiTimestampToAudioFrames(1041.666666, hostNow, audioTime, sampleRate);

  const delta1 = f2 - f1;
  const delta2 = f3 - f2;

  // Esperado a 48kHz: 0.020833333 * 48000 = exatamente 1000 frames!
  assert.equal(delta1, 1000, 'delta entre p1 e p2 deve ser exatamente 1000 frames apesar do atraso da entrega');
  assert.equal(delta2, 1000, 'delta entre p2 e p3 deve ser exatamente 1000 frames apesar do atraso da entrega');
});

test('convertMidiTimestampToAudioFrames política de fallback para timestamps inválidos', async () => {
  const { convertMidiTimestampToAudioFrames } = await import('../webMidiTime.js');
  const now = 5000.0;
  const audioTime = 3.0;
  const sampleRate = 48000.0;
  const expectedFallback = Math.round(audioTime * sampleRate); // 144,000

  // 1. Timestamp ausente / undefined / null
  assert.equal(convertMidiTimestampToAudioFrames(undefined, now, audioTime, sampleRate), expectedFallback);
  assert.equal(convertMidiTimestampToAudioFrames(null, now, audioTime, sampleRate), expectedFallback);

  // 2. Timestamp NaN
  assert.equal(convertMidiTimestampToAudioFrames(NaN, now, audioTime, sampleRate), expectedFallback);

  // 3. Timestamp infinito
  assert.equal(convertMidiTimestampToAudioFrames(Infinity, now, audioTime, sampleRate), expectedFallback);

  // 4. Timestamp negativo ou zero
  assert.equal(convertMidiTimestampToAudioFrames(-100.0, now, audioTime, sampleRate), expectedFallback);
  assert.equal(convertMidiTimestampToAudioFrames(0, now, audioTime, sampleRate), expectedFallback);

  // 5. Timestamp no futuro (> now + 50ms)
  assert.equal(convertMidiTimestampToAudioFrames(now + 1000.0, now, audioTime, sampleRate), expectedFallback);

  // 6. Mensagem muito antiga (> 3s)
  assert.equal(convertMidiTimestampToAudioFrames(now - 4000.0, now, audioTime, sampleRate), expectedFallback);
});

test('processBatchMidiEvents preserva ordem temporal e garante monotonicidade', async () => {
  const { processBatchMidiEvents } = await import('../webMidiTime.js');

  const events = [
    { byte: 0xF8, timeStamp: 1000.0 },
    { byte: 0xF8, timeStamp: 1020.8333 },
    { byte: 0xF8, timeStamp: 1041.6666 }
  ];

  const processed = processBatchMidiEvents(events, 1050.0, 2.0, 48000.0, 0);
  assert.equal(processed.length, 3);
  assert.equal(processed[0].byte, 0xF8);
  assert.equal(processed[1].byte, 0xF8);
  assert.equal(processed[2].byte, 0xF8);
  assert.ok(processed[1].timestampFrames > processed[0].timestampFrames, 'frame 1 deve ser maior que frame 0');
  assert.ok(processed[2].timestampFrames > processed[1].timestampFrames, 'frame 2 deve ser maior que frame 1');
  assert.equal(processed[1].timestampFrames - processed[0].timestampFrames, 1000);
});
