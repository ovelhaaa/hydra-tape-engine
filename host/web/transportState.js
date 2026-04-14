const REPEAT_STORAGE_KEY = 'hydra.transport.repeat.enabled';

function readRepeatFromStorage(storage) {
  if (!storage) return false;
  const raw = storage.getItem(REPEAT_STORAGE_KEY);
  return raw === 'true';
}

function writeRepeatToStorage(storage, value) {
  if (!storage) return;
  storage.setItem(REPEAT_STORAGE_KEY, value ? 'true' : 'false');
}

export function createTransportState(storage = globalThis.sessionStorage) {
  let isRepeatEnabled = false;
  try {
    isRepeatEnabled = readRepeatFromStorage(storage);
  } catch {
    isRepeatEnabled = false;
  }

  const listeners = new Set();

  const notify = () => {
    const snapshot = { isRepeatEnabled };
    listeners.forEach((listener) => listener(snapshot));
  };

  const setRepeatEnabled = (value) => {
    const next = Boolean(value);
    if (isRepeatEnabled === next) return;
    isRepeatEnabled = next;
    try {
      writeRepeatToStorage(storage, isRepeatEnabled);
    } catch {
      // Ignore storage write errors for private browsing and restricted contexts.
    }
    notify();
  };

  return {
    getState() {
      return { isRepeatEnabled };
    },
    setRepeatEnabled,
    toggleRepeat() {
      setRepeatEnabled(!isRepeatEnabled);
    },
    subscribe(listener) {
      listeners.add(listener);
      return () => listeners.delete(listener);
    }
  };
}

export { REPEAT_STORAGE_KEY };
