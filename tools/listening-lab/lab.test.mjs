import { test } from "node:test";
import assert from "node:assert/strict";
import { inventory, openPackage, parseManifest, suggestions } from "./inventory.mjs";
import { match, levels } from "./audio.mjs";
import { importSession, keyFor, mapping, newSession, overall, report } from "./session.mjs";

const file = (path, payload) => Object.assign(new File([payload], path.split("/").at(-1)), { webkitRelativePath: path });
const warp = { suite: "Accidental Warp V1.3", scenarios: [{ id: "grain", git_commit: "abc",
  sample_rate_hz: 48000, frames: 24000, automation: [{ frame: 12000, parameter: "Grain", value: 80 }],
  variants: [{ engines: {
    worklet: { output_wav: "grain/01_worklet.wav" },
    python: { output_wav: "grain/01_python.wav" },
    native: { output_wav: "grain/01_native.wav" },
  } }, { engines: { native: { output_wav: "grain/02_native.wav" } } }] }] };

test("Warp variants, commit, timing, all three pairs and missing WAVs", async () => {
  const source = await inventory([
    file("lab/manifest.json", JSON.stringify(warp)), file("lab/grain/01_worklet.wav", "worklet"),
    file("lab/grain/01_native.wav", "native"), file("lab/grain/02_native.wav", "native"),
  ]);
  const pack = await openPackage(source, source.packages[0]);
  assert.equal(pack.commit, "abc");
  const [row, lone] = pack.scenarios;
  assert.equal(row.events[0].time, 0.25);
  assert.equal(row.duration, 0.5);
  assert.deepEqual(row.pairs.map(p => p.label), [
    "Original Worklet × Python", "Original Worklet × TapeCore Native", "Python × TapeCore Native",
  ]);
  assert.equal(row.pairs[0].b.file, null);
  assert.ok(row.pairs[1].a.hash);
  assert.equal(lone.pairs[0].solo, true);
});

test("release gate times are seconds, and CI identifies Native/WASM", () => {
  const release = parseManifest("manifest.json", {
    warp_automation: { rate: 48000, frames: 480000, events: [[2, 32, 20]],
      comparisons: { before_vs_head: {}, strict_vs_fast: {} } },
  });
  assert.deepEqual(release.scenarios[0].events, [{ time: 2, label: "Grain → 20" }]);
  assert.equal(release.scenarios[0].pairs[0].kind, "Regressão");
  const ci = parseManifest("review.json", { rate: 48000, scenes: [{ scene: "tape", seconds: 3, wasm_peak: .5 }] });
  assert.equal(ci.scenarios[0].pairs[0].label, "TapeCore Native × WASM");
  assert.equal(ci.scenarios[0].input, "tape/dry.wav");
});

test("ambiguous filenames require a unique path and no-manifest suggestions require confirmation", async () => {
  const source = await inventory([file("lab/one/baseline.wav", "a"), file("lab/one/optimized.wav", "b"),
    file("lab/two/baseline.wav", "a"), file("lab/one/optimized.wav", "duplicate")]);
  assert.equal(source.packages.length, 0);
  assert.deepEqual(suggestions(source), []);
  const clean = await inventory([file("lab/one/baseline.wav", "a"), file("lab/one/optimized.wav", "b")]);
  assert.deepEqual(suggestions(clean), [{ a: "lab/one/baseline.wav", b: "lab/one/optimized.wav", kind: "Regressão" }]);
});

test("RMS matching attenuates the louder source and peak protection is common", () => {
  const signal = Float32Array.of(0.5, -0.5, 0.5, -0.5, 1, 1);
  const buffer = { length: 6, duration: 3, sampleRate: 2, numberOfChannels: 1, getChannelData: () => signal };
  assert.deepEqual(levels(buffer, "R", 2), { rms: .5, peak: .5 });
  assert.deepEqual(match([{ rms: .5, peak: 1.2 }, { rms: .25, peak: .8 }], "MATCHED").gains, [.5, 1]);
  assert.ok(Math.abs(match([{ rms: .25, peak: 1.225 }, { rms: 0, peak: 0 }], "RAW").protection - .8) < 1e-12);
  assert.deepEqual(match([{ rms: .25, peak: .2 }, { rms: 0, peak: 0 }], "MATCHED").gains, [1, 1]);
});

test("content-bound session, reproducible blind allocation and honest report", async () => {
  const open = async sound => openPackage(await inventory([file("baseline.wav", "a"), file("optimized.wav", sound)]),
    null, { a: "baseline.wav", b: "optimized.wav", id: "tape", kind: "Regressão" });
  const pack = await open("b");
  assert.equal((await open("b")).id, pack.id);
  const session = newSession(pack);
  const [row] = pack.scenarios;
  const [pair] = row.pairs;
  const key = keyFor(row, pair);
  assert.equal(overall(pack, session).status, "INCOMPLETE");
  session.evaluations[key] = { scenario: row.id, pair: [pair.a.path, pair.b.path], notes: "Ataque > evidente",
    decision: "APPROVE WITH NOTES", timestamp: "2026-11-15T03:04:05Z", differences: ["Brilho"],
    artifacts: "Leve", level: { mode: "MATCHED", channel: "Stereo", gains: [1, .8], protection: 1 } };
  session.trials[key] = { seed: "seed", preference: "X", confidence: 4, revealed: true };
  const json = JSON.stringify(session);
  assert.deepEqual(importSession(json, pack), session);
  const changed = await open("changed sound");
  assert.throws(() => importSession(json, changed), /outro pacote/);
  assert.equal(mapping(session.trials[key].seed, key), mapping(JSON.parse(json).trials[key].seed, key));
  assert.equal(overall(pack, session).status, "APPROVED WITH RESERVATIONS");
  const md = report(pack, session);
  assert.match(md, /APPROVED WITH RESERVATIONS/);
  assert.match(md, /Escuta cega/);
  assert.match(md, /Ataque/);
  session.evaluations[key].decision = "REJECT";
  assert.equal(overall(pack, session).status, "REQUIRES FIXES");
});
