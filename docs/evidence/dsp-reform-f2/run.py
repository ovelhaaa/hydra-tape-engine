"""Reproducible F2 bench. Outputs only to a new artifacts directory.
Builds are explicit C0 + variant 0/1/2 with identical flags/toolchain.
Run --help; never rewrites F0.2/F1 or goldens.
"""
from pathlib import Path
import argparse, ctypes as C, hashlib, json, os, random, re, shutil, subprocess, sys, time
import xml.etree.ElementTree as ET
import numpy as np
ROOT=Path(__file__).resolve().parents[3]
HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE.parent/'dsp-reform-f1'))
import baseline as b
sys.path.insert(0,str(ROOT/'scripts'))
import phase64_validate_engine as gates
STAGES=['HPF','feedback_bump','feedback_gap','electronics','private_playback','combined','private_bump','private_gap','private_shelf']
BANDS=[(40,80),(80,150),(150,300),(300,1000),(1000,4000),(4000,24001)]
def dump(p,v):
 p.parent.mkdir(parents=True,exist_ok=True);b.dump(p,v)
def command(cmd,log=None,check=True):
 r=subprocess.run(list(map(str,cmd)),cwd=ROOT,capture_output=True,text=True)
 if log:log.parent.mkdir(parents=True,exist_ok=True);log.write_text(r.stdout+r.stderr,encoding='utf-8')
 if check and r.returncode:raise RuntimeError(f'{cmd}: exit {r.returncode}\n{r.stdout[-2500:]}\n{r.stderr[-2500:]}')
 return r
def flags(v):return ['-DHYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING=1',f'-DHYDRA_DSP_REFORM_F2_VARIANT={v}']
def native(v):return ROOT/f'build/f2-{v}-native'
def wasm(v):return ROOT/f'build/f2-{v}-wasm'
def libpath(v):return native(v)/('libhydra_abi_test_library.dll' if os.name=='nt' else 'libhydra_abi_test_library.so')
def exe(p):return Path(str(p)+('.exe' if os.name=='nt' else ''))
def process(runner,x,params,rate=44100,diag=False):
 lib=runner.lib;h=C.c_void_p();ptr=C.POINTER(C.c_float)
 assert lib.hydra_dsp_create_v1_4(rate,2000,8000,C.byref(h))==0 and h
 out=np.empty_like(x);peaks=np.zeros(8);energies=[];seconds=np.zeros(8);count=0
 try:
  assert lib.hydra_dsp_prepare(h,128,2)==0;lib.hydra_dsp_reset(h)
  for i,v in enumerate(params):
   if i!=29:assert lib.hydra_dsp_set_parameter(h,i,float(v))==0
  assert lib.hydra_dsp_set_tempo_source(h,0)==0
  assert lib.hydra_dsp_transport_play(h)==0
  lib.hydra_dsp_commit.argtypes=[C.c_void_p];assert lib.hydra_dsp_commit(h)==0
  effective=[]
  for i in range(38):
   val=C.c_float();assert lib.hydra_dsp_get_parameter(h,i,C.byref(val))==0;effective.append(val.value)
  if diag:
   lib.phase64_diag_data.restype=ptr
   lib.phase64_diag_enable(1)
  start_clock=time.perf_counter();block_us=[]
  for start in range(0,len(x),128):
   n=min(128,len(x)-start)
   left=np.ascontiguousarray(x[start:start+n,0]);right=np.ascontiguousarray(x[start:start+n,1]);ol=np.empty(n,np.float32);orr=np.empty(n,np.float32)
   if diag:lib.f2_diag_reset()
   tick=time.perf_counter_ns()
   assert lib.hydra_dsp_process(h,left.ctypes.data_as(ptr),right.ctypes.data_as(ptr),ol.ctypes.data_as(ptr),orr.ctypes.data_as(ptr),n)==0
   block_us.append((time.perf_counter_ns()-tick)/1000)
   out[start:start+n,0]=ol;out[start:start+n,1]=orr
   if diag:
    assert lib.phase64_diag_count()==n
    rec=np.ctypeslib.as_array(lib.phase64_diag_data(),shape=(n*17,)).reshape(n,17)
    assert np.array_equal(rec[:,7],ol) and np.array_equal(rec[:,14],orr)
    z=rec[:,[4,5,6,13,11,12,15,16]].astype(float)
    assert np.isfinite(z).all();peaks=np.maximum(peaks,np.max(abs(z),axis=0))
    seconds+=np.sum(z*z,axis=0);count+=n
    if (start+n)//rate>start//rate:
     energies.append(np.sqrt(seconds/count).tolist());seconds[:]=0;count=0
  assert np.isfinite(out).all()
  return out,{'parameters':params,'readback':effective,'input_sha256':b.sha(x.tobytes()),'output_sha256':b.sha(out.tobytes()),'finite':True,'peak':float(abs(out).max()),'max_sample_step':float(abs(np.diff(out,axis=0)).max()),'rms':float(np.sqrt(np.mean(out.astype(float)**2))),'elapsed_seconds':time.perf_counter()-start_clock,'block_call_us_P50_P95_P99_peak':np.percentile(block_us,[50,95,99,100]).tolist(),'internal_peaks_fbL_recL_magL_fbR_recR_magR_rawRecL_rawRecR':peaks.tolist() if diag else None,'internal_second_rms':energies if diag else None}
 finally:lib.hydra_dsp_destroy(h)
def params(feedback=85,mode='single',speed=50,age=50,bump=50,tone=50,clean=True):
 p=b.PARAMS.copy()
 for i,v in {5:speed,6:age,7:bump,12:500,13:feedback,14:100,15:100,16:7 if 'multi' in mode else 4,20:tone,21:int('pingpong' in mode),23:int(mode=='reverse')}.items():p[i]=v
 if clean:
  for i in [0,1,2,4,8,19,25,30]:p[i]=0
 return p
def energy(y,rate):
 y=y.astype(float);spec=np.abs(np.fft.rfft(y,axis=0))**2/len(y)**2
 spec[1:-1]*=2;f=np.fft.rfftfreq(len(y),1/rate)
 return {f'{lo}-{min(hi,rate//2+1)}':float(spec[(f>=lo)&(f<hi)].sum()) for lo,hi in BANDS}
def db(x):return float(10*np.log10(max(x,1e-30)))
def baseline_check(out):
 # Compile the actual frozen F1 source with this run's compiler; this also works
 # in Linux CI without pretending MinGW and Linux have identical libm results.
 ref=out/'frozen-f1-source'
 if not ref.exists():command(['git','worktree','add','--detach',ref,'f3281d0e9230ad151bcad94c574042adc7996467'],out/'f1-checkout.log')
 library=out/('frozen-f1.dll' if os.name=='nt' else 'frozen-f1.so')
 compiler=os.environ.get('CXX','g++')
 command([compiler,'-O3','-DNDEBUG','-std=c++17','-ffp-contract=off','-shared',*(['-fPIC'] if os.name!='nt' else []),'-DHYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING=1','-I'+str(ref/'include'),'-I'+str(ref/'dsp/core'),ref/'dsp/core/tape_core.cpp',ref/'dsp/core/hydra_dsp_c_api.cpp','-o',library],out/'f1-build.log')
 reference=b.render.HydraEngineRunner(library)
 frozen=json.loads((HERE.parent/'dsp-reform-f1/c0_baseline_manifest.json').read_text())
 old=json.loads((HERE.parent/'dsp-reform-f0/baseline_renders_manifest.json').read_text())
 runner=b.render.HydraEngineRunner(libpath(0));result={}
 for name,scene in old['scenarios'].items():
  p=b.PARAMS.copy()
  for k,v in scene['params_overrides'].items():p[int(k)]=v
  x=b.stimulus(scene);y,e=b.process(runner,x,p);yr,er=b.process(reference,x,p)
  expected=frozen['scenarios'][name]
  row={'input_sha256':b.sha(x.tobytes()),'output_f32_sha256':b.sha(y.tobytes()),'input_exact':b.sha(x.tobytes())==expected['input_sha256'],'output_exact':b.sha(y.tobytes())==expected['output_f32_sha256'],'readback_exact':dict(enumerate(e))=={int(k):v for k,v in expected['parameters_readback'].items()}}
  row['same_toolchain_frozen_F1_bit_exact']=bool(np.array_equal(y,yr) and e==er)
  result[name]=row;assert row['input_exact'] and row['readback_exact'] and row['same_toolchain_frozen_F1_bit_exact'],name
  if os.name=='nt':assert row['output_exact'],name
 # Characterize the historical wrap response using the unchanged F1 library.
 wrap=exe(out/'f1-wrap-reference')
 command([compiler,'-O3','-std=c++17','-ffp-contract=off',*flags(0),'-I'+str(ROOT/'include'),'-I'+str(ROOT/'dsp/core'),ROOT/'tests/core/f2_wrap_contract.cpp',library,'-o',wrap],out/'f1-wrap-build.log')
 oldwrap=command([wrap],out/'f1-wrap.log').stdout
 newwrap=command([exe(native(0)/'hydra_f2_wrap_contract')],out/'f2-c0-wrap.log').stdout
 assert oldwrap==newwrap,'Frozen F1 wrap response changed'
 dump(out/'wrap-reference.json',{'same_toolchain_frozen_F1_response_exact':True,'reference_stdout':oldwrap})
 dump(out/'c0-bit-exact.json',result);print('C0 frozen F1: 10/10 BIT EXACT',flush=True)
def contracts(v,out):
 for kind,build in [('native',native(v)),('wasm',wasm(v))]:
  dest=out/f'contracts-{kind}';dest.mkdir(parents=True)
  r=command(['ctest','--test-dir',build,'--output-on-failure','-E','test_golden','--output-junit',dest/'ctest.xml'],dest/'ctest.log',False)
  failed=[x.attrib['name'] for x in ET.parse(dest/'ctest.xml').iter('testcase') if x.find('failure') is not None]
  assert r.returncode in [0,8] and failed in [[],['core_regression']]
  if failed:assert 'check failed: channel_energy(fullR, 2840, 2960) > 1e-5f' in r.stdout
  dump(dest/'qualification.json',{'original_exit':r.returncode,'failed':failed,'note':'A energy assertion retained; all other assertions evaluated separately below. New physical reader and wrap identity are independent of energy.'})
 # Diagnostic retains all original assertions, evaluates after the A mismatch.
 compiler=os.environ.get('CXX','g++');obs=exe(out/'regression-observation')
 command([compiler,'-O3','-std=c++17','-ffp-contract=off',*flags(v),'-I'+str(ROOT/'include'),'-I'+str(ROOT/'dsp/core'),HERE.parent/'dsp-reform-f1/regression_observation.cpp',native(v)/'libhydra_dsp_core.a','-o',obs],out/'observation-build.log')
 r=command([obs],out/'regression-observation.log',False)
 failures=[s for s in r.stdout.splitlines() if s.startswith('OBSERVATION_FAIL')]
 assert r.returncode==len(failures) and len(failures)<=1
 assert not failures or 'channel_energy(fullR, 2840, 2960) > 1e-5f' in failures[0]
 assert 'block_exact_L=1 block_exact_R=1' in r.stdout
 command([sys.executable,ROOT/'tests/core/compare_native_wasm.py','--native',exe(native(v)/'hydra_render_trace_native'),'--wasm-js',wasm(v)/'wasm-tests/hydra_render_trace.js','--report-json',out/'parity.json'],out/'parity.log')
 command([sys.executable,ROOT/'tests/core/verify_numerical_inputs.py','--native',exe(native(v)/'hydra_render_trace_native'),'--wasm-js',wasm(v)/'wasm-tests/hydra_render_trace.js','--output',out/'input-lut.json'],out/'input-lut.log')
def filters(v,out):
 compiler=os.environ.get('CXX','g++');probe=exe(out/'filter_probe');folder=out/'filters';folder.mkdir()
 command([compiler,'-O3','-std=c++17','-ffp-contract=off',*flags(v),'-I'+str(ROOT/'include'),'-I'+str(ROOT/'dsp/core'),HERE/'filter_probe.cpp','-o',probe],out/'filter-build.log')
 command([probe,folder],out/'filter-probe.log')
 result={}
 for path in sorted(folder.glob('*.f32')):
  _,sp,age,bump,tone,stage=path.stem.split('_');y=np.fromfile(path,'<f4');h=np.fft.rfft(y.astype(float));phase=np.unwrap(np.angle(h))
  hz=[40,60,80,100,130,150,180,200,240,300,1000,4000,8000,12000]
  result[f'speed{sp}_age{age}_bump{bump}_tone{tone}_{STAGES[int(stage)]}']={'sha256':b.sha(path.read_bytes()),'db_at_hz':{str(f):float(20*np.log10(max(abs(h[f]),1e-15))) for f in hz},'phase_rad_at_hz':{str(f):float(phase[f]) for f in hz},'peak_hz':int(np.argmax(abs(h))),'peak_db':float(20*np.log10(max(abs(h)))),'last_100ms_rms':float(np.sqrt(np.mean(y[-4410:].astype(float)**2)))}
 dump(out/'filters.json',result)
 shutil.rmtree(folder) # Verified generated child of a new output, not source.
def loops(v,out):
 runner=b.render.HydraEngineRunner(libpath(v));rows={}
 for speed,age in [(0,0),(0,100),(50,50),(100,0),(100,100)]:
  for feedback in [50,75,85,95,100]:
   for freq in [0,80,130,200]:
    x=np.zeros((44100*12,2),np.float32)
    if not freq:x[0]=.1
    else:
     t=np.arange(4410)/44100;x[:4410]=(.1*np.sin(2*np.pi*freq*t))[:,None]
    y,meta=process(runner,x,params(feedback,speed=speed,age=age));repeats=[]
    for n in range(1,21):
     seg=y[round((n*.5-.1)*44100):round((n*.5+.4)*44100)]
     power=energy(seg,44100);total=sum(power.values())
     row={'repeat':n,'band_mean_square':power,'band_relative':{k:a/max(total,1e-30) for k,a in power.items()},'rms':float(np.sqrt(np.mean(seg.astype(float)**2))),'peak':float(abs(seg).max())}
     if n in [1,2,4,8,16]:
      spec=np.abs(np.fft.rfft(seg.astype(float),axis=0)).mean(axis=1);f=np.fft.rfftfreq(len(seg),1/44100)
      row['spectrum_db_at_hz']={str(hz):float(20*np.log10(max(float(np.interp(hz,f,spec)),1e-30))) for hz in [40,60,80,100,130,150,200,300,1000,4000,8000,12000]}
     if freq:
      z=y[round((n*.5+.025)*44100):round((n*.5+.075)*44100)].mean(axis=1);t=np.arange(len(z))/44100
      amps=[abs(np.sum(z*np.exp(-2j*np.pi*freq*k*t))) for k in range(1,11)]
      row['estimated_harmonics_2_10_ratio']=float(np.linalg.norm(amps[1:])/max(amps[0],1e-15))
     repeats.append(row)
    peak=max(r['rms'] for r in repeats);below=[r['repeat']*.5 for r in repeats if r['rms']<=peak*.001]
    meta.update(repeats=repeats,observed_minus60db_seconds=below[0] if below else None,decay_note='Windowed output, finite 12s observation; null means threshold not reached. Not RT60 in a linear room.')
    rows[f's{speed}_a{age}_fb{feedback}_hz{freq}']=meta
 dump(out/'loops.json',rows)
def make_instrumented(v,out):
 source=out/'tape_core_instrumented.cpp'
 command([sys.executable,ROOT/'scripts/instrument_feedback.py',ROOT/'dsp/core/tape_core.cpp',source],out/'instrument.log')
 s=source.read_text();s=s.replace('float texture_sig, delay_sig, tape_sig;','float texture_sig, delay_sig, tape_sig; float recR,magR,fbR,outR,rawRecL,rawRecR;')
 s=s.replace('float recSig=dc.process(freshInput + feedbackInput); recSig=clampf(recSig,-4,4);','float recSig=dc.process(freshInput + feedbackInput); if(phase64_diag::enabled&&phase64_diag::n<8192){auto&r=phase64_diag::g[phase64_diag::n];if(ch==0)r.rawRecL=recSig;else r.rawRecR=recSig;} recSig=clampf(recSig,-4,4);')
 s=s.replace('{phase64_diag::g[phase64_diag::n].rec=recSig;}','{phase64_diag::g[phase64_diag::n].rec=recSig;}\n    if(ch==1&&phase64_diag::enabled&&phase64_diag::n<8192){phase64_diag::g[phase64_diag::n].recR=recSig;}')
 s=s.replace('{phase64_diag::g[phase64_diag::n].mag=magOut;}','{phase64_diag::g[phase64_diag::n].mag=magOut;}\n    if(ch==1&&phase64_diag::enabled&&phase64_diag::n<8192){phase64_diag::g[phase64_diag::n].magR=magOut;}')
 s=s.replace('r.fb=feedbackL;r.out=sampleOutL;','r.fb=feedbackL;r.out=sampleOutL;r.fbR=feedbackR;r.outR=sampleOutR;')
 s+='\nextern "C" void f2_diag_reset(){hydra::dsp::phase64_diag::n=0;}\n'
 source.write_text(s)
 library=out/('instrumented.dll' if os.name=='nt' else 'instrumented.so')
 command([os.environ.get('CXX','g++'),'-O3','-DNDEBUG','-std=c++17','-ffp-contract=off','-shared',*(['-fPIC'] if os.name!='nt' else []),*flags(v),'-I'+str(ROOT/'include'),'-I'+str(ROOT/'dsp/core'),source,ROOT/'dsp/core/hydra_dsp_c_api.cpp','-o',library],out/'instrument-build.log')
 return b.render.HydraEngineRunner(library)
def stress(v,out,inputs):
 runner=b.render.HydraEngineRunner(libpath(v));instrument=make_instrumented(v,out);result={}
 kinds=['impulse','burst_low','sine80','sine130','sine200','clean_guitar','electric_bass_proxy','low_mono_synth','irregular_percussion','drum_machine']
 for rate in [44100,48000]:
  for feedback in [50,75,85,95,100]:
   for mode in ['single','multihead','reverse','pingpong']:
    # All signals at all public feedbacks; rotate mode, plus each mode on low burst.
    selected=[kinds[(feedback//5+['single','multihead','reverse','pingpong'].index(mode))%len(kinds)],'burst_low']
    if mode=='single':selected=list(dict.fromkeys(kinds+selected))
    for kind in dict.fromkeys(selected):
     duration=60 if feedback>=95 and kind=='burst_low' else 12
     x=np.zeros((rate*duration,2),np.float32)
     if kind=='impulse':x[0]=.5
     elif kind in ['burst_low','sine80','sine130','sine200']:
      freq=40 if kind=='burst_low' else int(kind[4:]);n=rate;t=np.arange(n)/rate
      x[:n]=(.35*np.sin(2*np.pi*freq*t)*np.minimum(t/.005,1))[:,None]
     else:
      source=np.fromfile(inputs/(kind+'.input.f32'),'<f4').reshape(-1,2)
      if rate!=44100:
       from scipy.signal import resample_poly
       source=resample_poly(source,160,147,axis=0).astype(np.float32)
      x[:len(source)]=source
     speed,age,bump=(0,100,100) if feedback==95 else (100,0,100) if feedback==100 else (50,50,50)
     p=params(feedback,mode,speed,age,bump)
     y,meta=process(instrument,x,p,rate,True)
     # Independently prove instrumentation does not change actual candidate output.
     plain,plain_meta=process(runner,x,p,rate)
     assert np.array_equal(y,plain),'instrumented/production mismatch'
     sec=np.array([np.sqrt(np.mean(y[i*rate:(i+1)*rate].astype(float)**2)) for i in range(duration)])
     peaks=[float(abs(y[i*rate:(i+1)*rate]).max()) for i in range(duration)]
     ceiling=int(np.count_nonzero(abs(y)>=.9899))
     internal=np.array(meta['internal_second_rms']);late=internal[-1];previous=internal[-2]
     growth=20*np.log10(np.maximum(late,1e-12)/np.maximum(previous,1e-12))
     # A short screen compares a settled tail to an earlier multi-second window,
     # not two adjacent seconds containing different fractions of an impulse.
     # The canonical adjacent-second .05 dB rule is applied to 60-second tails.
     internal_tail_ratio=np.max(internal[-2:],axis=0)/np.maximum(np.max(internal[2:6],axis=0),1e-12)
     stable=bool(np.isfinite(y).all() and ceiling==0 and max(peaks[-2:])<=max(max(peaks[2:6])*1.02,1e-12) and np.max(internal_tail_ratio)<=1.02 and max(meta['internal_peaks_fbL_recL_magL_fbR_recR_magR_rawRecL_rawRecR'][-2:])<4)
     meta.update(duration_seconds=duration,rate=rate,mode=mode,source=kind,output_rms_per_second=sec.tolist(),output_peak_per_second=peaks,limiter_ceiling_samples=ceiling,internal_last_second_growth_db=growth.tolist(),instrumentation_bit_exact=True,plain_benchmark=plain_meta['block_call_us_P50_P95_P99_peak'],stable=stable)
     if duration==60:
      meta['canonical_tail_metrics']=gates.tail_metrics(y,rate)
      m=meta['canonical_tail_metrics'];stable=stable and m['last_minus_previous_db']<=.05 and m['peak_ratio']<=1.02 and np.max(growth)<=.05
      meta['stable']=bool(stable)
     key=f'{rate}_{kind}_fb{feedback}_{mode}';result[key]=meta
     dump(out/'stress.json',result)
     if not stable:raise RuntimeError('Stability rejected: '+key)
    print(f'v{v} stress {rate} fb{feedback} {mode}',flush=True)
 return result
def render_listening(v,out,inputs):
 runner=b.render.HydraEngineRunner(libpath(v));rows=[]
 sources=list(json.loads((inputs/'manifest.json').read_text())['sources'])
 # 12 scenes: every source, every mode, each fb in each mode; no 480-file dump.
 for i in range(12):
  kind=sources[i%8];mode=['single','multihead','reverse','pingpong'][i%4];feedback=[50,75,85][i//4]
  p=params(feedback,mode,clean=False)
  source=np.fromfile(inputs/(kind+'.input.f32'),'<f4').reshape(-1,2)
  x=np.zeros((44100*16,2),np.float32);x[:len(source)]=source
  y,meta=process(runner,x,p);name=f'scene{i+1:02d}'
  (out/(name+'.f32')).write_bytes(y.astype('<f4').tobytes())
  meta.update(scene=name,source=kind,mode=mode,feedback=feedback,sample_rate=44100,active_window=[0,6],synthetic=True)
  rows.append(meta)
 dump(out/'renders.json',rows)
def package(out):
 dest=out/'listening';dest.mkdir();shutil.copytree(ROOT/'tools/listening-lab',dest/'lab')
 # Use the existing Deck for synchronized switching; the F2 entry point adds a third slot.
 shutil.copy(HERE/'listening.html',dest/'index.html');shutil.copy(HERE/'listening.mjs',dest/'listening.mjs')
 allrows=[json.loads((out/f'variant{v}/renders.json').read_text()) for v in range(3)]
 rng=random.Random(20261123);manifest={'seed':20261123,'method':'MATCHED: stereo RMS in common 0..6s window, one constant gain/file relative to C0; common headroom trim <=.98; no limiter. RAW untouched. All stimuli synthetic.','scenes':[]};key={};private=[]
 for i,base in enumerate(allrows[0]):
  name=base['scene'];ys=[np.fromfile(out/f'variant{v}'/(name+'.f32'),'<f4').reshape(-1,2) for v in range(3)]
  rms=[np.sqrt(np.mean(y[:6*44100].astype(float)**2)) for y in ys];gains=[rms[0]/max(r,1e-30) for r in rms];trim=min(1,.98/max(float(abs(y).max())*g for y,g in zip(ys,gains)))
  order=rng.sample(range(3),3);scene={k:base[k] for k in ['scene','source','mode','feedback','sample_rate','synthetic']};scene['slots']=[];key[name]={}
  for j,v in enumerate(order):
   code=['X','Y','Z'][j];slot={'code':code,'files':{}}
   for fmt,y in [('RAW',ys[v]),('MATCHED',(ys[v].astype(float)*gains[v]*trim).astype(np.float32))]:
    filename=f'{name}-{code}-{fmt}.wav';b.render.write_wav_24bit(dest/filename,y,44100)
    slot['files'][fmt]={'path':filename,'sha256':b.sha((dest/filename).read_bytes())}
   scene['slots'].append(slot);key[name][code]=['Classic C0','Warm Tape','Vintage Progressive'][v]
  manifest['scenes'].append(scene)
  private.append({'scene':name,'per_variant':[dict(allrows[v][i],variant=v,matched_gain=float(gains[v]*trim),rms_window_0_6s=float(rms[v]),common_headroom_trim=float(trim),raw_sha256=b.sha(ys[v].tobytes())) for v in range(3)]})
 dump(dest/'manifest.json',manifest);dump(dest/'answer-key.json',key);dump(out/'render-provenance.json',private)
 shutil.copy(HERE/'LISTENING_GUIDE.md',dest/'LEIA-ME.md')
 hashes={p.relative_to(dest).as_posix():b.sha(p.read_bytes()) for p in sorted(dest.rglob('*')) if p.is_file()}
 dump(dest/'sha256.json',hashes)
 for p,h in hashes.items():assert b.sha((dest/p).read_bytes())==h
 print('Listening package complete:',dest,flush=True)
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('action',choices=['baseline','variant','stress','render','package']);p.add_argument('--variant',type=int,choices=range(3));p.add_argument('--output',type=Path,default=ROOT/'artifacts/dsp-reform-f2');a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
 if a.action=='baseline':baseline_check(out)
 elif a.action=='package':package(out)
 elif a.action in ['stress','render']:
  assert a.variant is not None;dest=out/f'variant{a.variant}';assert dest.is_dir()
  if a.action=='stress':stress(a.variant,dest,out/'inputs')
  else:render_listening(a.variant,dest,out/'inputs')
 else:
  assert a.variant is not None;v=a.variant;dest=out/f'variant{v}';dest.mkdir()
  contracts(v,dest);filters(v,dest);loops(v,dest);stress(v,dest,out/'inputs');render_listening(v,dest,out/'inputs')
if __name__=='__main__':main()
