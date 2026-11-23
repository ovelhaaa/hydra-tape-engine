"""Reproducible Classic C / Studio calibration and listening package (synthetic sources).
Requires numpy/scipy and CMake target hydra_phase75a_classic_c. No normalization of outputs.
"""
from pathlib import Path
import shutil
import argparse, ctypes as C, hashlib, io, json, math, os, subprocess, tarfile, time
import numpy as np
from scipy.io import wavfile
from scipy.signal import resample_poly, correlate
ROOT=Path(__file__).resolve().parents[1]
BASE='c1f996b'
DLL_SEARCH = os.add_dll_directory(str(Path(shutil.which('g++')).parent)) if os.name=='nt' else None
F=C.POINTER(C.c_float)
class Engine:
 def __init__(self,library):
  self.lib=C.CDLL(str(library));l=self.lib
  l.hydra_dsp_create_v1_4.argtypes=[C.c_float,C.c_float,C.c_float,C.POINTER(C.c_void_p)]
  l.hydra_dsp_prepare.argtypes=[C.c_void_p,C.c_uint32,C.c_uint32]
  l.hydra_dsp_set_parameter.argtypes=[C.c_void_p,C.c_int,C.c_float]
  l.hydra_dsp_commit.argtypes=[C.c_void_p];l.hydra_dsp_reset.argtypes=[C.c_void_p]
  l.hydra_dsp_destroy.argtypes=[C.c_void_p]
  l.hydra_dsp_process.argtypes=[C.c_void_p,F,F,F,F,C.c_uint32]
 def render(self,x,sr,controls,events=(),benchmark=False):
  x=np.ascontiguousarray(x,dtype=np.float32)
  if x.ndim==1:x=np.column_stack([x,x])
  out=np.zeros_like(x);h=C.c_void_p()
  assert self.lib.hydra_dsp_create_v1_4(sr,2000,4000,C.byref(h))==0
  assert self.lib.hydra_dsp_prepare(h,128,2)==0
  def apply(p):
   for k,v in p.items():assert self.lib.hydra_dsp_set_parameter(h,int(k),v)==0
   assert self.lib.hydra_dsp_commit(h)==0
  apply(controls);self.lib.hydra_dsp_reset(h)
  schedule={int(n):p for n,p in events};elapsed=[]
  try:
   for n in range(0,len(x),128):
    if n in schedule:apply(schedule[n])
    a=np.ascontiguousarray(x[n:n+128,0]);b=np.ascontiguousarray(x[n:n+128,1]);l=np.empty_like(a);r=np.empty_like(a)
    start=time.perf_counter_ns()
    assert self.lib.hydra_dsp_process(h,a.ctypes.data_as(F),b.ctypes.data_as(F),l.ctypes.data_as(F),r.ctypes.data_as(F),len(a))==0
    elapsed.append((time.perf_counter_ns()-start)/1e6);out[n:n+len(a)]=np.column_stack([l,r])
  finally:self.lib.hydra_dsp_destroy(h)
  return (out,{'mean_ms':float(np.mean(elapsed[64:])), 'p99_ms':float(np.percentile(elapsed[64:],99)), 'max_ms':max(elapsed), 'includes_ctypes_overhead':True}) if benchmark else out

def sources(sr,seconds=1):
 n=int(sr*seconds);t=np.arange(n)/sr;rng=np.random.default_rng(7501)
 result={f'sine_{hz}':np.sin(2*np.pi*hz*t) for hz in [100,1000,8000]}
 # Frequency-domain pink noise, deterministic and peak calibrated with the sources.
 spectrum=np.fft.rfft(rng.normal(size=n));f=np.fft.rfftfreq(n,1/sr);spectrum[0]=0;spectrum[1:]/=np.sqrt(f[1:]);result['pink_noise']=np.fft.irfft(spectrum,n)
 impulse=np.zeros(n);impulse[0]=1;result['impulse']=impulse
 for name,notes,decay in [('guitar_clean',[196,246.94,293.66,392],4),('bass',[55,73.42,82.41],3),('electric_piano',[164.81,207.65,246.94],2.2),('synth_chords',[130.81,164.81,196],.7)]:
  y=np.zeros(n)
  for j,note in enumerate(notes):
   u=t-j*.12;env=np.where(u>=0,(1-np.exp(-np.maximum(u,0)*400))*np.exp(-np.maximum(u,0)*decay),0)
   y+=env*(np.sin(2*np.pi*note*u)+.3*np.sin(4*np.pi*note*u)+.12*np.sin(6*np.pi*note*u))
  result[name]=y
 result['guitar_transient']=np.exp(-t*40)*(np.sin(2*np.pi*220*t)+.4*rng.normal(size=n));result['guitar_transient'][0]=2
 result['guitar_driven']=np.tanh(4*result['guitar_clean'])
 return {k:(v/max(float(np.max(np.abs(v))),1e-12)).astype(np.float32) for k,v in result.items()}

def stats(x,y,sr,hz=None):
 x=np.asarray(x,dtype=np.float64);y=np.asarray(y,dtype=np.float64)
 if x.ndim==1:x=np.column_stack([x,x])
 rms=lambda a:float(np.sqrt(np.mean(a*a)))
 peak=float(np.max(np.abs(y)));inpeak=float(np.max(np.abs(x)));xr=rms(x);yr=rms(y)
 result={'rms':yr,'rms_difference':rms(y-x),'rms_gain_db':20*math.log10(max(yr,1e-30)/max(xr,1e-30)), 'sample_peak':peak,'peak_gain_db':20*math.log10(max(peak,1e-30)/max(inpeak,1e-30)), 'full_scale_samples':int(np.sum(np.abs(y)>=1)), 'uncontrolled_overs':int(np.sum(np.abs(y)>1)), 'non_finite':int(np.sum(~np.isfinite(y))), 'dc_mean':float(np.mean(y)), 'introduced_dc':float(np.mean(y-x)), 'max_abs_error':float(np.max(np.abs(y-x))), 'true_peak_4x':float(np.max(np.abs(resample_poly(y,4,1,axis=0))))}
 spectrum_x=np.abs(np.fft.rfft(x[:,0]));spectrum_y=np.abs(np.fft.rfft(y[:,0]));mask=spectrum_x>max(spectrum_x)*1e-5
 result.update(mono_fold_down_rms=rms(.5*(y[:,0]+y[:,1])),stereo_difference_rms=rms(y[:,0]-y[:,1]),
  stereo_correlation=float(np.sum(y[:,0]*y[:,1])/max(float(np.sqrt(np.sum(y[:,0]**2)*np.sum(y[:,1]**2))),1e-30)),
  spectral_magnitude_max_delta_db=float(np.max(np.abs(20*np.log10(np.maximum(spectrum_y[mask],1e-30)/spectrum_x[mask])))) if mask.any() else 0.,
  relative_latency_samples=0 if np.array_equal(x,y) else int(np.argmax(correlate(y[:,0],x[:,0],method='fft'))-(len(x)-1)),
  thd=None,phase_shift_radians=None)
 if hz:
  t=np.arange(len(y))/sr
  a=np.sum(x[:,0]*np.exp(-2j*np.pi*hz*t));b=np.sum(y[:,0]*np.exp(-2j*np.pi*hz*t))
  fundamental=2*abs(b)/len(y);harmonics=[]
  for h in range(2,int((sr/2)//hz)+1):
   if h>10:break
   harmonics.append(2*abs(np.sum(y[:,0]*np.exp(-2j*np.pi*hz*h*t)))/len(y))
  result.update(thd=float(np.linalg.norm(harmonics)/max(fundamental,1e-30)),phase_shift_radians=float(np.angle(b/a)),relative_latency_samples=0 if np.array_equal(x,y) else None)
  bins=np.abs(np.fft.rfft(x[:,0]));by=np.abs(np.fft.rfft(y[:,0]));mask=bins>max(bins)*1e-5
  result['spectral_magnitude_max_delta_db']=float(np.max(np.abs(20*np.log10(np.maximum(by[mask],1e-30)/bins[mask]))))
 return result

def baseline_library():
 work=ROOT/'build'/'phase75a-reference';work.mkdir(parents=True,exist_ok=True)
 archive=subprocess.check_output(['git','archive',BASE,'include','dsp'],cwd=ROOT)
 with tarfile.open(fileobj=io.BytesIO(archive)) as tf:tf.extractall(work,filter='data')
 dll=work/('baseline.dll' if os.name=='nt' else 'baseline.so')
 subprocess.run(['g++','-shared','-fPIC','-O3','-std=c++17','-ffp-contract=off','-DHYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING=1',f'-I{work/"include"}',f'-I{work/"dsp/core"}',str(work/'dsp/core/tape_core.cpp'),str(work/'dsp/core/hydra_dsp_c_api.cpp'),'-o',str(dll)],check=True)
 return dll

DEFAULT={11:1,12:240,13:55,14:50,15:50,16:7,4:0,2:0,3:35,8:10,38:0}
def main():
 parser=argparse.ArgumentParser();parser.add_argument('--library',type=Path,default=ROOT/'build-native'/('libhydra_phase75a_classic_c.dll' if os.name=='nt' else 'libhydra_phase75a_classic_c.so'));parser.add_argument('--out',type=Path,default=ROOT/'artifacts'/'hydra-phase75a1-studio-signal-path-listening');args=parser.parse_args()
 out=args.out.resolve();out.mkdir(parents=True,exist_ok=True);(out/'audio').mkdir(exist_ok=True)
 engine=Engine(args.library.resolve());baseline=Engine(baseline_library());report={'baseline':BASE,'decision':'HUMAN_LISTENING_PENDING','provenance':'Deterministic synthesized instrument approximations; not recordings or human listening evidence.','calibration':[],'classic_parity':[],'benchmarks':{},'scenarios':[]}
 # 132 dry calibration cases, 132 matching Classic cases, including near-full-scale transients.
 for sr in [44100,48000]:
  for name,source in sources(sr).items():
   for db in [-24,-18,-12,-6,-3,-.01]:
    x=source*10**(db/20);y=engine.render(x,sr,{**DEFAULT,47:1,48:0,25:1,23:1,24:1})
    m=stats(x,y,sr,int(name.split('_')[1]) if name.startswith('sine_') else None)
    assert m['max_abs_error']==0,(sr,name,db,m)
    classic=engine.render(x,sr,{**DEFAULT,14:0,15:0})
    mixed=engine.render(x,sr,{**DEFAULT,47:1,48:100})
    mixed_metrics=stats(x,mixed,sr,int(name.split('_')[1]) if name.startswith('sine_') else None)
    assert mixed_metrics['uncontrolled_overs']==0 and mixed_metrics['non_finite']==0
    report['calibration'].append({'studio_mix':mixed_metrics,'sr':sr,'source':name,'dbfs_peak':db,'studio_dry':m,'classic_dry':stats(x,classic,sr,int(name.split('_')[1]) if name.startswith('sine_') else None)})
 print('Dry calibration PASS',len(report['calibration']),flush=True)
 # Independent frozen-reference parity across modes, features, and monitoring automation.
 x=sources(48000)['guitar_clean']*.3
 for motion in range(4):
  for feature,extra in [('single',{16:4}),('multi',{}),('pingpong',{21:1}),('reverse',{23:1,24:1}),('warp',{30:1}),('spring',{25:1}),('freeze',{22:1}),('high_feedback',{13:95})]:
   p={**DEFAULT,38:motion,**extra};a=baseline.render(x,48000,p);b=engine.render(x,48000,p)
   exact=a.tobytes()==b.tobytes();assert exact,(motion,feature)
   report['classic_parity'].append({'motion':motion,'feature':feature,'bit_exact':exact,'sha256':hashlib.sha256(b.tobytes()).hexdigest()})
 print('Frozen Classic parity PASS',len(report['classic_parity']),flush=True)
 # Longer musical scenarios include 3 seconds of unnormalized tail.
 sr=48000;src=sources(sr,3);n=6*sr
 cases=[('clean_guitar','guitar_clean',{},[],[]),('driven_guitar','guitar_driven',{},[],[]),('bass','bass',{},[],[]),('electric_piano','electric_piano',{},[],[]),('strong_peaks','guitar_transient',{},[],[]),('wet_automation','guitar_clean',{},[],[(48000,{48:0}),(72064,{48:150}),(96000,{48:40})]),('switch_tail','guitar_clean',{},[],[(192000,{47:1})]),('high_feedback','guitar_clean',{13:92},[],[]),('texture_vs_clean','guitar_clean',{11:0},[],[]),('mono','synth_chords',{40:0,8:0},[],[])]
 for idx,(name,source,extra,ea,eb) in enumerate(cases):
  mono=np.zeros(n,dtype=np.float32);mono[:len(src[source])]=src[source]*(.97 if name=='strong_peaks' else .24)
  x=np.column_stack([mono,mono]);p={**DEFAULT,38:idx%4,**extra}
  a=engine.render(x,sr,{**p,47:0},ea);b=engine.render(x,sr,{**p,47:0 if name=='switch_tail' else 1},eb)
  assert np.isfinite(a).all() and np.isfinite(b).all()
  item={'id':name,'source':source,'motion':idx%4,'parameters':p,'events_a':ea,'events_b':eb,'studio_initial_mode':0 if name=='switch_tail' else 1,'a':f'audio/{name}-classic.wav','b':f'audio/{name}-studio.wav','input':f'audio/{name}-input.wav','metrics_a':stats(x,a,sr),'metrics_b':stats(x,b,sr),'tail_rms_a':float(np.sqrt(np.mean(a[3*sr:]**2))),'tail_rms_b':float(np.sqrt(np.mean(b[3*sr:]**2)))}
  for suffix,data in [('classic',a),('studio',b),('input',x)]:wavfile.write(out/'audio'/f'{name}-{suffix}.wav',sr,data.astype(np.float32))
  # Raw interleaved buffers permit independent WASM verification with identical inputs.
  x.astype('<f4').tofile(out/'audio'/f'{name}-input.f32');b.astype('<f4').tofile(out/'audio'/f'{name}-studio.f32')
  item['tail_gain_studio_vs_classic_db']=20*math.log10(max(item['tail_rms_b'],1e-30)/max(item['tail_rms_a'],1e-30))
  report['scenarios'].append(item)
 # Static mix-law audition: identical stem, fixed coefficients, no per-file loudness matching.
 dry=np.column_stack([src['guitar_clean']*.2]*2);wet=engine.render(dry,sr,{**DEFAULT,47:1})-dry
 for name,data in [('linear',.5*dry+.5*wet),('equal_power',math.sqrt(.5)*dry+math.sqrt(.5)*wet)]:wavfile.write(out/'audio'/f'mix-law-{name}.wav',sr,data.astype(np.float32))
 bench=np.tile(x[:48000],(4,1))
 for label,e,p in [('baseline_classic',baseline,DEFAULT),('classic',engine,{**DEFAULT,47:0}),('studio',engine,{**DEFAULT,47:1})]:
  runs=[e.render(bench,sr,p,benchmark=True)[1] for _ in range(3)]
  report['benchmarks'][label]=runs
 (out/'metrics.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
 (out/'README-WINDOWS.txt').write_text('HUMAN_LISTENING_PENDING\nAll instrument sources are synthetic approximations. No output loudness normalization.\nRun: python -m http.server 8075 --bind 127.0.0.1 --directory .\nOpen http://localhost:8075 ; use headphones and a comfortable level.\nA/B uses two predecoded buffers on one AudioContext clock; blind assignment stays hidden until Reveal.\nCompare clarity, pick attack, bass phase, tape tails, switching at 4 s and overload on strong peaks.\nWet automation at 1/1.501/2 s. All WAVs are float32 48 kHz stereo. Metrics include sample and 4x true peaks.\nMix-law audition is supplemental: fixed .5/.5 versus sqrt(.5)/sqrt(.5), inherently different energy.\nNo decision based on synthetic sources alone: repeat with live guitar and real instrument recordings.\n',encoding='utf-8')
 # Shared audio clock, sample-aligned sources, short gain crossfades, blind labels.
 html='''<!doctype html><meta charset="utf-8"><title>Hydra Studio A/B</title><style>body{font:18px system-ui;background:#131b1d;color:#eee;max-width:920px;margin:40px auto;padding:20px}button,select{font:inherit;margin:8px;padding:12px}pre{white-space:pre-wrap}</style><h1>Hydra — Classic / Studio</h1><p>HUMAN_LISTENING_PENDING · Synthetic instrument approximations · No independent normalization</p><select id="scene"></select><p><button id="play">Play / restart synchronized</button><button id="stop">Stop</button><button id="a">A</button><button id="b">B</button><button id="blind">New blind trial</button><button id="reveal">Reveal</button></p><p id="state">Select a scenario, then Play.</p><input id="seek" aria-label="Seek seconds" type="range" min="0" max="6" value="0" step=".01"><pre id="metrics"></pre><p><a href="metrics.json">Metrics</a> · <a href="manifest.json">Manifest / hashes</a> · <a href="audio/mix-law-linear.wav">Linear mix law</a> · <a href="audio/mix-law-equal_power.wav">Equal-power mix law</a></p><script type="module">
const report=await fetch('metrics.json').then(r=>r.json());const scenes=report.scenarios;const scene=document.querySelector('#scene');for(const [i,s] of scenes.entries())scene.add(new Option(s.id,i));let ctx,nodes=[],gains=[],started=0,offset=0,blind=false,swap=false,selected=0;const state=document.querySelector('#state');
function stop(){for(const n of nodes){try{n.stop()}catch{}}nodes=[]}
function select(i){selected=i;for(let j=0;j<gains.length;j++){const p=gains[j].gain;p.cancelScheduledValues(ctx.currentTime);p.setValueAtTime(p.value,ctx.currentTime);p.linearRampToValueAtTime(j===(i^(swap?1:0))?1:0,ctx.currentTime+.01)}state.textContent=blind?`Blind ${i?'B':'A'} — identity hidden`:`${i?'B':'A'}: ${(i^(swap?1:0))?'Studio':'Classic'}`}
async function play(){stop();ctx??=new AudioContext();await ctx.resume();const s=scenes[scene.value];const buffers=await Promise.all([s.a,s.b].map(async url=>ctx.decodeAudioData(await (await fetch(url)).arrayBuffer())));offset=Number(document.querySelector('#seek').value);started=ctx.currentTime+.08;gains=[];for(const buffer of buffers){const n=ctx.createBufferSource(),g=ctx.createGain();n.buffer=buffer;g.gain.value=0;n.connect(g).connect(ctx.destination);n.start(started,Math.min(offset,buffer.duration-.01));nodes.push(n);gains.push(g)}select(selected);document.querySelector('#metrics').textContent=JSON.stringify({scenario:s.id,motion:s.motion,classic:s.metrics_a,studio:s.metrics_b,tail_classic:s.tail_rms_a,tail_studio:s.tail_rms_b},null,2)}
document.querySelector('#play').onclick=play;document.querySelector('#stop').onclick=stop;document.querySelector('#a').onclick=()=>select(0);document.querySelector('#b').onclick=()=>select(1);document.querySelector('#blind').onclick=()=>{document.querySelector('#metrics').hidden=true;blind=true;swap=crypto.getRandomValues(new Uint8Array(1))[0]%2===1;select(selected)};document.querySelector('#reveal').onclick=()=>{document.querySelector('#metrics').hidden=false;blind=false;select(selected)};scene.onchange=()=>{stop();document.querySelector('#seek').value=0};document.querySelector('#seek').onchange=play;
</script>'''
 (out/'index.html').write_text(html,encoding='utf-8')
 manifest={'name':'hydra-phase75a1-studio-signal-path-listening','commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),'dirty':bool(subprocess.check_output(['git','diff','--name-only'],cwd=ROOT,text=True).strip()),'baseline':BASE,'decision':'HUMAN_LISTENING_PENDING','sample_rate':sr,'normalization':'none (source peak calibration only)','files':{str(p.relative_to(out)).replace('\\','/'):hashlib.sha256(p.read_bytes()).hexdigest() for p in out.rglob('*') if p.is_file() and p.name!='manifest.json'}}
 (out/'manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8');print('Listening package:',out,flush=True)
if __name__=='__main__':main()
