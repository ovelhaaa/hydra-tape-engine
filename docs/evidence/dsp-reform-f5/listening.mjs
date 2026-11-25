import { Deck } from './lab/audio.mjs';

const el = id => document.getElementById(id);
const manifest = await fetch('manifest.json').then(r => {
  if (!r.ok) throw Error(r.status);
  return r.json();
});
const manifestBytes = new TextEncoder().encode(JSON.stringify(manifest));
const identity = Array.from(
  new Uint8Array(await crypto.subtle.digest('SHA-256', manifestBytes)),
  x => x.toString(16).padStart(2, '0')).join('');
const storageKey = `hydra-f5-${identity}`;
let saved = JSON.parse(localStorage.getItem(storageKey) || '{}');
let context, deck, buffers, request = 0;

// Three-source deck: the shared Lab deck assumes two slots, so the gain
// crossfade is replaced by an equal-duration N-way fade over the same nodes.
class Deck3 extends Deck {
  select(index) {
    const time = this.context.currentTime;
    this.switches.forEach(({ gain }, i) => {
      if (gain.cancelAndHoldAtTime) gain.cancelAndHoldAtTime(time);
      else gain.setValueAtTime(gain.value, time);
      gain.linearRampToValueAtTime(Number(i === index), time + this.fade);
    });
    this.active = index;
  }
}

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
  saved[key()] = {
    preference: el('preference').value,
    confidence: el('confidence').value,
    comments: el('comments').value,
  };
  localStorage.setItem(storageKey, JSON.stringify(saved));
  progress();
}

function form() {
  for (const id of ['preference', 'confidence', 'comments'])
    el(id).value = saved[key()]?.[id] || '';
  progress();
}

function drawWave() {
  const canvas = el('wave');
  const ctx = canvas.getContext('2d');
  const w = canvas.width, h = canvas.height;
  ctx.clearRect(0, 0, w, h);
  if (!buffers) return;
  const data = buffers[deck?.active ?? 0]?.getChannelData(0);
  if (!data) return;
  const step = Math.max(1, Math.floor(data.length / w));
  ctx.fillStyle = '#4fd1c5';
  for (let x = 0; x < w; x++) {
    let lo = 1, hi = -1;
    for (let i = x * step; i < Math.min(data.length, (x + 1) * step); i += 8) {
      lo = Math.min(lo, data[i]);
      hi = Math.max(hi, data[i]);
    }
    const y0 = (1 - hi) * h / 2, y1 = (1 - lo) * h / 2;
    ctx.fillRect(x, y0, 1, Math.max(1, y1 - y0));
  }
  const s = scene();
  const duration = s.duration_sec || 1;
  ctx.fillStyle = '#e53e3e';
  for (const t of s.events) {
    const x = Math.round(t / duration * w);
    ctx.fillRect(x, 0, 2, h);
    ctx.fillStyle = '#fff';
    ctx.font = '11px sans-serif';
    ctx.fillText(`${t.toFixed(2)}s`, Math.min(w - 38, x + 4), 14);
    ctx.fillStyle = '#e53e3e';
  }
  if (deck) {
    const x = Math.round(deck.position / duration * w);
    ctx.fillStyle = '#f6e05e';
    ctx.fillRect(x, 0, 1, h);
  }
}

function commandFlash() {
  if (!deck) return;
  const s = scene();
  const p = deck.position;
  const near = s.events.find(t => Math.abs(t - p) < 0.09);
  el('flash').textContent = near !== undefined ? `COMANDO ${near.toFixed(2)} s` : '—';
  el('flash').style.background = near !== undefined ? '#e53e3e' : 'transparent';
  el('flash').style.color = near !== undefined ? '#fff' : '#000';
  el('command').textContent = s.events.length
    ? `Comandos: ${s.events.map(t => `${t.toFixed(2)} s`).join(', ')}`
    : 'Controle negativo: nenhum comando de tempo';
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
    el('description').textContent = `${s.description} · ${el('format').value}`;
    buffers = await Promise.all(s.slots.map(async slot => {
      const f = slot.files[el('format').value];
      const response = await fetch(f.path);
      if (!response.ok) throw Error(`${f.path}: ${response.status}`);
      const bytes = await response.arrayBuffer();
      const hash = Array.from(
        new Uint8Array(await crypto.subtle.digest('SHA-256', bytes)),
        x => x.toString(16).padStart(2, '0')).join('');
      if (hash !== f.sha256) throw Error(`SHA-256 inválido: ${f.path}`);
      return context.decodeAudioData(bytes);
    }));
    if (current !== request) return;
    deck = new Deck3(context, buffers);
    deck.configure([1, 1, 1], 1, Number(el('volume').value));
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
    el('status').textContent = 'Pronto · três WAVs verificados por SHA-256';
    drawWave();
    commandFlash();
  } catch (error) {
    el('status').textContent = `Erro: ${error.message}`;
  }
}

function select(i) {
  if (!deck) return;
  deck.select(i);
  for (const b of el('slots').children)
    b.setAttribute('aria-pressed', String(Number(b.dataset.slot) === i));
  drawWave();
}

el('play').onclick = async () => {
  const active = deck;
  if (!active) return;
  await context.resume();
  if (active !== deck) return;
  active.run ? active.pause() : active.start();
};
el('restart').onclick = () => deck?.seek(0);
el('seek').oninput = () => {
  deck?.seek(Number(el('seek').value));
  drawWave();
};
el('volume').oninput = () => deck?.configure([1, 1, 1], 1, Number(el('volume').value));
for (const id of ['preference', 'confidence', 'comments'])
  el(id).addEventListener('input', save);
for (const id of ['scene', 'format'])
  el(id).addEventListener('change', load);
el('export').onclick = () => {
  const blob = new Blob([JSON.stringify({
    manifest_sha256: identity, method: manifest.method, decisions: saved,
  }, null, 2)], { type: 'application/json' });
  const a = document.createElement('a');
  a.href = URL.createObjectURL(blob);
  a.download = 'hydra-f5-listening-decision.json';
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
  if (['1', '2', '3'].includes(e.key)) select(Number(e.key) - 1);
  if (e.code === 'Space') {
    e.preventDefault();
    el('play').click();
  }
});
setInterval(() => {
  if (deck && !el('seek').matches(':active')) el('seek').value = deck.position;
  commandFlash();
  if (deck?.run) drawWave();
}, 100);
await load();
