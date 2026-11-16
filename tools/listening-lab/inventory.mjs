const engines = { worklet: "Original Worklet", python: "Python", native: "TapeCore Native", wasm: "WASM" };
const upgrades = {
  original_fast_vs_corrected_fast: ["original_fast", "corrected_fast", "Original Fast × Corrected Fast"],
  original_strict_vs_corrected_strict: ["original_strict", "corrected_strict", "Original Strict × Corrected Strict"],
  corrected_strict_vs_corrected_fast: ["corrected_strict", "corrected_fast", "Corrected Strict × Corrected Fast"],
  baseline_vs_corrected: ["baseline", "corrected_fast", "Baseline × Corrected Fast"],
  before_vs_head: ["before", "head", "Baseline × Optimized"],
  strict_vs_fast: ["strict", "fast", "Strict × Fast"],
  head_vs_head_post_tanh: ["head", "head_post_tanh", "Head × controle pós-tanh"],
  current_vs_post_tanh_control: ["current", "post_tanh_control", "PCM atual × controle pós-tanh"],
};

export const groups = ["Accidental Warp", "Tape Character", "Feedback & Stability", "Automation", "Stereo / Spatial"];
export const humanize = id => id.replace(/^\d+_/, "").replaceAll("_", " ");
const basename = path => path.split("/").at(-1);
const stem = path => basename(path).replace(/\.wav$/i, "");
const parameters = { 3: "Drive", 8: "Feedback", 31: "Speed", 32: "Grain" };

function group(id, warp, events) {
  if (/automation|grain|speed|toggle|delay_step/.test(id) || events.length) return groups[3];
  if (/stereo|ping_pong|multi_head/.test(id)) return groups[4];
  if (/feedback|cascade|decay|dropout/.test(id)) return groups[2];
  return warp ? groups[0] : groups[1];
}

function markers(events, rate) {
  return (events ?? []).map(event => {
    if (Array.isArray(event)) {
      const [time, param, value] = event;
      return { time: time, label: `${parameters[param] ?? "Parâmetro " + param} → ${value}` };
    }
    const time = Number.isFinite(event.frame) ? event.frame / rate : event.seconds ?? event.requested_seconds;
    return { time, label: `${event.parameter} → ${event.value}` };
  }).filter(event => Number.isFinite(event.time) && event.time >= 0);
}

function scenario(id, meta, options = {}) {
  const rate = meta.sample_rate_hz ?? meta.rate ?? options.rate;
  const events = markers(meta.automation ?? meta.automation_seconds ?? meta.events, rate);
  return {
    id, label: humanize(id), group: group(id, options.warp ?? id.startsWith("warp"), events), rate,
    duration: meta.duration_sec ?? meta.seconds ?? (meta.frames / rate || undefined),
    input: meta.input_wav ?? options.input, conditions: options.conditions ?? meta.settings,
    events, pairs: options.pairs ?? [], status: meta.status, source: options.source,
  };
}

function pair(a, b, kind, label = `${a.label} × ${b.label}`) {
  return { id: JSON.stringify([a.path, b.path]), a, b, kind, label };
}

const entry = (path, label, status) => ({ path, label, status });
const solo = render => ({ ...pair(render, render, "Escuta individual", render.label), solo: true });
const enginePair = (id, a = "native", b = "wasm") =>
  pair(entry(`${id}/${a}.wav`, engines[a]), entry(`${id}/${b}.wav`, engines[b]), "Motores");

export function parseManifest(name, data) {
  if (Array.isArray(data.scenarios) && (data.suite?.includes("Accidental Warp") || data.scenarios.some(row => row.variants))) {
    const commits = new Set(data.scenarios.map(row => row.git_commit).filter(Boolean));
    return { title: data.suite ?? "Accidental Warp", commit: [...commits].join(", "), incomplete: data.complete === false,
      scenarios: data.scenarios.flatMap(row => (row.variants?.length ? row.variants : [{}]).map((variant, index) => {
        const available = Object.keys(engines).flatMap(key => {
          const render = variant.engines?.[key];
          return render?.output_wav ? [entry(render.output_wav, engines[key], render.status)] : [];
        });
        const pairs = available.flatMap((a, i) => available.slice(i + 1).map(b => pair(a, b, "Motores")));
        if (available.length === 1) pairs.push(solo(available[0]));
        const id = row.variants?.length > 1 ? `${row.id} / variante ${index + 1}` : row.id;
        return scenario(id, row, { warp: true, pairs,
          conditions: { warp: variant.warp_parameters, tape: variant.tape_core_initial_parameters_by_id },
          source: row.tracked_worktree_dirty ? "Render com alterações locais no código rastreado" : undefined });
      })) };
  }
  if (Array.isArray(data.scenes)) {
    return { title: "CI · Listening Review", commit: data.commit,
      scenarios: data.scenes.map(row => scenario(row.scene, row, { rate: data.rate, input: `${row.scene}/dry.wav`,
        pairs: Number.isFinite(row.wasm_peak) ? [enginePair(row.scene)]
          : Number.isFinite(row.native_peak) ? [solo(entry(`${row.scene}/native.wav`, engines.native))] : [],
        source: data.excerpt })) };
  }
  if (name === "package_summary.json" && Array.isArray(data.scenarios)) {
    return { title: "CI · A/B Offline Review", commit: data.commit, scenarios: data.scenarios.map(row =>
      scenario(row.scenario, row, { rate: data.rate, pairs: [enginePair(row.scenario)] })) };
  }
  const rows = Object.entries(data).filter(([, row]) => row?.comparisons && row.rate);
  if (rows.length) {
    return { title: data.suite ?? "Release gate · TapeCore", scenarios: rows.map(([id, row]) => {
      const pairs = Object.keys(row.comparisons).flatMap(key => {
        const spec = upgrades[key];
        if (!spec) return [];
        const [a, b, label] = spec;
        const peakFrame = row.comparisons[key]?.peak_frame;
        const comparison = pair(entry(`${id}/${a}.wav`, a), entry(`${id}/${b}.wav`, b), "Regressão", label);
        if (Number.isFinite(peakFrame)) {
          comparison.markers = [{ time: peakFrame / row.rate, label: "Pico da diferença no manifesto" }];
        }
        comparison.markers = [...(comparison.markers ?? []), ...(row.listening_markers ?? [])];
        return [comparison];
      });
      return scenario(id, row, { pairs, input: `${id}/input.f32` });
    }) };
  }
  throw new Error(`Formato de manifesto desconhecido (${name}).`);
}

export async function sha256(bytes) {
  return Array.from(new Uint8Array(await crypto.subtle.digest("SHA-256", bytes)),
    byte => byte.toString(16).padStart(2, "0")).join("");
}

export const manifestName = path => /^(manifest|review|package_summary|metrics)\.json$/i.test(basename(path));

export async function inventory(files, commitOverride = "") {
  const paths = new Map();
  for (const file of files) {
    const path = file.webkitRelativePath || file.name;
    const items = paths.get(path) ?? [];
    items.push(file);
    paths.set(path, items);
  }
  const available = [...paths.keys()].filter(path => /\.wav$/i.test(path)).sort();
  const errors = [];
  const packages = [];
  for (const [path, items] of paths) {
    if (!manifestName(path) || items.length !== 1) continue;
    try {
      const source = await items[0].text();
      const data = JSON.parse(source.replace(/^\uFEFF/, ""));
      if (basename(path) === "metrics.json") continue;
      const { provenance, ...manifest } = data;
      packages.push({ path, source: provenance ? JSON.stringify(manifest) : source,
        provenance, ...parseManifest(basename(path), manifest) });
    } catch (error) {
      errors.push(`${path} · ${error.message}`);
    }
  }
  if (!packages.length && available.length < 2) throw new Error(errors.at(0) ?? "Selecione pelo menos dois WAVs ou um manifesto Hydra.");
  return { packages, errors, available, paths, commitOverride };
}

function resolve(path, root, source) {
  const expected = root + path.replaceAll("\\", "/").replace(/^\.\//, "");
  if (source.paths.get(expected)?.length === 1) return expected;
  // The file picker discards directory paths. Only use a basename with a unique manifest reference.
  if (source.available.every(item => !item.includes("/")) && source.uniqueNames.has(basename(expected))
    && source.paths.get(basename(expected))?.length === 1) return basename(expected);
  return null;
}

async function verifiedProvenance(provenance, scenarios, fileHashes, locate, paths) {
  const digestPattern = /^[a-f0-9]{64}$/;
  if (provenance.package_schema_version !== 1 || !provenance.renders || Array.isArray(provenance.renders)) {
    throw new Error("Esquema de procedência inválido.");
  }
  const files = Object.keys(provenance.renders).sort().map(path => {
    const digest = provenance.renders[path]?.render_wav_sha256;
    if (!digestPattern.test(digest)) throw new Error(`SHA-256 ausente ou inválido · ${path}`);
    const actual = fileHashes.get(locate(path));
    if (actual && actual !== digest) throw new Error(`SHA-256 divergente · ${path}`);
    return [path, digest];
  });
  if (provenance.package_id !== await sha256(new TextEncoder().encode(JSON.stringify(files)))) {
    throw new Error("SHA-256 do inventário de procedência divergente.");
  }
  let complete = true;
  for (const row of scenarios) {
    const input = row.input && locate(row.input);
    const pcmHash = input && /\.f32$/i.test(input)
      ? await sha256(await paths.get(input)[0].arrayBuffer()) : null;
    for (const path of new Set(row.pairs.flatMap(p => [p.a.path, p.b.path]))) {
      const render = Object.hasOwn(provenance.renders, path) ? provenance.renders[path] : null;
      if (!render || (row.rate && render.sample_rate !== row.rate)) {
        throw new Error(`Procedência ausente ou taxa divergente · ${path}`);
      }
      if (!fileHashes.has(locate(path))) complete = false;
      const wavHash = fileHashes.get(input);
      if ((wavHash && render.source_wav_sha256 && wavHash !== render.source_wav_sha256) ||
          (pcmHash && render.source_pcm_sha256 && pcmHash !== render.source_pcm_sha256)) {
        throw new Error(`SHA-256 da fonte divergente · ${row.input}`);
      }
    }
  }
  return { ...provenance, complete };
}

export async function openPackage(source, descriptor, manual) {
  const root = descriptor?.path.includes("/") ? descriptor.path.slice(0, descriptor.path.lastIndexOf("/") + 1) : "";
  let scenarios = descriptor?.scenarios ?? [];
  let title = descriptor?.title ?? "Seleção manual";
  if (descriptor && title.startsWith("Release gate")) {
    const special = "freeverb_drive_pcm_55hz";
    const paths = source.paths.get(`${root}${special}/metrics.json`);
    if (paths?.length === 1) {
      const conditions = JSON.parse(await paths[0].text());
      const spec = upgrades.current_vs_post_tanh_control;
      scenarios = [...scenarios, scenario(special, {}, { conditions, pairs: [
        pair(entry(`${special}/${spec[0]}.wav`, "PCM atual"), entry(`${special}/${spec[1]}.wav`, "Controle pós-tanh"), "Regressão", spec[2]),
      ] })];
    }
  }
  if (manual) {
    title = "Seleção manual";
    const byScenario = new Map();
    for (const { id, a, b, kind } of Array.isArray(manual) ? manual : [manual]) {
      if (!byScenario.has(id)) byScenario.set(id, scenario(id, {}));
      byScenario.get(id).pairs.push(pair(entry(a, a), entry(b, b), kind, `${stem(a)} × ${stem(b)}`));
    }
    scenarios = [...byScenario.values()];
  }
  const expected = new Set(scenarios.flatMap(row => [row.input, ...row.pairs.flatMap(p => [p.a.path, p.b.path])].filter(Boolean)));
  const counts = new Map();
  for (const name of expected) counts.set(basename(name), (counts.get(basename(name)) ?? 0) + 1);
  const lookup = { ...source, uniqueNames: new Set([...counts].filter(([, n]) => n === 1).map(([name]) => name)) };
  const fileHashes = new Map();
  const locate = path => manual ? (source.paths.get(path)?.length === 1 ? path : null) : resolve(path, root, lookup);
  for (const path of expected) {
    const found = locate(path);
    if (found && !fileHashes.has(found) && /\.wav$/i.test(found)) {
      fileHashes.set(found, await sha256(await source.paths.get(found)[0].arrayBuffer()));
    }
  }
  scenarios = scenarios.map(row => ({ ...row, inputFound: row.input ? locate(row.input) : null,
    pairs: row.pairs.map(p => {
      const attach = item => {
        const found = locate(item.path);
        return { ...item, file: found ? source.paths.get(found)[0] : null, hash: fileHashes.get(found) };
      };
      return { ...p, a: attach(p.a), b: attach(p.b) };
    }) }));
  const commit = descriptor?.commit || source.commitOverride.trim() || null;
  const identity = { title, commit, manifest: descriptor?.source ?? null, scenarios: scenarios.map(row =>
    [row.id, row.conditions, row.pairs.map(p => [p.id, p.kind, p.a.hash ?? null, p.b.hash ?? null])]),
    files: [...fileHashes].sort(([a], [b]) => a.localeCompare(b)).map(([path, hash]) => [manual ? path : path.startsWith(root) ? path.slice(root.length) : path, hash]) };
  const id = await sha256(new TextEncoder().encode(JSON.stringify(identity)));
  let provenance = descriptor?.provenance;
  const companion = descriptor && source.paths.get(root + "provenance.json");
  if (!provenance && companion) {
    if (companion.length !== 1) throw new Error("Manifesto de procedência ambíguo.");
    provenance = JSON.parse((await companion[0].text()).replace(/^\uFEFF/, ""));
    // Legacy sessions are bound to the original manifest and A/B files.
    if (provenance.listening_lab_package_id !== id) {
      throw new Error("A procedência pertence a outro pacote de escuta.");
    }
  }
  if (provenance) provenance = await verifiedProvenance(provenance, scenarios, fileHashes, locate, source.paths);
  return { id, title, commit, scenarios, provenance,
    incomplete: descriptor?.incomplete ?? false };
}

export function suggestions(source) {
  const found = [];
  for (const path of source.available) {
    const dir = path.slice(0, path.lastIndexOf("/") + 1);
    const name = stem(path);
    for (const [from, to, kind] of [["baseline", "optimized", "Regressão"], ["before", "head", "Regressão"],
      ["strict", "fast", "Regressão"], ["worklet", "python", "Motores"], ["worklet", "native", "Motores"],
      ["python", "native", "Motores"], ["native", "wasm", "Motores"]]) {
      const suffix = new RegExp(`(^|[_-])${from}$`, "i");
      if (!suffix.test(name)) continue;
      const other = dir + name.replace(suffix, (_, sep) => sep + to) + ".wav";
      const candidates = source.available.filter(item => item.toLowerCase() === other.toLowerCase());
      if (candidates.length === 1 && source.paths.get(path).length === 1 && source.paths.get(candidates[0]).length === 1) {
        found.push({ a: path, b: candidates[0], kind });
      }
    }
  }
  return found;
}
