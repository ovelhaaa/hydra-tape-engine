import test from 'node:test';
import assert from 'node:assert/strict';

import { createTransportState, REPEAT_STORAGE_KEY } from '../transportState.js';

function createMockStorage(initial = {}) {
  const store = new Map(Object.entries(initial));
  return {
    getItem(key) {
      return store.has(key) ? store.get(key) : null;
    },
    setItem(key, value) {
      store.set(key, String(value));
    }
  };
}

test('createTransportState reads repeat from storage', () => {
  const storage = createMockStorage({ [REPEAT_STORAGE_KEY]: 'true' });
  const state = createTransportState(storage);

  assert.equal(state.getState().isRepeatEnabled, true);
});

test('toggleRepeat updates state and persists in storage', () => {
  const storage = createMockStorage();
  const state = createTransportState(storage);

  state.toggleRepeat();
  assert.equal(state.getState().isRepeatEnabled, true);
  assert.equal(storage.getItem(REPEAT_STORAGE_KEY), 'true');

  state.toggleRepeat();
  assert.equal(state.getState().isRepeatEnabled, false);
  assert.equal(storage.getItem(REPEAT_STORAGE_KEY), 'false');
});
