import { inventory, openPackage, suggestions, groups } from "./inventory.mjs";
import { Deck, decodePair, db, dbText, levels, match, wavFormat } from "./audio.mjs";
import { decisions, differences, artifacts, newSession, storageKey, keyFor, importSession, report, overall, mapping,
  provenanceLabels, renderOrigin } from "./session.mjs";
import { draw, clock } from "./wave.mjs";

const $ = id => document.getElementById(id);
const show = (id, visible) => { $(id).hidden = !visible; };
const say = (id, message) => { $(id).textContent = message; };
const option = (value, label = value) => new Option(label, value);
const addOptions = (id, values, empty = "—") =>
  $(id).replaceChildren(option("", empty), ...values.map(value => option(value)));
const notice = message => { say("notice", message); show("notice", Boolean(message)); };

let source, pack, session, row, pair, deck, context, trim, stats, formats;
let epoch = 0;
let importing = false;
let manualPairs = [];
let range = [0, 1];
let drag;
const key = () => keyFor(row, pair);
const trial = () => pair ? session.trials[key()] : null;
const blind = () => Boolean(trial() && !trial().revealed);
const order = () => blind() && mapping(trial().seed, key()) ? [1, 0] : [0, 1];
const slotLabels = () => blind() ? ["X", "Y"] : ["A", "B"];
const markers = () => [...row.events, ...(blind() ? [] : pair.markers ?? []), ...(session.markers[key()] ?? [])]
  .filter(marker => marker.time <= deck.duration)
  .sort((a, b) => a.time - b.time);

for (const id of ["fidelity", "musicality", "confidence"]) addOptions(id, [1, 2, 3, 4, 5]);
addOptions("artifacts", artifacts);
addOptions("decision", decisions);
for (const label of differences) {
  const wrapper = document.createElement("label");
  wrapper.className = "check";
  const input = Object.assign(document.createElement("input"), { type: "checkbox", value: label });
  wrapper.append(input, label);
  $("differences").append(wrapper);
}

function persist() {
  try {
    localStorage.setItem(storageKey(pack), JSON.stringify(session));
    return true;
  } catch {
    notice("Armazenamento local indisponível. Exporte o JSON para guardar a sessão.");
    return false;
  }
}

function clearDeck() {
  deck?.close();
  deck = null;
  trim = null;
  stats = null;
  show("player", false);
}

async function loadFiles(input) {
  if (!input.files.length || importing) return;
  const files = Array.from(input.files);
  input.value = "";
  importing = true;
  notice("Inventariando arquivos…");
  try {
    const next = await inventory(files, $("commit").value);
    source = next;
    show("package-choices", next.packages.length > 1);
    $("package-select").replaceChildren(...next.packages.map((item, i) => option(i, item.path)));
    show("manual", !next.packages.length);
    if (next.packages.length) {
      await selectPackage(next.packages[0]);
    } else {
      clearDeck();
      epoch++;
      pack = session = row = pair = null;
      show("workspace", false);
      show("exports", false);
      say("package-name", "Confirmar comparação");
      say("package-meta", `${next.available.length} WAVs · descoberta assistida`);
      for (const id of ["manual-a", "manual-b"]) {
        $(id).replaceChildren(option("", "Selecionar WAV"), ...next.available.map(path =>
          option(path, path + (next.paths.get(path).length > 1 ? " · ambíguo" : ""))));
        for (const choice of $(id).options) if (next.paths.get(choice.value)?.length > 1) choice.disabled = true;
      }
      manualPairs = [];
      $("manual-open").disabled = true;
      say("manual-count", "");
      next.suggestions = suggestions(next);
      $("suggestions").replaceChildren(option("", "Escolher manualmente"),
        ...next.suggestions.map((p, i) => option(i, `${p.a} × ${p.b}`)));
      notice([...next.errors, "Confira os dois arquivos e confirme a associação."].join(" "));
    }
  } catch (error) {
    notice(error.message);
  } finally {
    importing = false;
  }
}

$("folder").onchange = event => loadFiles(event.target);
$("files").onchange = event => loadFiles(event.target);
$("package-select").onchange = async () => {
  try { await selectPackage(source.packages[Number($("package-select").value)]); }
  catch (error) { notice(error.message); }
};
$("suggestions").onchange = () => {
  if ($("suggestions").value === "") return;
  const { a, b, kind } = source.suggestions[Number($("suggestions").value)];
  $("manual-a").value = a;
  $("manual-b").value = b;
  $("manual-kind").value = kind;
  $("manual-name").value = a.split("/").slice(-2, -1)[0] ?? a.replace(/\.wav$/i, "");
};
$("manual").onsubmit = event => {
  event.preventDefault();
  const a = $("manual-a").value;
  const b = $("manual-b").value;
  const id = $("manual-name").value.trim();
  if (a === b || !id) return notice("Informe um cenário e escolha dois WAVs distintos.");
  if (manualPairs.some(p => p.id === id && ((p.a === a && p.b === b) || (p.a === b && p.b === a)))) {
    return notice("Este par já foi confirmado para o cenário.");
  }
  manualPairs.push({ id, a, b, kind: $("manual-kind").value });
  $("manual-open").disabled = false;
  say("manual-count", `${manualPairs.length} par(es) confirmado(s)`);
};
$("manual-open").onclick = async () => {
  try {
    await selectPackage(null, [...manualPairs]);
    show("manual", false);
  }
  catch (error) { notice(error.message); }
};

async function selectPackage(descriptor, manual) {
  const attempt = ++epoch;
  clearDeck();
  show("workspace", false);
  show("exports", false);
  notice("Calculando a identidade SHA-256 do pacote…");
  source.commitOverride = $("commit").value;
  const candidate = await openPackage(source, descriptor, manual);
  if (epoch !== attempt) return;
  pack = candidate;
  session = newSession(pack);
  const info = [...source.errors];
  try {
    const saved = localStorage.getItem(storageKey(pack));
    if (saved) session = importSession(saved, pack);
  } catch (error) {
    info.push(`Sessão local indisponível · ${error.message}`);
  }
  if (pack.incomplete) info.push("O manifesto marca o pacote como incompleto.");
  notice(info.join(" "));
  show("workspace", true);
  refreshPackage();
  const pendingBlind = pack.scenarios.flatMap(row => row.pairs.map(pair => ({ row, pair })))
    .find(item => session.trials[keyFor(item.row, item.pair)]?.revealed === false);
  await selectScenario(pendingBlind?.row ?? pack.scenarios[0], pendingBlind?.pair);
}

function refreshPackage() {
  const masked = blind();
  const { status } = overall(pack, session);
  say("package-name", pack.title);
  say("package-meta", [
    `${pack.scenarios.length} cenários · pacote ${pack.id}`,
    ...provenanceLabels(pack).map(([label, value]) => `${label} · ${value}`),
  ].join(" | "));
  const done = pack.scenarios.filter(item => item.pairs.length && item.pairs.every(p =>
    p.a.file && p.b.file && session.evaluations[keyFor(item, p)]?.decision)).length;
  say("progress", `${done}/${pack.scenarios.length}`);
  say("overall", status);
  show("exports", !masked);
  show("load", !masked);
  show("pair-field", !masked);
  $("next").disabled = masked || !row || pack.scenarios.indexOf(row) >= pack.scenarios.length - 1;
  const sections = [];
  for (const group of groups) {
    const rows = pack.scenarios.filter(item => item.group === group);
    if (!rows.length) continue;
    const section = document.createElement("div");
    section.className = "suite-group";
    const heading = document.createElement("h3");
    heading.textContent = group;
    section.append(heading);
    for (const item of rows) {
      const button = document.createElement("button");
      button.className = "scenario";
      button.setAttribute("aria-current", String(item === row));
      button.disabled = masked;
      const names = [...new Set(item.pairs.flatMap(p => [p.a, p.b]).filter(render => render.file).map(render => render.label))];
      const records = item.pairs.map(p => session.evaluations[keyFor(item, p)]);
      const reviewed = records.filter(record => record?.decision).length;
      const issues = item.pairs.some(p => !p.a.file || !p.b.file || [p.a.status, p.b.status].includes("FAIL"))
        || item.status === "FAIL" || records.some(record => ["NEEDS INVESTIGATION", "REJECT"].includes(record?.decision)
          || ["Moderado", "Grave"].includes(record?.artifacts));
      const details = document.createElement("small");
      const status = `${reviewed}/${item.pairs.length} decididos${issues ? " · verificar" : ""}`;
      details.textContent = masked ? status : `${names.join(" / ") || "Sem par disponível"} · ${status}`;
      button.append(item.label, details);
      button.onclick = () => selectScenario(item);
      section.append(button);
    }
    sections.push(section);
  }
  $("scenarios").replaceChildren(...sections);
}

async function selectScenario(item, preferred) {
  row = item;
  pair = preferred ?? item?.pairs.find(p => p.a.file && p.b.file) ?? item?.pairs[0];
  say("deck-title", item?.label ?? "Sem cenários no manifesto");
  $("pair").replaceChildren(...(item?.pairs ?? []).map(p =>
    option(p.id, `${p.kind} · ${p.label}${p.a.file && p.b.file ? "" : " · WAV ausente"}`)));
  if (pair) $("pair").value = pair.id;
  await selectPair();
}

async function selectPair() {
  const attempt = ++epoch;
  clearDeck();
  refreshPackage();
  show("evaluate", false);
  show("blind-panel", false);
  show("conditions", false);
  say("condition-text", "");
  say("deck-message", "");
  if (!pair) {
    say("deck-message", "O manifesto não traz um par de renders para este cenário.");
    return;
  }
  if (!pair.a.file || !pair.b.file) {
    say("deck-message", "WAV ausente ou ambíguo · " + [pair.a, pair.b].filter(item => !item.file).map(item => item.path).join(" / "));
    return;
  }
  say("deck-message", "Decodificando WAVs…");
  try {
    context ??= new AudioContext();
    const files = [pair.a.file, pair.b.file];
    const [buffers, headers] = await Promise.all([decodePair(context, files), Promise.all(files.map(wavFormat))]);
    if (attempt !== epoch) return;
    deck = new Deck(context, buffers);
    formats = headers;
    const record = session.evaluations[key()];
    $("mode").value = (blind() ? trial().mode : record?.level.mode ?? trial()?.mode) ?? "RAW";
    $("channel").value = ["Stereo", "L", "R"].includes(record?.level.channel) ? record.level.channel : "Stereo";
    if (Number.isFinite(record?.level.volume)) $("volume").value = record.level.volume;
    $("preference").value = trial()?.preference ?? "";
    $("confidence").value = trial()?.confidence ?? "";
    $("seek").max = deck.duration;
    $("zoom").value = "1";
    $("view").value = pair.solo ? "A" : "Both";
    $("loop-start").value = "0";
    $("loop-end").value = Number(deck.duration.toFixed(3));
    fillForm(record);
    show("player", true);
    say("deck-message", buffers[0].duration === buffers[1].duration ? ""
      : "Durações diferentes · o deck usa o trecho comum a partir de 0 s.");
    if (blind()) chooseSlot(0);
    refreshIdentity();
    applyLevels();
    updateMarkers();
    $("loop").setAttribute("aria-pressed", "false");
    zoom(0);
  } catch (error) {
    if (attempt === epoch) say("deck-message", `Falha ao decodificar os WAVs · ${error.message}`);
  }
}

function fillForm(record) {
  for (const id of ["fidelity", "musicality", "artifacts", "decision", "notes"]) $(id).value = record?.[id] ?? "";
  for (const input of $("differences").querySelectorAll("input")) {
    input.checked = record?.differences.includes(input.value) ?? false;
  }
  say("save-status", record ? `Salvo em ${new Date(record.timestamp).toLocaleString("pt-BR")}` : "");
}

function refreshIdentity() {
  const masked = blind();
  refreshPackage();
  show("evaluate", !masked);
  show("slot1", !pair.solo);
  $("view").disabled = Boolean(pair.solo);
  show("conditions", !masked);
  const { a, b } = pair;
  const duration = row.duration == null ? clock(deck.duration) : clock(row.duration);
  const format = i => [
    formats[i] ? formats[i].rate + " Hz · " + formats[i].channels + " canais" : "",
    clock(deck.buffers[i].duration),
  ].filter(Boolean).join(" · ");
  say("condition-text", masked ? "" : [
    `A · ${a.path} · ${format(0)}${a.status ? ` · ${a.status}` : ""}`,
    pair.solo ? "" : `B · ${b.path} · ${format(1)}${b.status ? ` · ${b.status}` : ""}`,
    row.input ? `Entrada · ${row.input}${row.inputFound ? "" : " (ausente)"}` : "",
    `${row.rate ? row.rate + " Hz · " : ""}${duration} no cenário · deck a ${context.sampleRate} Hz`,
    row.source ?? "",
    ...[pair.a, ...(pair.solo ? [] : [pair.b])].map((item, index) => {
      const origin = renderOrigin(pack, item.path);
      return origin ? `${index === 0 ? "A" : "B"} · ${origin} · SHA-256 ${item.hash ?? "indisponível"}` : "";
    }),
    row.conditions ? JSON.stringify(row.conditions, null, 2) : "",
  ].filter(Boolean).join("\n"));
  show("blind-panel", pair.kind === "Regressão");
  show("blind-start", !trial());
  show("blind-form", masked && !trial().preference);
  show("reveal", masked && Boolean(trial().preference));
  $("mode").disabled = masked || Boolean(pair.solo);
  if (pair.solo) $("mode").value = "RAW";
  const t = trial();
  if (!t) say("blind-result", "");
  else if (t.revealed) {
    const first = mapping(t.seed, key());
    say("blind-result", `${t.preference} · confiança ${t.confidence ?? "—"} · X = ${[a.label, b.label][first]} · Y = ${[a.label, b.label][1 - first]}`);
  } else say("blind-result", t.preference ? `Preferência registrada · ${t.preference}` : "Associação fixa nesta sessão.");
  for (const [i, label] of slotLabels().entries()) {
    const strong = document.createElement("strong");
    strong.textContent = label;
    $("slot" + i).replaceChildren(strong, masked ? "" : [a.label, b.label][i]);
    $("view").querySelector(`option[value="${["A", "B"][i]}"]`).textContent = label;
  }
  $("view").querySelector('option[value="Difference"]').textContent = slotLabels().join(" − ");
  updateSlots();
}

function updateSlots() {
  order().forEach((index, i) => $("slot" + i).setAttribute("aria-pressed", String(deck.active === index)));
}

function chooseSlot(i) {
  if (!deck || (pair.solo && i === 1)) return;
  deck.select(order()[i]);
  updateSlots();
}

function applyLevels() {
  if (!deck) return;
  const mode = $("mode").value;
  const channel = $("channel").value;
  const volume = Number($("volume").value);
  if (stats?.channel !== channel) stats = { channel, rows: deck.buffers.map(buffer => levels(buffer, channel, deck.duration)) };
  trim = match(stats.rows, mode);
  deck.fade = Number($("fade").value);
  deck.route(channel);
  deck.configure(trim.gains, trim.protection, volume);
  say("volume-value", `${Math.round(volume * 100)}%`);
  const p = text => Object.assign(document.createElement("p"), { textContent: text });
  const values = order().map((index, i) => {
    const { rms, peak } = stats.rows[index];
    return p(`${slotLabels()[i]} · RMS ${dbText(db(rms))}FS · pico ${dbText(db(peak))}FS · ganho ${dbText(db(trim.gains[index]))}`);
  });
  const delta = trim.silent ? "—" : dbText(trim.delta * (order()[0] ? -1 : 1));
  if (pair.solo) {
    $("meters").replaceChildren(values[0], p(`Proteção ${dbText(db(trim.protection))}`));
    return;
  }
  $("meters").replaceChildren(...values,
    p(`Δ ${slotLabels()[1]}−${slotLabels()[0]} ${delta} · proteção ${dbText(db(trim.protection))}`));
  if (mode === "MATCHED" && trim.silent) $("meters").append(p("Canal silencioso · ganhos mantidos."));
}

function paint() {
  if (!deck) return;
  const view = $("view").value;
  const labels = slotLabels();
  const description = view === "Difference" ? `${labels.join(" − ")} · lag 0 ms · diferença de amostras RAW`
    : "Amplitude RAW · laranja " + labels[0] + " · turquesa " + labels[1];
  say("wave-key", pair.solo ? "Amplitude RAW · laranja A" : description);
  draw($("wave"), order().map(i => deck.buffers[i]), view, $("channel").value, range,
    deck.loop ? deck.region : null, markers());
}

function zoom(center) {
  const width = deck.duration / Number($("zoom").value);
  const left = Math.max(0, Math.min(center - width / 2, deck.duration - width));
  range = [left, left + width];
  $("pan").max = deck.duration - width;
  $("pan").value = left;
  show("pan-field", width < deck.duration);
  paint();
}

function seek(time, recenter = false) {
  if (!deck) return;
  deck.seek(time);
  if (recenter && (time < range[0] || time > range[1])) zoom(time);
}

function updateMarkers() {
  $("markers").replaceChildren(...markers().map(marker => {
    const button = document.createElement("button");
    button.textContent = `${clock(marker.time)} · ${marker.label}`;
    button.onclick = () => seek(marker.time, true);
    return button;
  }));
}

function setLoop(enabled, start = deck.region[0], end = deck.region[1]) {
  start = Math.max(0, start);
  end = Math.min(deck.duration, end);
  if (!Number.isFinite(start) || !Number.isFinite(end) || end - start < Math.min(0.005, deck.duration)) {
    say("deck-message", "Defina um intervalo válido de pelo menos 5 ms.");
    return;
  }
  deck.setLoop(enabled, start, end);
  $("loop-start").value = Number(start.toFixed(3));
  $("loop-end").value = Number(end.toFixed(3));
  $("loop").setAttribute("aria-pressed", String(enabled));
  paint();
}

function frame() {
  if (deck) {
    const time = deck.position;
    const playing = Boolean(deck.run);
    const position = (time - range[0]) / (range[1] - range[0]);
    $("cursor").style.left = `${position * 100}%`;
    $("seek").value = time;
    const readout = `${clock(time)} / ${clock(deck.duration)}`;
    if ($("time").textContent !== readout) say("time", readout);
    const button = playing ? "❚❚ Pausar" : "▶ Reproduzir";
    if ($("play").textContent !== button) say("play", button);
  }
  requestAnimationFrame(frame);
}
requestAnimationFrame(frame);

$("pair").onchange = () => {
  pair = row.pairs.find(p => p.id === $("pair").value);
  selectPair();
};
$("next").onclick = () => selectScenario(pack.scenarios[pack.scenarios.indexOf(row) + 1]);
$("play").onclick = async () => {
  if (!deck) return;
  const current = deck;
  if (current.run) return current.pause();
  try {
    await context.resume();
    if (current === deck) current.start();
  } catch (error) { say("deck-message", error.message); }
};
$("stop").onclick = () => { deck.stop(); zoom(0); };
$("loop").onclick = () => setLoop(!deck.loop);
$("set-loop").onclick = () => setLoop(true, $("loop-start").valueAsNumber, $("loop-end").valueAsNumber);
$("clear-loop").onclick = () => setLoop(deck.loop, 0, deck.duration);
$("slot0").onclick = () => chooseSlot(0);
$("slot1").onclick = () => chooseSlot(1);
for (const id of ["mode", "volume", "fade"]) $(id).oninput = applyLevels;
$("channel").onchange = () => { applyLevels(); paint(); };
$("view").onchange = paint;
$("zoom").onchange = () => zoom(deck.position);
$("pan").oninput = () => {
  const left = Number($("pan").value);
  range = [left, left + deck.duration / Number($("zoom").value)];
  paint();
};
$("seek").oninput = () => seek(Number($("seek").value), true);
const resize = new ResizeObserver(() => paint());
resize.observe($("wave"));
const timeAt = event => {
  const rect = $("wave").getBoundingClientRect();
  const fraction = Math.max(0, Math.min(1, (event.clientX - rect.left) / rect.width));
  return range[0] + fraction * (range[1] - range[0]);
};
$("wave").onpointerdown = event => {
  if (event.button !== 0) return;
  $("wave").setPointerCapture(event.pointerId);
  drag = { x: event.clientX, time: timeAt(event) };
};
$("wave").onpointercancel = () => { drag = null; };
$("wave").onpointerup = event => {
  if (!drag || !deck) return;
  const time = timeAt(event);
  if (Math.abs(event.clientX - drag.x) > 5) setLoop(true, Math.min(drag.time, time), Math.max(drag.time, time));
  else seek(time);
  drag = null;
};

$("marker-form").onsubmit = event => {
  event.preventDefault();
  const label = $("marker-name").value.trim();
  if (!label) return;
  (session.markers[key()] ??= []).push({ time: deck.position, label });
  $("marker-name").value = "";
  persist();
  updateMarkers();
  paint();
};

$("blind-start").onclick = () => {
  $("mode").value = "MATCHED";
  session.trials[key()] = { seed: crypto.randomUUID(), preference: null, confidence: null,
    revealed: false, mode: "MATCHED" };
  persist();
  chooseSlot(0);
  refreshIdentity();
  applyLevels();
  updateMarkers();
  paint();
  $("preference").focus();
};
$("blind-form").onsubmit = event => {
  event.preventDefault();
  const t = trial();
  t.preference = $("preference").value;
  t.confidence = Number($("confidence").value) || null;
  t.timestamp = new Date().toISOString();
  persist();
  refreshIdentity();
  $("reveal").focus();
};
$("reveal").onclick = () => {
  trial().revealed = true;
  persist();
  refreshIdentity();
  applyLevels();
  updateMarkers();
  paint();
};
$("evaluate").onsubmit = event => {
  event.preventDefault();
  session.evaluations[key()] = {
    scenario: row.id, pair: [pair.a.path, pair.b.path], timestamp: new Date().toISOString(),
    fidelity: Number($("fidelity").value) || null, musicality: Number($("musicality").value) || null,
    artifacts: $("artifacts").value || null, decision: $("decision").value || null,
    differences: [...$("differences").querySelectorAll("input:checked")].map(input => input.value),
    notes: $("notes").value,
    level: { mode: $("mode").value, channel: $("channel").value, gains: trim.gains,
      protection: trim.protection, volume: Number($("volume").value) },
    blind: trial()?.preference ? { ...trial() } : null,
  };
  const saved = persist();
  refreshPackage();
  say("save-status", saved ? "Avaliação salva." : "Avaliação mantida nesta aba. Exporte o JSON.");
};

function download(name, content, type) {
  const url = URL.createObjectURL(new Blob([content], { type }));
  const anchor = Object.assign(document.createElement("a"), { href: url, download: name });
  anchor.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}
$("export-json").onclick = () => {
  const data = { ...session, provenance: pack.provenance };
  download("hydra-listening-session.json", JSON.stringify(data, null, 2), "application/json");
};
$("export-report").onclick = () => download("ACCIDENTAL_WARP_LISTENING_SIGNOFF.md", report(pack, session), "text/markdown");
$("import-json").onchange = async event => {
  const input = event.target;
  const file = input.files[0];
  input.value = "";
  if (!file) return;
  try {
    session = importSession(await file.text(), pack);
    if (persist()) notice("Sessão importada.");
    const pending = pack.scenarios.flatMap(row => row.pairs.map(pair => ({ row, pair })))
      .find(item => session.trials[keyFor(item.row, item.pair)]?.revealed === false);
    if (pending) await selectScenario(pending.row, pending.pair);
    else await selectPair();
  } catch (error) { notice(error.message); }
};

document.addEventListener("keydown", event => {
  if (!deck || event.defaultPrevented || event.ctrlKey || event.metaKey || event.altKey
    || event.target.closest("input, textarea, select, [contenteditable]")) return;
  const actions = {
    " ": () => $("play").click(), a: () => chooseSlot(0), b: () => chooseSlot(1),
    x: () => chooseSlot(0), y: () => chooseSlot(1), A: () => chooseSlot(1 - order().indexOf(deck.active)),
    l: () => $("loop").click(), r: () => $("stop").click(),
    ArrowLeft: () => seek(deck.position - 0.5, true), ArrowRight: () => seek(deck.position + 0.5, true),
  };
  if (event.key === " " && event.target.closest("button, a, summary")) return;
  if (!Object.hasOwn(actions, event.key)) return;
  event.preventDefault();
  if (!event.repeat || event.key.startsWith("Arrow")) actions[event.key]();
});
