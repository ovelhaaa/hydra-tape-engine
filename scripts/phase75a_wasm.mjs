import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import { performance } from 'node:perf_hooks';
const root=process.cwd();
const web=path.resolve(process.env.HYDRA_WEB_BUILD || 'build-web/web');
const lab=path.resolve(process.env.HYDRA_STUDIO_LAB || 'artifacts/hydra-phase75a1-studio-signal-path-listening');
const factory=(await import(pathToFileURL(path.join(web,'hydra_dsp.js')))).default;
const mod=await factory({wasmBinary:await fs.readFile(path.join(web,'hydra_dsp.wasm'))});
const wrap=(name,args)=>mod.cwrap(`hydra_dsp_${name}`,'number',args.map(()=> 'number'));
const api={create:wrap('create_v1_4',[0,0,0,0]),prepare:wrap('prepare',[0,0,0]),set:wrap('set_parameter',[0,0,0]),commit:wrap('commit',[0]),reset:wrap('reset',[0]),destroy:wrap('destroy',[0]),process:wrap('process',[0,0,0,0,0,0])};
function render(input,sr,controls,events=[]){
 const hp=mod._malloc(4);assert.equal(api.create(sr,2000,4000,hp),0);const h=mod.HEAPU32[hp>>2];mod._free(hp);assert.equal(api.prepare(h,128,2),0);
 const apply=p=>{for(const [id,value] of Object.entries(p))assert.equal(api.set(h,Number(id),value),0);assert.equal(api.commit(h),0)};
 apply(controls);api.reset(h);
 const ptrs=[0,0,0,0].map(()=>mod._malloc(512));const [il,ir,ol,or]=ptrs;const result=new Float32Array(input.length);const schedule=new Map(events.map(([n,p])=>[n,p]));const times=[];
 try{for(let start=0;start<input.length/2;start+=128){if(schedule.has(start))apply(schedule.get(start));const n=Math.min(128,input.length/2-start);for(let j=0;j<n;j++){mod.HEAPF32[(il>>2)+j]=input[2*(start+j)];mod.HEAPF32[(ir>>2)+j]=input[2*(start+j)+1]}const t=performance.now();assert.equal(api.process(h,il,ir,ol,or,n),0);times.push(performance.now()-t);for(let j=0;j<n;j++){result[2*(start+j)]=mod.HEAPF32[(ol>>2)+j];result[2*(start+j)+1]=mod.HEAPF32[(or>>2)+j]}}}finally{ptrs.forEach(p=>mod._free(p));api.destroy(h)}
 const sorted=times.slice(100).sort((a,b)=>a-b);return {result,meanMs:sorted.reduce((a,b)=>a+b,0)/sorted.length,p99Ms:sorted[Math.floor(sorted.length*.99)],maxMs:Math.max(...times)};
}
const report={dry:[],nativeWasm:[],benchmarks:[]};
for(const sr of [44100,48000])for(let motion=0;motion<4;motion++){
 const input=new Float32Array(sr*2);for(let i=0;i<sr;i++){const x=Math.fround(.998*Math.sin(i*2*Math.PI*1000/sr));input[2*i]=x;input[2*i+1]=-x}
 const {result}=render(input,sr,{47:1,48:0,38:motion,11:1,23:1,24:1,25:1,13:95});
 assert.ok(result.every((v,i)=>v===input[i]),'Studio dry must preserve sample values below full scale');report.dry.push({sr,motion,numericallyExact:true,bitExact:Buffer.from(result.buffer).equals(Buffer.from(input.buffer)),note:'Signed zero may change through floating-point addition; no general bit-exact dry claim.'});
}
const manifest=JSON.parse(await fs.readFile(path.join(lab,'metrics.json'),'utf8'));
for(const s of manifest.scenarios){
 const buf=await fs.readFile(path.join(lab,`audio/${s.id}-input.f32`));const input=new Float32Array(buf.buffer,buf.byteOffset,buf.byteLength/4);
 const refbuf=await fs.readFile(path.join(lab,`audio/${s.id}-studio.f32`));const ref=new Float32Array(refbuf.buffer,refbuf.byteOffset,refbuf.byteLength/4);
 const {result,...bench}=render(input,48000,{...s.parameters,47:s.studio_initial_mode},s.events_b);
 let maxError=0,energy=0;for(let i=0;i<ref.length;i++){assert.ok(Number.isFinite(result[i]));const d=result[i]-ref[i];maxError=Math.max(maxError,Math.abs(d));energy+=d*d}
 // All input float samples are identical; allow only sub-microfloat implementation drift.
 assert.ok(maxError<1e-6,`${s.id}: Native/WASM max error ${maxError}`);
 report.nativeWasm.push({scenario:s.id,maxError,rmsError:Math.sqrt(energy/ref.length),bitExact:Buffer.from(result.buffer).equals(refbuf)});
}
const stimulus=new Float32Array(48000*8);for(let i=0;i<stimulus.length/2;i++)stimulus[2*i]=stimulus[2*i+1]=.15*Math.sin(i*.03);
for(let mode=0;mode<2;mode++)for(let motion=0;motion<4;motion++){
 const {result,...bench}=render(stimulus,48000,{11:1,16:7,13:85,47:mode,38:motion});report.benchmarks.push({mode,motion,...bench,budgetMs:128/48});assert.ok(bench.meanMs<128/48);
}
await fs.writeFile(path.join(lab,'wasm-validation.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));
