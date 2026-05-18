export const CURRENT_PRESET_VERSION = 2;

// Define valid ranges, defaults, and types for all 28 architecture variables
export const DEFAULT_CONTROL_STATE = {
  // Tape mechanics
  flutterDepth: 14,
  wowDepth: 10,
  dropoutSeverity: 5,
  drive: 40,
  noise: 30,
  tapeSpeed: 50,
  tapeAge: 40,
  headBumpAmount: 30,
  azimuthError: 10,
  flutterRate: 6.0,
  wowRate: 0.8,
  // Delay Core
  delayActive: 1,
  delayTimeMs: 500,
  feedback: 40,
  dryWet: 50,
  bpm: 120,
  tone: 50,
  // Heads
  activeHeads: 4,
  headsMusical: 0,
  // Modes
  guitarFocus: 0,
  pingPong: 0,
  freeze: 0,
  reverse: 0,
  reverseSmear: 0,
  // Reverb
  spring: 0,
  springDecay: 60,
  springDamping: 45,
  springMix: 50
};

const RANGE_BY_PARAM = {
  // Tape mechanics
  flutterDepth: { min: 0, max: 45 },
  wowDepth: { min: 0, max: 35 },
  dropoutSeverity: { min: 0, max: 40 },
  drive: { min: 0, max: 100 },
  noise: { min: 0, max: 100 },
  tapeSpeed: { min: 0, max: 100 },
  tapeAge: { min: 0, max: 100 },
  headBumpAmount: { min: 0, max: 100 },
  azimuthError: { min: 0, max: 100 },
  flutterRate: { min: 0.1, max: 20.0 },
  wowRate: { min: 0.1, max: 5.0 },
  // Delay
  delayActive: { min: 0, max: 1, isBoolean: true },
  delayTimeMs: { min: 10, max: 2000 },
  feedback: { min: 0, max: 100 },
  dryWet: { min: 0, max: 100 },
  bpm: { min: 30, max: 300 },
  tone: { min: 0, max: 100 },
  // Multitap Heads
  activeHeads: { min: 1, max: 7 },
  headsMusical: { min: 0, max: 1, isBoolean: true },
  // Extra modes
  guitarFocus: { min: 0, max: 1, isBoolean: true },
  pingPong: { min: 0, max: 1, isBoolean: true },
  freeze: { min: 0, max: 1, isBoolean: true },
  reverse: { min: 0, max: 1, isBoolean: true },
  reverseSmear: { min: 0, max: 1, isBoolean: true },
  // Spring
  spring: { min: 0, max: 1, isBoolean: true },
  springDecay: { min: 0, max: 100 },
  springDamping: { min: 0, max: 100 },
  springMix: { min: 0, max: 100 }
};

function isObject(value) {
  return !!value && typeof value === 'object' && !Array.isArray(value);
}

function clampNumber(value, min, max, fallback) {
  if (!Number.isFinite(value)) return fallback;
  return Math.min(max, Math.max(min, value));
}

function sanitizeParam(paramId, value) {
  const config = RANGE_BY_PARAM[paramId];
  if (!config) return undefined;
  const numeric = Number(value);
  const clamped = clampNumber(numeric, config.min, config.max, DEFAULT_CONTROL_STATE[paramId]);
  return config.isBoolean ? (clamped >= 0.5 ? 1 : 0) : clamped;
}

export function sanitizeControlState(input = {}) {
  const merged = { ...DEFAULT_CONTROL_STATE, ...(isObject(input) ? input : {}) };
  const sanitized = {};
  Object.keys(RANGE_BY_PARAM).forEach((paramId) => {
    sanitized[paramId] = sanitizeParam(paramId, merged[paramId]);
  });
  return sanitized;
}

function sanitizeProvidedControlState(input = {}) {
  const sanitized = {};
  if (!isObject(input)) return sanitized;
  Object.keys(RANGE_BY_PARAM).forEach((paramId) => {
    if (Object.prototype.hasOwnProperty.call(input, paramId)) {
      sanitized[paramId] = sanitizeParam(paramId, input[paramId]);
    }
  });
  return sanitized;
}

function buildFxChainFromState(state) {
  const enabled = Object.prototype.hasOwnProperty.call(state, 'delayActive')
    ? state.delayActive >= 0.5
    : DEFAULT_CONTROL_STATE.delayActive >= 0.5;
  return [
    {
      type: 'tapeDelay',
      enabled,
      params: { ...state }
    }
  ];
}

export function buildPresetFromControlState(
  controlState,
  { name = 'Hydra Preset', metadata = {}, fillDefaults = false } = {}
) {
  const state = fillDefaults
    ? sanitizeControlState(controlState)
    : sanitizeProvidedControlState(controlState);
  const preset = {
    version: CURRENT_PRESET_VERSION,
    name,
    // Flat parameter store maps directly to all C API configurations.
    engineParams: { ...state },
    metadata: {
      createdAt: new Date().toISOString(),
      source: 'hydra-web',
      ...metadata
    }
  };

  if (fillDefaults) {
    // Keep the legacy WebAudio chain shape for old preset importers and tests.
    preset.fxChain = buildFxChainFromState(state);
  }

  return preset;
}

export function serializePreset(controlState, options = {}) {
  const preset = buildPresetFromControlState(controlState, options);
  return JSON.stringify(preset, null, 2);
}

function migrateV1ToV2(v1Preset) {
  const v1Engine = isObject(v1Preset.engineParams) ? v1Preset.engineParams : {};
  const v1Fx = Array.isArray(v1Preset.fxChain) && isObject(v1Preset.fxChain[0]) ? v1Preset.fxChain[0] : {};
  const v1FxParams = isObject(v1Fx.params) ? v1Fx.params : {};

  const migratedControl = sanitizeControlState({
    drive: v1Engine.drive,
    flutterDepth: v1Engine.flutterDepth,
    wowDepth: v1Engine.wowDepth,
    delayTimeMs: v1FxParams.delayTimeMs ?? v1Engine.delayTimeMs,
    feedback: v1FxParams.feedback ?? v1Engine.feedback,
    dryWet: v1FxParams.dryWet ?? v1Engine.dryWet,
    delayActive: v1Fx.enabled === undefined ? v1Engine.delayActive : (v1Fx.enabled ? 1 : 0)
  });

  const meta = isObject(v1Preset.metadata) ? v1Preset.metadata : {};
  return buildPresetFromControlState(migratedControl, {
    name: typeof v1Preset.name === 'string' && v1Preset.name.trim() ? v1Preset.name : 'Migrated Preset',
    fillDefaults: true,
    metadata: {
      ...meta,
      migratedFromVersion: 1
    }
  });
}

function ensureValidV2Shape(preset) {
  if (!isObject(preset)) {
    throw new Error('Invalid preset: root element must be an object.');
  }
  if (preset.version !== 2) {
    throw new Error(`Unsupported version: expected 2, got ${preset.version}.`);
  }
  if (typeof preset.name !== 'string' || !preset.name.trim()) {
    throw new Error('Invalid preset: "name" field must be a non-empty string.');
  }
  if (!isObject(preset.engineParams)) {
    throw new Error('Invalid preset: "engineParams" field must be an object.');
  }
}

export function deserializePresetFromText(text) {
  let parsed;
  try {
    parsed = JSON.parse(text);
  } catch {
    throw new Error('Preset inválido: JSON malformado.');
  }

  if (!isObject(parsed)) {
    throw new Error('Invalid preset: root element must be a JSON object.');
  }

  let v2Preset = parsed;
  let migratedFromVersion = null;

  if (parsed.version === 1) {
    v2Preset = migrateV1ToV2(parsed);
    migratedFromVersion = 1;
  } else if (parsed.version !== 2) {
    throw new Error(`Unsupported preset version: ${parsed.version}. Only versions 1 and 2 are supported.`);
  }

  ensureValidV2Shape(v2Preset);

  const fx = Array.isArray(v2Preset.fxChain) && isObject(v2Preset.fxChain[0])
    ? v2Preset.fxChain[0]
    : null;
  const fxParams = fx && isObject(fx.params) ? fx.params : {};
  const mergedParams = {
    ...v2Preset.engineParams,
    ...fxParams,
    ...(fx && fx.enabled !== undefined ? { delayActive: fx.enabled ? 1 : 0 } : {})
  };

  // Legacy fxChain presets are sparse and should import as a complete UI state;
  // flat v2 engineParams exports preserve exactly the parameters they contain.
  const fillDefaults = migratedFromVersion !== null || !!fx;
  const controlState = fillDefaults
    ? sanitizeControlState(mergedParams)
    : sanitizeProvidedControlState(mergedParams);

  return {
    preset: buildPresetFromControlState(controlState, {
      name: v2Preset.name,
      fillDefaults,
      metadata: isObject(v2Preset.metadata) ? v2Preset.metadata : {}
    }),
    controlState,
    migratedFromVersion
  };
}
