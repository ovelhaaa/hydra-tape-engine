import {test} from 'node:test';
import assert from 'node:assert/strict';
import {Audition, importReviews, exportReviews, verifiedFile, parseTelemetry} from '../engine.mjs';
function context() {
  return {currentTime:0,destination:{},resume:async()=>{},createGain:()=>({gain:{value:0,cancelAndHoldAtTime(){},linearRampToValueAtTime(){}},connect(){}}),createBufferSource:()=>({start(t,p){this.started=[t,p];},stop(){this.stopped=true;},connect(){},disconnect(){}})};
}
test('A/B retains sources and position; seek shares start/offset; loop wraps; scenario stops sources',async()=>{
  const c=context(), p=new Audition(c), buffers=[{length:480000,sampleRate:48000,duration:10},{length:480000,sampleRate:48000,duration:10}];
  p.load(buffers); assert.equal(p.playing,false); await p.play(); c.currentTime=2;
  const sources=[...p.sources], before=p.position; p.select(1); assert.equal(p.position,before); assert.deepEqual(p.sources,sources);
  assert.deepEqual(sources[0].started,sources[1].started);
  await p.seek(4); assert.ok(sources.every(s=>s.stopped)); assert.deepEqual(p.sources[0].started,p.sources[1].started); assert.equal(p.position,4);
  await p.setLoop(true,3,5); c.currentTime+=3; assert.ok(p.position>=3 && p.position<5);
  const previous=[...p.sources]; p.load(buffers); assert.ok(previous.every(s=>s.stopped)); assert.equal(p.playing,false); assert.equal(p.position,0);
});
test('a pending resume cannot start a newly selected scenario',async()=>{
 const c=context(); let resume; c.resume=()=>new Promise(r=>resume=r); const p=new Audition(c), b={length:10,sampleRate:10,duration:1}; p.load([b,b]); const pending=p.play(); p.load([b,b]); resume(); await pending; assert.equal(p.playing,false);
});
test('review JSON roundtrip is independent of storage and validates provenance',()=>{
 const m={reference_commit:'a'.repeat(40),scenarios:{one:{duration_sec:10}}}, r={one:{state:'NEEDS ATTENTION',comments:'Listen again',criteria:{Clicks:'FAIL'},notes:[{time:2.5,description:'Transient',severity:'medium'}]}};
 assert.deepEqual(importReviews(exportReviews(m,r),m),r); assert.throws(()=>importReviews(exportReviews(m,r),{...m,reference_commit:'b'.repeat(40)}));
 assert.throws(()=>importReviews(exportReviews(m,{one:{...r.one,state:'APPROVED'}}),m));
});
test('missing, empty and corrupt files fail clearly; CSV parsing rejects invalid data',async()=>{
 await assert.rejects(verifiedFile(null,'0'.repeat(64)),/Missing/);
 await assert.rejects(verifiedFile(new File(['wrong'],'audio.wav'),'0'.repeat(64)),/SHA-256/);
 assert.equal(parseTelemetry('time_sec,tempo_source\n0,0\n1,2')[1].tempo_source,2);
 assert.throws(()=>parseTelemetry('bad\nvalue')); assert.throws(()=>parseTelemetry('time_sec\n1\n0'));
});
