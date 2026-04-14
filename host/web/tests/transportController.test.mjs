import test from 'node:test';
import assert from 'node:assert/strict';

import { createTransportController } from '../transportController.js';

function createMockPlayer() {
  const listeners = new Map();
  return {
    currentTime: 12,
    playCalls: 0,
    addEventListener(event, handler) {
      listeners.set(event, handler);
    },
    removeEventListener(event) {
      listeners.delete(event);
    },
    async play() {
      this.playCalls += 1;
    },
    emit(event) {
      const handler = listeners.get(event);
      if (handler) {
        return handler();
      }
      return undefined;
    }
  };
}

test('timeline end keeps behavior when repeat is disabled', async () => {
  const player = createMockPlayer();
  const transportState = { getState: () => ({ isRepeatEnabled: false }) };
  const controller = createTransportController({ player, transportState });

  await player.emit('ended');

  assert.equal(player.currentTime, 12);
  assert.equal(player.playCalls, 0);
  controller.dispose();
});

test('timeline end restarts playback when repeat is enabled', async () => {
  const player = createMockPlayer();
  const messages = [];
  const transportState = { getState: () => ({ isRepeatEnabled: true }) };
  const controller = createTransportController({
    player,
    transportState,
    setStatus: (message) => messages.push(message)
  });

  await player.emit('ended');

  assert.equal(player.currentTime, 0);
  assert.equal(player.playCalls, 1);
  assert.equal(messages[0], 'Repeat ON: restarting from the beginning.');
  controller.dispose();
});
