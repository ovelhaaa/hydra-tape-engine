import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
import {DEFAULT_CONTROL_STATE,serializePreset,deserializePresetFromText} from '../presetSerialization.js';

test('Studio presets round-trip and old presets retain Classic default',()=>{
 const state={...DEFAULT_CONTROL_STATE,signalPathMode:1,studioWetLevel:73};
 assert.deepEqual(deserializePresetFromText(serializePreset(state)).controlState,state);
 const old=deserializePresetFromText(JSON.stringify({version:2,name:'old',engineParams:{dryWet:75,delayWet:42}})).controlState;
 assert.equal(old.signalPathMode??DEFAULT_CONTROL_STATE.signalPathMode,0);assert.equal(old.dryWet,75);assert.equal(old.delayWet,42);
});

test('AudioWorklet confirms signal path mode and routes Wet Level into real WASM', {skip:!process.env.HYDRA_WEB_BUILD},async()=>{
 let Processor;const messages=[];globalThis.sampleRate=48000;globalThis.currentFrame=0;
 globalThis.AudioWorkletProcessor=class{constructor(){this.port={postMessage:m=>messages.push(m),onmessage:null}}};
 globalThis.registerProcessor=(_,cls)=>{Processor=cls};
 const web=path.resolve(process.env.HYDRA_WEB_BUILD);
 await import(pathToFileURL(path.join(web,'hydra-processor.js')).href+'?studio='+Date.now());
 const data=await fs.readFile(path.join(web,'hydra_dsp.wasm'));
 const node=new Processor({processorOptions:{wasmBinary:data.buffer.slice(data.byteOffset,data.byteOffset+data.byteLength)}});
 try{
  for(let i=0;!node.ready&&i<500;i++)await new Promise(r=>setTimeout(r,10));assert.equal(node.ready,true);
  node.port.onmessage({data:{type:'command',command:'signalPathMode',value:1}});
  assert.ok(messages.some(m=>m.type==='stateAck'&&m.key==='signalPathMode'&&m.value===1));
  const l=new Float32Array(128),r=new Float32Array(128),ol=new Float32Array(128),or=new Float32Array(128);
  const params={p_48:new Float32Array([0])};
  for(let block=0;block<60;block++){for(let i=0;i<128;i++){l[i]=Math.fround(.99*Math.sin((block*128+i)*.01));r[i]=-l[i]}node.process([[l,r]],[[ol,or]],params)}
  assert.deepEqual(ol,l);assert.deepEqual(or,r);
  for(let motion=0;motion<4;motion++){
   node.port.onmessage({data:{type:'command',command:'motionMode',value:motion}});
   node.process([[l,r]],[[ol,or]],params);assert.deepEqual(ol,l);assert.deepEqual(or,r);
  }
  node.port.onmessage({data:{type:'command',command:'signalPathMode',value:0}});
  assert.ok(messages.some(m=>m.type==='stateAck'&&m.key==='signalPathMode'&&m.value===0));
 }finally{node.cleanup()}
});
