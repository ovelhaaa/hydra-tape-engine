export const decisions = ["APPROVE", "APPROVE WITH NOTES", "NEEDS INVESTIGATION", "REJECT"];
export const differences = ["Brilho", "Volume", "Pitch", "Ritmo", "Imagem estéreo", "Saturação", "Feedback", "Outros"];
export const artifacts = ["Nenhum", "Leve", "Moderado", "Grave"];
export const keyFor = (row, pair) => JSON.stringify([row.id, pair.id]);
export const storageKey = pack => `hydra/listening/v1/${pack.id}`;
export const newSession = pack => ({ schemaVersion: 1, pr: 47, packageId: pack.id, commit: pack.commit,
  evaluations: {}, trials: {}, markers: {} });

export function mapping(seed, key) {
  let hash = 2166136261;
  for (const char of `${seed}:${key}`) hash = Math.imul(hash ^ char.charCodeAt(0), 16777619);
  return (hash >>> 0) >> 12 & 1;
}

export function importSession(json, pack) {
  const data = JSON.parse(json);
  if (data.schemaVersion !== 1 || data.packageId !== pack.id || data.commit !== pack.commit || data.pr !== 47) {
    throw new Error("A avaliação pertence a outro pacote, commit ou versão de esquema.");
  }
  const allowed = new Map(pack.scenarios.flatMap(row => row.pairs.map(pair => [keyFor(row, pair), [row, pair]])));
  const dictionary = value => value && typeof value === "object" && !Array.isArray(value);
  const rating = value => value == null || (Number.isInteger(value) && value >= 1 && value <= 5);
  if (!["evaluations", "trials", "markers"].every(key => dictionary(data[key]))) {
    throw new Error("Estrutura de sessão inválida.");
  }
  for (const [key, value] of Object.entries(data.evaluations)) {
    const [row, pair] = allowed.get(key) ?? [];
    if (!pair || value.scenario !== row.id || JSON.stringify(value.pair) !== pair.id
      || !Number.isFinite(Date.parse(value.timestamp)) || typeof value.notes !== "string"
      || ![null, ...decisions].includes(value.decision) || ![null, ...artifacts].includes(value.artifacts)
      || !rating(value.fidelity) || !rating(value.musicality)
      || !value.level || !["RAW", "MATCHED"].includes(value.level.mode)
      || !Array.isArray(value.level.gains) || value.level.gains.length !== 2
      || !value.level.gains.every(gain => Number.isFinite(gain) && gain >= 0 && gain <= 1)
      || !Number.isFinite(value.level.protection)
      || !Array.isArray(value.differences) || value.differences.some(item => !differences.includes(item))) {
      throw new Error("Avaliação inválida para este pacote.");
    }
  }
  for (const [key, trial] of Object.entries(data.trials)) {
    if (!allowed.has(key) || typeof trial.seed !== "string" || !trial.seed
      || ![null, "X", "Y", "Equivalentes"].includes(trial.preference) || !rating(trial.confidence)
      || typeof trial.revealed !== "boolean" || (trial.revealed && !trial.preference)) {
      throw new Error("Sessão cega inválida.");
    }
  }
  for (const [key, markers] of Object.entries(data.markers)) {
    if (!allowed.has(key) || !Array.isArray(markers) || markers.some(marker =>
      !Number.isFinite(marker.time) || marker.time < 0 || typeof marker.label !== "string")) {
      throw new Error("Marcadores inválidos.");
    }
  }
  return data;
}

export function overall(pack, session) {
  const pending = [];
  const reviewed = [];
  for (const row of pack.scenarios) {
    if (!row.pairs.length) pending.push(`${row.label} · sem par`);
    for (const pair of row.pairs) {
      const record = session.evaluations[keyFor(row, pair)];
      if (record) reviewed.push({ row, pair, record });
      if (!record?.decision || !pair.a.file || !pair.b.file) pending.push(`${row.label} · ${pair.label}`);
    }
  }
  const has = decision => reviewed.some(item => item.record.decision === decision);
  const status = has("REJECT") || has("NEEDS INVESTIGATION") ? "REQUIRES FIXES"
    : pack.incomplete || pending.length || !reviewed.length ? "INCOMPLETE"
      : has("APPROVE WITH NOTES") ? "APPROVED WITH RESERVATIONS" : "APPROVED";
  return { status, pending, reviewed };
}

const inline = value => String(value ?? "—").replace(/[\r\n]+/g, " ").replace(/[\\`*_~<>\[\]|]/g, "\\$&");

export function report(pack, session) {
  const result = overall(pack, session);
  const lines = ["# Escuta A/B — Accidental Warp V1.3", "", `- **Status** \`${result.status}\``,
    "- **PR** [#47](https://github.com/ovelhaaa/hydra-tape-engine/pull/47)", `- **Commit avaliado** ${inline(pack.commit ?? "Indisponível")}`,
    `- **Pacote de WAVs** ${inline(pack.title)} · \`${pack.id}\``, "", "## Cenários pendentes", "",
    ...(result.pending.length ? result.pending.map(label => `- ${inline(label)}`) : ["Nenhum."]),
    "", "## Avaliações e problemas encontrados", ""];
  if (!result.reviewed.length) lines.push("Nenhuma avaliação salva.", "");
  for (const { row, pair, record } of result.reviewed) {
    const trial = session.trials[keyFor(row, pair)];
    lines.push(`### ${inline(row.label)} · ${inline(pair.label)}`, "",
      pair.solo ? `- **Arquivo** ${inline(pair.a.path)}` : `- **Arquivos A/B** ${inline(pair.a.path)} / ${inline(pair.b.path)}`,
      `- **Decisão** ${inline(record.decision ?? "Pendente")} · ${record.timestamp}`,
      `- **Fidelidade / Musicalidade** ${inline(record.fidelity)} / ${inline(record.musicality)}`,
      `- **Artefatos** ${inline(record.artifacts)}`, `- **Diferenças** ${inline(record.differences.join(", ") || "—")}`,
      `- **Monitoração** ${record.level.mode} · canal ${inline(record.level.channel)} · ganhos A/B ${record.level.gains.map(g => g.toFixed(4)).join(" / ")} · proteção ${record.level.protection.toFixed(4)}`);
    if (trial?.preference) {
      const order = mapping(trial.seed, keyFor(row, pair)) ? ["B", "A"] : ["A", "B"];
      const reveal = trial.revealed ? ` · X = ${order[0]}, Y = ${order[1]}` : "";
      lines.push(`- **Escuta cega** ${inline(trial.preference)} · confiança ${inline(trial.confidence)} · semente ${inline(trial.seed)}${reveal}`);
    }
    lines.push("", ...(record.notes ? record.notes.split(/\r?\n/).map(line => `> ${inline(line)}`) : ["Sem observações." ]), "");
  }
  return lines.join("\n") + "\n";
}
