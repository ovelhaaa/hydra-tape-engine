import {Audition, STATES, isBiasPackage, reviewCriteria, reviewStorageKey, auditionDetails, validateManifest, verifiedFile, parseTelemetry, telemetryEvents, exportReviews, importReviews} from './engine.mjs';
const $ = id => document.getElementById(id);
let manifest, files, player, selected, reviews = {}, buffers = [], events = [], generation = 0, ready = false;
const controls = ['play','stop','restart','a','b','seek','loop','prev','next','state','comments','add-note'];
function message(text, error = false) { $('message').textContent = text; $('message').className = error ? 'error' : ''; }
function enable(value) { ready = value; controls.forEach(id => $(id).disabled = !value); $('criteria').querySelectorAll('select').forEach(s => s.disabled = !value); }
function review() { return reviews[selected] ??= {state: 'NOT REVIEWED', criteria: {}, comments: '', notes: []}; }
function persist() { try { localStorage.setItem(reviewStorageKey(manifest), exportReviews(manifest, reviews)); } catch { /* Export remains available without storage. */ } }
function options(select, values, value) { select.replaceChildren(...values.map(text => {const option = document.createElement('option'); option.textContent = text; return option;})); select.value = value; }
function library() {
  $('library').replaceChildren(); let group;
  for (const [id, s] of Object.entries(manifest.scenarios)) {
    const details = auditionDetails(manifest, s);
    if (details.group !== group) { group = details.group; const heading = document.createElement('h3'); heading.textContent = group; $('library').append(heading); }
    const button = document.createElement('button'); button.className = selected === id ? 'selected' : ''; button.dataset.scenario = id;
    button.textContent = `${id.slice(0,2)} · ${details.title}`;
    const detail = document.createElement('small'); detail.textContent = `${s.bpm} BPM · ${s.source_material} · ${reviews[id]?.state ?? 'NOT REVIEWED'}`;
    button.append(detail); button.onclick = () => selectScenario(id); $('library').append(button);
  }
}
function showReview() {
  options($('state'), STATES, review().state); $('comments').value = review().comments;
  $('criteria').replaceChildren(...reviewCriteria(manifest).map(criterion => {
    const label = document.createElement('label'); label.textContent = criterion; const select = document.createElement('select');
    options(select, ['N/A', ...STATES], review().criteria[criterion] ?? 'N/A'); select.disabled = !ready;
    select.onchange = () => {review().criteria[criterion] = select.value; persist();}; label.append(select); return label;
  }));
  $('notes').replaceChildren(...review().notes.map((note, index) => {
    const row = document.createElement('div'); row.className = 'note-row'; row.textContent = `${note.time.toFixed(3)} s · ${note.severity} · ${note.description} `;
    const remove = document.createElement('button'); remove.textContent = 'Remove'; remove.onclick = () => {review().notes.splice(index, 1); persist(); showReview();}; row.append(remove); return row;
  }));
}
async function selectScenario(id) {
  const token = ++generation; player?.stop(); buffers = []; selected = id; enable(false); $('wave').getContext('2d').clearRect(0,0,$('wave').width,240);
  const s = manifest.scenarios[id], details = auditionDetails(manifest, s); library(); showReview(); $('group').textContent = details.group; $('title').textContent = details.title;
  $('description').textContent = isBiasPackage(manifest) ? `A: baseline triangular. B: ${details.b}. Ambos são sinais processados, com a mesma fonte, parâmetros e ganho. Alterne A/B no mesmo trecho e registre diferenças de apito, dinâmica e caráter de fita.` : s.description;
  $('a').textContent = `A · ${details.a}`; $('b').textContent = `B · ${details.b}`;
  $('wave').setAttribute('aria-label', `${details.a} and ${details.b} waveforms. Click to seek; shift-drag to select a loop.`);
  $('metadata').textContent = `${s.bpm} BPM · ${['Internal','Tap Tempo','MIDI Clock'][s.tempo_source]} · ${s.source_material} · ${s.duration_sec} s · ${s.sample_rate_hz} Hz`;
  $('config').textContent = JSON.stringify({parameter_ids: s.dsp_params, reverse_division: s.dsp_params['37'], feedback_percent: s.dsp_params['13']}, null, 2);
  $('metrics').textContent = JSON.stringify(s.analysis_metrics, null, 2); $('provenance').textContent = JSON.stringify({commit: manifest.reference_commit, api: manifest.api_version, environment: manifest.environment}, null, 2);
  $('events').replaceChildren(); message(`Verifying and loading ${s.name}…`);
  try {
    if (!player) { player = new Audition(new AudioContext({sampleRate: manifest.sample_rate_hz})); player.master.gain.value = Number($('volume').value); }
    const data = await Promise.all(['original_wav','processed_wav','telemetry_csv'].map(kind => verifiedFile(files.get(s[`${kind}_file`]), s[`${kind}_sha256`])));
    const decoded = await Promise.all(data.slice(0,2).map(bytes => player.context.decodeAudioData(bytes)));
    if (token !== generation) return;
    if (decoded.some(b => Math.abs(b.duration - s.duration_sec) > 1 / b.sampleRate || b.numberOfChannels !== 2)) throw Error(`Incomplete or invalid WAV: ${decoded.map(b => `${b.duration}s / ${b.sampleRate}Hz / ${b.numberOfChannels}ch`).join(', ')}`);
    const rows = parseTelemetry(new TextDecoder().decode(data[2]));
    const expectedRows = Math.ceil(Math.round(s.duration_sec * s.sample_rate_hz) / 512);
    if (rows.length !== expectedRows) throw Error('Incomplete telemetry');
    buffers = decoded; player.load(buffers); player.select(0); $('a').setAttribute('aria-pressed','true'); $('b').setAttribute('aria-pressed','false'); events = [...s.markers, ...telemetryEvents(rows)].sort((a,b) => a.time-b.time);
    $('seek').max = player.duration; $('seek').value = 0; $('loop-start').value = 0; $('loop-end').value = player.duration; $('loop').checked = false;
    $('note-time').max = player.duration; $('zoom').value = 1; $('wave').style.width = '100%';
    $('events').replaceChildren(...events.map(event => {const button = document.createElement('button'); button.className = 'event'; button.textContent = `${event.time.toFixed(3)} s · ${event.label}`; button.onclick = () => player.seek(event.time); return button;}));
    enable(true); message('Verified WAV/CSV hashes. Ready for your listening decision. No autoplay.');
  } catch (error) { if (token === generation) { player?.stop(); enable(false); message(`${s.name}: ${error.message}`, true); } }
}
$('package').onchange = async event => {
  ++generation; player?.stop(); enable(false); manifest = null; files = new Map(); $('export').disabled = true;
  try {
    const candidates = [...event.target.files].filter(f => f.name === 'manifest.json');
    if (candidates.length !== 1) throw Error('Select one extracted package folder containing manifest.json');
    manifest = validateManifest(JSON.parse(await candidates[0].text()));
    $('edition').textContent = isBiasPackage(manifest) ? 'HYDRA TAPE ENGINE 2.0 / PHASE 6.2 · BIAS A/B' : 'HYDRA TAPE ENGINE 2.0 / PHASE 6.1';
    $('comparison-help').textContent = isBiasPackage(manifest) ? 'A = baseline triangular processado · B = variante processada. Mesmo ganho, sem normalização. Comece pela candidata; registre sua avaliação antes de aprovar.' : 'Compare the source and processed signal at the same position. No normalization.';
    const prefix = candidates[0].webkitRelativePath.slice(0, -'manifest.json'.length);
    for (const file of event.target.files) if (file.webkitRelativePath.startsWith(prefix)) files.set(file.webkitRelativePath.slice(prefix.length), file);
    // Check physical presence for all scenarios before allowing audition.
    for (const s of Object.values(manifest.scenarios)) for (const kind of ['original_wav','processed_wav','telemetry_csv']) if (!files.get(s[`${kind}_file`])?.size) throw Error(`Missing/empty file: ${s[`${kind}_file`]}`);
    reviews = {}; try { const saved = localStorage.getItem(reviewStorageKey(manifest)) ?? localStorage.getItem(`hydra-lab:${manifest.reference_commit}`); if (saved) reviews = importReviews(saved, manifest); } catch { message('Stored session unavailable; import a JSON backup if needed.'); }
    const initial = isBiasPackage(manifest) ? Object.keys(manifest.scenarios).find(id => manifest.scenarios[id].comparison_variant === 'averaged') : null;
    $('export').disabled = false; await selectScenario(initial ?? Object.keys(manifest.scenarios)[0]);
  } catch (error) { manifest = null; $('library').replaceChildren(); message(error.message, true); }
};
$('play').onclick = () => player.playing ? player.pause() : player.play().catch(e => message(e.message, true));
$('stop').onclick = () => player.stop(); $('restart').onclick = () => player.seek(0);
for (const [id,index] of [['a',0],['b',1]]) $(id).onclick = () => {player.select(index); $('a').setAttribute('aria-pressed', String(index===0)); $('b').setAttribute('aria-pressed', String(index===1));};
$('volume').oninput = () => {if (player) player.master.gain.value = Number($('volume').value); $('volume-value').textContent = `${Math.round(Number($('volume').value)*100)}%`;};
$('seek').oninput = () => player.seek(Number($('seek').value));
function loop() {if (!ready) return; try {player.setLoop($('loop').checked, Number($('loop-start').value), Number($('loop-end').value));} catch(e) {message(e.message, true);} }
for (const id of ['loop','loop-start','loop-end']) $(id).onchange = loop;
for (const [id,direction] of [['prev',-1],['next',1]]) $(id).onclick = () => {const p = player.position; const list = direction < 0 ? [...events].reverse() : events; const event = list.find(e => direction < 0 ? e.time < p - .02 : e.time > p + .02); if (event) player.seek(event.time);};
$('state').onchange = () => {review().state = $('state').value; persist(); library();}; $('comments').oninput = () => {review().comments = $('comments').value; persist();};
$('add-note').onclick = () => {const time = Number($('note-time').value), description = $('note-description').value.trim(); if (!description || time < 0 || time > player.duration) return message('Enter a description and a timestamp within the scenario.', true); review().notes.push({time, description, severity: $('severity').value}); persist(); showReview(); $('note-description').value = '';};
$('export').onclick = () => {const url = URL.createObjectURL(new Blob([exportReviews(manifest, reviews)], {type:'application/json'})); const a = document.createElement('a'); a.href = url; a.download = `hydra-reviews-${manifest.reference_commit.slice(0,7)}.json`; a.click(); setTimeout(() => URL.revokeObjectURL(url),1000);};
$('import').onchange = async event => {try {if (!manifest) throw Error('Open a package first'); const imported = importReviews(await event.target.files[0].text(), manifest); reviews = {...reviews, ...imported}; persist(); showReview(); library(); message('Reviews imported. Export to keep a backup.');} catch(e) {message(e.message,true);} event.target.value = '';};
$('zoom').oninput = () => { $('wave').style.width = `${Number($('zoom').value)*100}%`; };
let dragStart;
function canvasTime(event) {const rect = $('wave').getBoundingClientRect(); return Math.max(0, Math.min(player.duration, (event.clientX-rect.left)/rect.width*player.duration));}
$('wave').onpointerdown = event => {if (!ready) return; if (event.shiftKey) {dragStart=canvasTime(event); $('wave').setPointerCapture(event.pointerId);} else player.seek(canvasTime(event));};
$('wave').onpointerup = event => {if (dragStart === undefined || !ready) return; const end=canvasTime(event); $('loop-start').value=Math.min(dragStart,end).toFixed(3); $('loop-end').value=Math.max(dragStart,end).toFixed(3); dragStart=undefined; $('loop').checked=true; loop();};
function draw() {
  if (ready && buffers.length) {
    const canvas=$('wave'), ctx=canvas.getContext('2d'); const width=Math.round(canvas.getBoundingClientRect().width); if(canvas.width!==width) canvas.width=width;
    ctx.clearRect(0,0,width,240); ctx.font='12px system-ui';
    const details=auditionDetails(manifest,manifest.scenarios[selected]);
    buffers.forEach((buffer,index) => {const data=buffer.getChannelData(0), center=60+index*120; ctx.fillStyle=index?'#9ed2d1':'#b9e3a0'; ctx.fillText(`${index?'B':'A'} · ${index?details.b:details.a} (left channel)`,8,center-42);
      for(let x=0;x<width;x++){const start=Math.floor(x/width*data.length),end=Math.max(start+1,Math.floor((x+1)/width*data.length)); let lo=1,hi=-1; for(let i=start;i<end;i++){lo=Math.min(lo,data[i]);hi=Math.max(hi,data[i]);}ctx.fillRect(x,center-hi*40,1,Math.max(1,(hi-lo)*40));}
    });
    ctx.fillStyle='#b9e3a022';ctx.fillRect(player.start/player.duration*width,0,(player.end-player.start)/player.duration*width,240);
    ctx.fillStyle='#c4a978';for(const event of events)ctx.fillRect(event.time/player.duration*width,0,1,240);
    ctx.fillStyle='#ffffff';ctx.fillRect(player.position/player.duration*width,0,2,240);
    $('seek').value=player.position; $('time').textContent=`${player.position.toFixed(3)} / ${player.duration.toFixed(3)} s`;
  }
  requestAnimationFrame(draw);
} requestAnimationFrame(draw);
