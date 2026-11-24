import { Deck } from './lab/audio.mjs';

const el = id => document.getElementById(id);
const manifest = await fetch('manifest.json').then(r => { if (!r.ok) throw Error(r.status); return r.json(); });
const manifestBytes = new TextEncoder().encode(JSON.stringify(manifest));
const identity = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', manifestBytes)),
  x => x.toString(16).padStart(2, '0')).join('');
const storageKey = `hydra-f4-${identity}`;
let saved = JSON.parse(localStorage.getItem(storageKey) || '{}');
let context, deck, request = 0;

const scene = () => manifest.scenes[Number(el('scene').value)];
const key = () => `${scene().scene}/${el('format').value}`;

for (const [i, s] of manifest.scenes.entries()) {
  el('scene').add(new Option(`${i + 1} · ${s.description}`, i));
}

function progress() {
  const done = manifest.scenes.reduce((sum, s) =>
    sum + ['RAW', 'MATCHED'].filter(f => saved[`${s.scene}/${f}`]?.preference &&
      saved[`${s.scene}/${f}`]?.confidence).length, 0);
  el('progress').textContent = `${done} / ${manifest.scenes.length * 2} avaliações completas`;
  el('reveal').disabled = done !== manifest.scenes.length * 2;
}

function save() {
  saved[key()] = { preference: el('preference').value, confidence: el('confidence').value,
    comments: el('comments').value };
  localStorage.setItem(storageKey, JSON.stringify(saved));
  progress();
}

function form() {
  for (const id of ['preference', 'confidence', 'comments'])
    el(id).value = saved[key()]?.[id] || '';
  progress();
}

async function load() {
  const current = ++request;
  deck?.close();
  deck = null;
  el('play').disabled = true;
  el('status').textContent = 'Carregando e verificando WAVs…';
  el('answers').textContent = '';
  form();
  try {
    context ??= new AudioContext();
    const s = scene();
    el('description').textContent = `${s.description} · ${el('format').value} · comandos ` +
      s.events.map(e => `${e.seconds.toFixed(2)} s`).join(', ');
    const buffers = await Promise.all(s.slots.map(async slot => {
      const f = slot.files[el('format').value];
      const response = await fetch(f.path);
      if (!response.ok) throw Error(`${f.path}: ${response.status}`);
      const bytes = await response.arrayBuffer();
      const hash = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', bytes)),
        x => x.toString(16).padStart(2, '0')).join('');
      if (hash !== f.sha256) throw Error(`SHA-256 inválido: ${f.path}`);
      return context.decodeAudioData(bytes);
    }));
    if (current !== request) return;
    deck = new Deck(context, buffers);
    deck.configure([1, 1], 1, Number(el('volume').value));
    el('slots').replaceChildren(...s.slots.map((slot, i) => {
      const button = document.createElement('button');
      button.textContent = slot.code;
      button.dataset.slot = i;
      button.setAttribute('aria-pressed', String(i === 0));
      button.onclick = () => select(i);
      return button;
    }));
    el('seek').max = deck.duration;
    el('play').disabled = false;
    el('status').textContent = 'Pronto · dois WAVs verificados por SHA-256';
  } catch (error) {
    el('status').textContent = `Erro: ${error.message}`;
  }
}

function select(i) {
  if (!deck) return;
  deck.select(i);
  for (const b of el('slots').children)
    b.setAttribute('aria-pressed', String(Number(b.dataset.slot) === i));
}

el('play').onclick = async () => {
  const active = deck;
  if (!active) return;
  await context.resume();
  if (active !== deck) return;
  active.run ? active.pause() : active.start();
};
el('restart').onclick = () => deck?.seek(0);
el('seek').oninput = () => deck?.seek(Number(el('seek').value));
el('volume').oninput = () => deck?.configure([1, 1], 1, Number(el('volume').value));
for (const id of ['preference', 'confidence', 'comments'])
  el(id).addEventListener('input', save);
for (const id of ['scene', 'format'])
  el(id).addEventListener('change', load);
el('export').onclick = () => {
  const blob = new Blob([JSON.stringify({ manifest_sha256: identity, method: manifest.method,
    decisions: saved }, null, 2)], { type: 'application/json' });
  const a = document.createElement('a');
  a.href = URL.createObjectURL(blob);
  a.download = 'hydra-f4-listening-decision.json';
  a.click();
  setTimeout(() => URL.revokeObjectURL(a.href), 1000);
};
el('reveal').onclick = async () => {
  if (el('reveal').disabled) return;
  const r = await fetch('answer-key.json');
  if (!r.ok) throw Error(r.status);
  el('answers').textContent = JSON.stringify(await r.json(), null, 2);
};
document.addEventListener('keydown', e => {
  if (['INPUT', 'SELECT', 'TEXTAREA'].includes(e.target.tagName)) return;
  if (['1', '2'].includes(e.key)) select(Number(e.key) - 1);
  if (e.code === 'Space') { e.preventDefault(); el('play').click(); }
});
setInterval(() => { if (deck && !el('seek').matches(':active')) el('seek').value = deck.position; }, 100);
await load();
