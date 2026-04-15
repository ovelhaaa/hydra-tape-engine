export const CURRENT_PRESET_VERSION = 2;

export const PRESET_SCHEMA_V2 = {
  type: 'object',
  required: ['version', 'name', 'engineParams', 'fxChain', 'metadata'],
  properties: {
    version: { type: 'number', const: CURRENT_PRESET_VERSION },
    name: { type: 'string' },
    engineParams: {
      type: 'object',
      properties: {
        drive: { type: 'number', minimum: 0, maximum: 100 },
        flutterDepth: { type: 'number', minimum: 0, maximum: 100 },
        wowDepth: { type: 'number', minimum: 0, maximum: 100 }
      }
    },
    fxChain: {
      type: 'array',
      items: {
        type: 'object',
        required: ['type', 'enabled', 'params'],
        properties: {
          type: { type: 'string' },
          enabled: { type: 'boolean' },
          params: {
            type: 'object',
            properties: {
              delayTimeMs: { type: 'number', minimum: 10, maximum: 2000 },
              feedback: { type: 'number', minimum: 0, maximum: 100 },
              dryWet: { type: 'number', minimum: 0, maximum: 100 }
            }
          }
        }
      }
    },
    metadata: {
      type: 'object',
      properties: {
        createdAt: { type: 'string' },
        source: { type: 'string' }
      }
    }
  }
};

export const DEFAULT_CONTROL_STATE = {
  delayActive: 1,
  delayTimeMs: 500,
  feedback: 40,
  dryWet: 50,
  drive: 40,
  flutterDepth: 20,
  wowDepth: 15
};

const RANGE_BY_PARAM = {
  delayActive: { min: 0, max: 1, isBoolean: true },
  delayTimeMs: { min: 10, max: 2000 },
  feedback: { min: 0, max: 100 },
  dryWet: { min: 0, max: 100 },
  drive: { min: 0, max: 100 },
  flutterDepth: { min: 0, max: 100 },
  wowDepth: { min: 0, max: 100 }
};

function isObject(value) {
  return !!value && typeof value === 'object' && !Array.isArray(value);
}

function clampNumber(value, min, max, fallback) {
  if (!Number.isFinite(value)) return fallback;
  return Math.min(max, Math.max(min, value));
}

export function sanitizeControlState(input = {}) {
  const merged = { ...DEFAULT_CONTROL_STATE, ...(isObject(input) ? input : {}) };
  const sanitized = {};
  Object.entries(RANGE_BY_PARAM).forEach(([paramId, config]) => {
    const numeric = Number(merged[paramId]);
    const clamped = clampNumber(numeric, config.min, config.max, DEFAULT_CONTROL_STATE[paramId]);
    sanitized[paramId] = config.isBoolean ? (clamped >= 0.5 ? 1 : 0) : clamped;
  });
  return sanitized;
}

export function buildPresetFromControlState(controlState, { name = 'Hydra Preset', metadata = {} } = {}) {
  const state = sanitizeControlState(controlState);
  return {
    version: CURRENT_PRESET_VERSION,
    name,
    engineParams: {
      drive: state.drive,
      flutterDepth: state.flutterDepth,
      wowDepth: state.wowDepth
    },
    fxChain: [
      {
        type: 'tapeDelay',
        enabled: state.delayActive === 1,
        params: {
          delayTimeMs: state.delayTimeMs,
          feedback: state.feedback,
          dryWet: state.dryWet
        }
      }
    ],
    metadata: {
      createdAt: new Date().toISOString(),
      source: 'hydra-web',
      ...metadata
    }
  };
}

export function serializePreset(controlState, options = {}) {
  const preset = buildPresetFromControlState(controlState, options);
  return JSON.stringify(preset, null, 2);
}

function migrateV1ToV2(v1Preset) {
  const engineParams = isObject(v1Preset.engineParams) ? v1Preset.engineParams : {};
  const migratedControl = sanitizeControlState({
    drive: engineParams.drive,
    flutterDepth: engineParams.flutterDepth,
    wowDepth: engineParams.wowDepth,
    delayTimeMs: engineParams.delayTimeMs,
    feedback: engineParams.feedback,
    dryWet: engineParams.dryWet,
    delayActive: engineParams.delayActive
  });

  const metadata = isObject(v1Preset.metadata) ? v1Preset.metadata : {};
  return buildPresetFromControlState(migratedControl, {
    name: typeof v1Preset.name === 'string' && v1Preset.name.trim() ? v1Preset.name : 'Imported v1 preset',
    metadata: {
      ...metadata,
      migratedFromVersion: 1
    }
  });
}

function ensureValidV2Shape(preset) {
  if (!isObject(preset)) {
    throw new Error('Preset inválido: o conteúdo JSON deve ser um objeto.');
  }
  if (preset.version !== 2) {
    throw new Error(`Preset inválido: versão esperada 2, recebida ${String(preset.version)}.`);
  }
  if (typeof preset.name !== 'string' || !preset.name.trim()) {
    throw new Error('Preset inválido: campo "name" é obrigatório e deve ser texto não vazio.');
  }
  if (!isObject(preset.engineParams)) {
    throw new Error('Preset inválido: campo "engineParams" deve ser um objeto.');
  }
  if (!Array.isArray(preset.fxChain)) {
    throw new Error('Preset inválido: campo "fxChain" deve ser um array.');
  }
  if (!isObject(preset.metadata)) {
    throw new Error('Preset inválido: campo "metadata" deve ser um objeto.');
  }
}

function normalizeV2Preset(preset) {
  const engineParams = isObject(preset.engineParams) ? preset.engineParams : {};
  const fxChain = Array.isArray(preset.fxChain) ? preset.fxChain : [];
  const delayNode = fxChain.find((fx) => isObject(fx) && fx.type === 'tapeDelay') || {};
  const delayParams = isObject(delayNode.params) ? delayNode.params : {};

  const controlState = sanitizeControlState({
    drive: engineParams.drive,
    flutterDepth: engineParams.flutterDepth,
    wowDepth: engineParams.wowDepth,
    delayTimeMs: delayParams.delayTimeMs,
    feedback: delayParams.feedback,
    dryWet: delayParams.dryWet,
    delayActive: delayNode.enabled === undefined ? undefined : (delayNode.enabled ? 1 : 0)
  });

  return {
    preset: buildPresetFromControlState(controlState, {
      name: preset.name,
      metadata: isObject(preset.metadata) ? preset.metadata : {}
    }),
    controlState
  };
}

export function deserializePresetFromText(text) {
  let parsed;
  try {
    parsed = JSON.parse(text);
  } catch {
    throw new Error('Preset inválido: JSON malformado.');
  }

  if (!isObject(parsed)) {
    throw new Error('Preset inválido: o conteúdo JSON deve ser um objeto.');
  }

  let v2Preset = parsed;
  let migratedFromVersion = null;

  if (parsed.version === 1) {
    v2Preset = migrateV1ToV2(parsed);
    migratedFromVersion = 1;
  } else if (parsed.version !== 2) {
    throw new Error(`Versão de preset não suportada: ${String(parsed.version)}. Suportadas: 1 e 2.`);
  }

  ensureValidV2Shape(v2Preset);
  const normalized = normalizeV2Preset(v2Preset);
  return {
    ...normalized,
    migratedFromVersion
  };
}
