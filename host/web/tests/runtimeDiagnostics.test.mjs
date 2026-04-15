import test from 'node:test';
import assert from 'node:assert/strict';
import { WEB_BUILD_HELP, formatRuntimeError, inferRuntimeHint } from '../runtimeDiagnostics.js';

test('inferRuntimeHint retorna dica para file://', () => {
  const hint = inferRuntimeHint(new Error('anything'), { protocol: 'file:' });
  assert.match(hint, /file:\/\//i);
});

test('inferRuntimeHint retorna dica para erro de runtime web', () => {
  const hint = inferRuntimeHint(new Error('Failed to import hydra_dsp.wasm'), { protocol: 'http:' });
  assert.ok(hint);
  assert.match(hint, /hydra_dsp\.js\/wasm/i);
  assert.match(hint, new RegExp(WEB_BUILD_HELP.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')));
});

test('formatRuntimeError combina contexto + detalhe + dica', () => {
  const formatted = formatRuntimeError('Falha ao iniciar o áudio', new Error('worklet failed'), { protocol: 'http:' });
  assert.match(formatted.message, /Falha ao iniciar o áudio/i);
  assert.match(formatted.message, /worklet failed/i);
  assert.ok(formatted.hint);
});

test('formatRuntimeError sem dica mantém mensagem curta', () => {
  const formatted = formatRuntimeError('Falha no playback', new Error('operation cancelled'), { protocol: 'https:' });
  assert.equal(formatted.hint, null);
  assert.equal(formatted.message, 'Falha no playback: operation cancelled');
});
