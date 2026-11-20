import { chromium } from 'playwright';
import { createServer } from 'node:http';
import { readFile, mkdir, writeFile } from 'node:fs/promises';
import { resolve, extname } from 'node:path';
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import assert from 'node:assert/strict';
const root = resolve(import.meta.dirname, '../../..');
const build = resolve(root, process.env.HYDRA_WEB_BUILD || 'build-wasm/web');
const output = resolve(root, process.env.HYDRA_RESET_REPORT || 'artifacts/reset-browser');
await mkdir(output, { recursive: true });
const server = createServer(async (req, res) => {
  try {
    const name = req.url.split('?')[0];
    if (name === '/') { res.end('<!doctype html><title>Hydra real AudioWorklet reset harness</title>'); return; }
    let data;
    if (name === '/resetController.js') data = await readFile(resolve(root, 'host/web/resetController.js'));
    else if (name === '/hydra-processor.js' || name === '/fault-processor.js') {
      data = await readFile(resolve(root, 'host/web/hydra-processor.js'), 'utf8');
      if (name === '/fault-processor.js') data = data.replace("registerProcessor('hydra-processor', HydraProcessor);", `
        class FaultProcessor extends HydraProcessor {
          onMessage(msg) {
            if (msg.type === 'testFault') { this.fault = msg.mode; this.port.postMessage({type:'faultReady'}); return; }
            if (msg.type === 'reset' && this.fault === 'error') {
              const reset = this.api.reset;
              this.api.reset = () => { throw new Error('controlled reset failure'); };
              try { super.onMessage(msg); } finally { this.api.reset = reset; }
              return;
            }
            if (msg.type === 'reset' && this.fault === 'timeout') {
              const post = this.port.postMessage.bind(this.port);
              this.port.postMessage = data => { if (data.key !== 'reset') post(data); };
              try { super.onMessage(msg); } finally { this.port.postMessage = post; }
              return;
            }
            super.onMessage(msg);
          }
        }
        registerProcessor('hydra-processor', FaultProcessor);`);
    } else if (['/hydra_dsp.js', '/hydra_dsp.wasm'].includes(name)) data = await readFile(resolve(build, name.slice(1)));
    else throw Error('Not found');
    res.setHeader('Content-Type', ({'.js':'text/javascript','.wasm':'application/wasm'})[extname(name)] || 'text/plain');
    res.end(data);
  } catch { res.statusCode = 404; res.end('Missing'); }
});
await new Promise(r => server.listen(0, '127.0.0.1', r));
const browser = await chromium.launch({ headless: true, ...(process.env.HYDRA_BROWSER_CHANNEL ? {channel: process.env.HYDRA_BROWSER_CHANNEL} : {}), args: ['--autoplay-policy=no-user-gesture-required'] });
const results = [];
try {
  for (const scenario of ['running', 'stopped', 'error', 'repeated', 'timeout']) {
    const page = await browser.newPage();
    await page.goto(`http://127.0.0.1:${server.address().port}`);
    const result = await page.evaluate(async scenario => {
      const { createResetController } = await import('/resetController.js');
      const context = new AudioContext({ sampleRate: 48000 });
      const events = [];
      const fault = ['error', 'timeout'].includes(scenario);
      await context.audioWorklet.addModule(fault ? '/fault-processor.js' : '/hydra-processor.js');
      const wasmBinary = await (await fetch('/hydra_dsp.wasm')).arrayBuffer();
      const node = new AudioWorkletNode(context, 'hydra-processor', { processorOptions: { wasmBinary }, outputChannelCount: [2] });
      const ready = new Promise((resolve, reject) => node.port.addEventListener('message', ({data}) => {
        if (data.type === 'ready') resolve();
        if (data.type === 'error') reject(new Error(data.message));
      }));
      node.port.start();
      node.port.addEventListener('message', ({data}) => {
        if (data.key === 'reset') events.push({ type: data.type, requestId: data.requestId, state: context.state, audioTime: context.currentTime, time: performance.now() });
      });
      const oscillator = new OscillatorNode(context, { frequency: 440 });
      const analyser = new AnalyserNode(context);
      oscillator.connect(node).connect(analyser).connect(context.destination);
      oscillator.start();
      await context.resume(); await ready;
      await new Promise(r => setTimeout(r, 120));
      const samples = new Float32Array(analyser.fftSize); analyser.getFloatTimeDomainData(samples);
      const energy = samples.reduce((s, v) => s + v*v, 0);
      if (scenario === 'stopped') await context.suspend();
      if (fault) {
        const configured = new Promise(resolve => { const listener = ({data}) => { if (data.type === 'faultReady') { node.port.removeEventListener('message', listener); resolve(); } }; node.port.addEventListener('message', listener); });
        node.port.postMessage({type:'testFault',mode:scenario}); await configured;
      }
      const suspend = context.suspend.bind(context), resume = context.resume.bind(context);
      context.suspend = async () => { await suspend(); events.push({type:'suspended',time:performance.now(),audioTime:context.currentTime}); };
      context.resume = async () => { events.push({type:'resumeRequested',time:performance.now()}); await resume(); };
      let posts = 0;
      const post = node.port.postMessage.bind(node.port);
      node.port.postMessage = data => { if (data.type === 'reset') { posts++; events.push({type:'resetPosted',state:context.state,time:performance.now(),audioTime:context.currentTime}); } post(data); };
      const reset = createResetController({getContext:()=>context,getNode:()=>node});
      const start = performance.now(); let error = null, coalesced = null;
      try { const first = reset(); if (scenario === 'repeated') { const second = reset(); coalesced = first === second; await Promise.all([first, second]); } else await first; }
      catch (e) { error = e.message; }
      const elapsedMs = performance.now() - start;
      const state = context.state;
      const timeBeforeWait = context.currentTime; await new Promise(r => setTimeout(r, 70));
      const audioAdvanced = context.currentTime > timeBeforeWait;
      await context.close();
      return {scenario, energy, posts, events, error, elapsedMs, state, audioAdvanced, coalesced};
    }, scenario);
    assert.ok(result.energy > 0, `${scenario}: real audio rendered`);
    assert.equal(result.posts, 1);
    assert.equal(result.events.find(e=>e.type==='resetPosted').state, 'suspended');
    if (scenario === 'timeout' || scenario === 'error') {
      assert.ok(result.error); assert.equal(result.state, 'suspended'); assert.equal(result.audioAdvanced,false);
      assert.ok(!result.events.some(e=>e.type==='resumeRequested'));
      if(scenario==='timeout') { assert.match(result.error,/timed out/); assert.ok(result.elapsedMs>=1900); }
      else assert.match(result.error,/controlled reset failure/);
    } else {
      assert.equal(result.error,null);
      const ack = result.events.find(e=>e.type==='stateAck'); assert.ok(ack); assert.equal(ack.state,'suspended');
      assert.equal(ack.audioTime,result.events.find(e=>e.type==='resetPosted').audioTime,'no audio quantum during reset');
      assert.equal(result.state,scenario==='stopped'?'suspended':'running');
      assert.equal(result.audioAdvanced,scenario!=='stopped');
      if(scenario==='repeated') assert.equal(result.coalesced,true);
    }
    results.push(result); await page.close();
  }
  const hash = path => readFile(path).then(data=>createHash('sha256').update(data).digest('hex'));
  const report = {status:'PASS', checkoutSha:execFileSync('git',['rev-parse','HEAD'],{cwd:root,encoding:'utf8'}).trim(), browser:browser.version(), sampleRate:48000, wasmSha256:await hash(resolve(build,'hydra_dsp.wasm')), runtimeSha256:await hash(resolve(build,'hydra_dsp.js')), controllerSha256:await hash(resolve(root,'host/web/resetController.js')), processorSha256:await hash(resolve(root,'host/web/hydra-processor.js')), results};
  await writeFile(resolve(output,'result.json'),JSON.stringify(report,null,2));
  console.log(JSON.stringify({status:report.status,browser:report.browser,results:results.map(({scenario,elapsedMs,state})=>({scenario,elapsedMs,state}))},null,2));
} catch (error) {
  await writeFile(resolve(output,'result.json'),JSON.stringify({status:'FAIL',browser:browser.version(),results,error:error.stack},null,2));
  throw error;
} finally { await browser.close(); server.close(); }
