import { test } from "node:test";
import assert from "node:assert/strict";
import { createServer } from "node:http";
import { readFile } from "node:fs/promises";
import { createRequire } from "node:module";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";

const require = createRequire(import.meta.url);
const appRoot = fileURLToPath(new URL(".", import.meta.url));

async function setup(t) {
  const { chromium } = require(process.env.HYDRA_PLAYWRIGHT ?? "playwright");
  const browser = await chromium.launch(process.env.HYDRA_BROWSER ? { executablePath: process.env.HYDRA_BROWSER } : {});
  const server = createServer(async (request, response) => {
    const name = request.url === "/" ? "index.html" : request.url.slice(1);
    const type = { html: "text/html", mjs: "text/javascript", css: "text/css" }[name.split(".").at(-1)];
    if (!type || !/^[\w.-]+$/.test(name)) return response.writeHead(404).end();
    try { response.writeHead(200, { "Content-Type": type }).end(await readFile(resolve(appRoot, name))); }
    catch { response.writeHead(404).end(); }
  });
  await new Promise(done => server.listen(0, "127.0.0.1", done));
  t.after(async () => { await browser.close(); await new Promise(done => server.close(done)); });
  const page = await browser.newPage({ viewport: { width: 1460, height: 940 } });
  const errors = [];
  page.on("pageerror", error => errors.push(error.message));
  await page.goto(`http://127.0.0.1:${server.address().port}/`);
  return { page, errors };
}

test("Web Audio renders two synchronized sources without restarting on A/B, including loop and channel routing", async t => {
  const { page, errors } = await setup(t);
  const rendered = await page.evaluate(async () => {
    const { Deck } = await import("/audio.mjs");
    const rate = 48000;
    const context = new OfflineAudioContext(2, rate / 2, rate);
    const a = new AudioBuffer({ sampleRate: rate, length: rate, numberOfChannels: 2 });
    const b = new AudioBuffer({ sampleRate: rate, length: rate, numberOfChannels: 2 });
    a.getChannelData(0).fill(.2);
    a.getChannelData(1).fill(.1);
    for (let i = 0; i < rate; i++) b.getChannelData(1)[i] = i / rate;
    const deck = new Deck(context, [a, b]);
    deck.route("R");
    deck.configure([1, 1], 1, 1);
    deck.seek(.2);
    deck.setLoop(true, .2, .4);
    deck.start();
    const original = deck.run;
    const rendering = context.startRendering();
    await context.suspend(.1);
    const timeBefore = deck.position;
    deck.select(1);
    const timeAfter = deck.position;
    const sameRun = original === deck.run;
    await context.resume();
    const out = await rendering;
    const sample = (at, channel = 0) => out.getChannelData(channel)[Math.round(at * rate)];
    return { timeBefore, timeAfter, sameRun, before: sample(.08), after: sample(.15),
      loop: sample(.35), stereo: sample(.15, 1),
      difference: Math.max(...out.getChannelData(0).slice(4700, 5500).map((v, i) =>
        Math.abs(v - out.getChannelData(0)[4699 + i]))) };
  });
  assert.equal(rendered.sameRun, true);
  assert.equal(rendered.timeBefore, rendered.timeAfter);
  assert.ok(Math.abs(rendered.before - .1) < .001);
  assert.ok(Math.abs(rendered.after - .338) < .002);
  assert.ok(Math.abs(rendered.loop - rendered.after) < .002);
  assert.equal(rendered.after, rendered.stereo);
  assert.ok(rendered.difference < .002, JSON.stringify(rendered));
  assert.deepEqual(errors, []);
});

const real = process.env.HYDRA_RELEASE_ARTIFACT;
test("real release WAVs through seek, switch, loop, blind reveal, persistence and exports", { skip: !real }, async t => {
  const { page, errors } = await setup(t);
  await page.locator("#folder").setInputFiles(resolve(real));
  await page.locator("#player").waitFor({ timeout: 60_000 });
  await page.locator("#scenarios").getByRole("button", { name: /^tape clean/i }).click();
  await page.locator("#player").waitFor();
  assert.match(await page.locator("#pair option:checked").textContent(), /Baseline × Optimized/);
  assert.match(await page.locator("#meters").textContent(), /RMS/);
  await page.locator("#mode").selectOption("MATCHED");
  await page.locator("#loop-start").fill("0.2");
  await page.locator("#loop-end").fill("0.8");
  await page.locator("#set-loop").click();
  await page.locator("#play").click();
  await page.waitForFunction(() => document.querySelector("#time").textContent.startsWith("00:00.3"));
  await page.locator("#slot1").click();
  assert.equal(await page.locator("#slot1").getAttribute("aria-pressed"), "true");
  assert.match(await page.locator("#play").textContent(), /Pausar/);
  await page.waitForFunction(() => document.querySelector("#time").textContent.startsWith("00:00.7"));
  await page.waitForFunction(() => document.querySelector("#time").textContent.startsWith("00:00.2"));
  await page.locator("#play").click();
  await page.locator("#stop").click();
  await page.locator("#blind-start").click();
  const visible = await page.locator("body").innerText();
  for (const hidden of ["Baseline", "Optimized", "before.wav", "head.wav", "Strict × Fast"]) {
    assert.ok(!visible.includes(hidden), `Blind mode leaked ${hidden}`);
  }
  assert.equal(await page.locator("#slot0").innerText(), "X");
  assert.equal(await page.locator("#reveal").isVisible(), false);
  await page.locator("#preference").selectOption("X");
  await page.locator("#confidence").selectOption("4");
  await page.locator("#blind-form button").click();
  assert.doesNotMatch(await page.locator("#blind-result").textContent(), /X =/);
  await page.reload();
  await page.locator("#folder").setInputFiles(resolve(real));
  await page.locator("#reveal").waitFor({ timeout: 60_000 });
  assert.equal(await page.locator("#slot0").innerText(), "X");
  await page.locator("#reveal").click();
  assert.match(await page.locator("#blind-result").textContent(), /X = (before|head) · Y = (head|before)/);
  await page.locator("#decision").selectOption("APPROVE WITH NOTES");
  await page.locator("#notes").fill("Validado no teste de interface.");
  await page.locator("#evaluate button").click();
  assert.equal(await page.locator("#overall").textContent(), "INCOMPLETE");
  for (const id of ["export-json", "export-report"]) {
    const [download] = await Promise.all([page.waitForEvent("download"), page.locator("#" + id).click()]);
    const path = await download.path();
    const content = await readFile(path, "utf8");
    if (id === "export-json") {
      assert.match(content, /"schemaVersion": 1/);
      await page.locator("#import-json").setInputFiles(path);
      await page.waitForFunction(() => document.querySelector("#notice").textContent === "Sessão importada.");
    } else {
      assert.match(content, /INCOMPLETE/);
      assert.match(content, /Cenários pendentes/);
      assert.match(content, /Validado no teste/);
    }
  }
  await page.reload();
  await page.locator("#folder").setInputFiles(resolve(real));
  await page.locator("#player").waitFor({ timeout: 60_000 });
  await page.locator("#scenarios").getByRole("button", { name: /^tape clean/i }).click();
  await page.locator("#player").waitFor();
  assert.equal(await page.locator("#notes").inputValue(), "Validado no teste de interface.");
  assert.deepEqual(errors, []);
});

const warp = process.env.HYDRA_WARP_ARTIFACT;
test("real Warp package loads three decodable engines and automation markers", { skip: !warp }, async t => {
  const { page, errors } = await setup(t);
  await page.locator("#folder").setInputFiles(resolve(warp));
  await page.locator("#player").waitFor({ timeout: 60_000 });
  assert.equal(await page.locator("#pair option").count(), 3);
  for (const item of await page.locator("#pair option").all()) {
    await page.locator("#pair").selectOption(await item.getAttribute("value"));
    await page.locator("#player").waitFor();
    assert.match(await page.locator("#meters").textContent(), /RMS/);
  }
  await page.locator("#scenarios").getByRole("button", { name: /^grain/i }).click();
  await page.locator("#player").waitFor();
  assert.match(await page.locator("#markers").innerText(), /00:00.299 · grainMs/);
  await page.locator("#view").selectOption("Difference");
  assert.match(await page.locator("#wave-key").innerText(), /lag 0 ms/);
  await page.getByRole("button", { name: /^toggle/i }).click();
  await page.locator("#player").waitFor();
  assert.equal(await page.locator("#slot1").isVisible(), false);
  assert.match(await page.locator("#pair option:checked").textContent(), /Escuta individual/);
  assert.deepEqual(errors, []);
});

const ci = process.env.HYDRA_CI_ARTIFACT;
test("extracted CI package and explicit file pairing work at mobile width", { skip: !ci }, async t => {
  const { page, errors } = await setup(t);
  await page.setViewportSize({ width: 390, height: 844 });
  await page.locator("#commit").fill("18351893431e994535be7929daa38eae55c9a870");
  await page.locator("#folder").setInputFiles(resolve(ci));
  await page.locator("#player").waitFor({ timeout: 60_000 });
  assert.match(await page.locator("#package-meta").innerText(), /9 cenários · commit 1835189/);
  assert.match(await page.locator("#pair option:checked").textContent(), /TapeCore Native × WASM/);
  assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);

  await page.locator("#files").setInputFiles(["native.wav", "wasm.wav"].map(name => resolve(ci, "single_head_impulse", name)));
  await page.locator("#suggestions").selectOption("0");
  assert.equal(await page.locator("#workspace").isVisible(), false);
  await page.locator("#manual button").first().click();
  await page.locator("#manual-open").click();
  await page.locator("#player").waitFor();
  assert.match(await page.locator("#pair option:checked").textContent(), /native × wasm/);
  assert.deepEqual(errors, []);
});
