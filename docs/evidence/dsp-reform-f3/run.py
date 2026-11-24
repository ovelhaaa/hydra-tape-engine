"""F3 bench: reuse immutable F2 gates/player, pin F2 Vintage in every build."""
from pathlib import Path
import sys, os, json, argparse, shutil, subprocess, importlib.util
import numpy as np
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
sys.path.insert(0,str(HERE.parent/'dsp-reform-f2'))
_spec=importlib.util.spec_from_file_location('hydra_f2_bench',HERE.parent/'dsp-reform-f2/run.py')
f2=importlib.util.module_from_spec(_spec);_spec.loader.exec_module(f2)
f2.flags=lambda v:['-DHYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING=1','-DHYDRA_DSP_REFORM_F2_VARIANT=2',f'-DHYDRA_DSP_REFORM_F3_VARIANT={v}']
f2.native=lambda v:ROOT/f'build/f3-{v}-native'
f2.wasm=lambda v:ROOT/f'build/f3-{v}-wasm'
OUT=ROOT/'artifacts/dsp-reform-f3'

def frozen(out):
 ref=out/'frozen-f2-source'
 if not ref.exists():f2.command(['git','worktree','add','--detach',ref,'2c15b88680ff65aff8bd5ac09543fab44b52c785'])
 lib=out/('frozen-f2.dll' if os.name=='nt' else 'frozen-f2.so')
 f2.command([os.environ.get('CXX','g++'),'-O3','-DNDEBUG','-std=c++17','-ffp-contract=off','-shared',*(['-fPIC'] if os.name!='nt' else []),*f2.flags(0),'-I'+str(ref/'include'),'-I'+str(ref/'dsp/core'),ref/'dsp/core/tape_core.cpp',ref/'dsp/core/hydra_dsp_c_api.cpp','-o',lib],out/'frozen-build.log')
 a=f2.b.render.HydraEngineRunner(lib);b=f2.b.render.HydraEngineRunner(f2.libpath(0));rows={}
 old=json.loads((HERE.parent/'dsp-reform-f0/baseline_renders_manifest.json').read_text())
 for name,scene in old['scenarios'].items():
  p=f2.b.PARAMS.copy()
  for k,v in scene['params_overrides'].items():p[int(k)]=v
  x=f2.b.stimulus(scene);ya,ea=f2.b.process(a,x,p);yb,eb=f2.b.process(b,x,p)
  assert np.array_equal(ya,yb) and ea==eb,name
  rows[name]={'input_sha256':f2.b.sha(x.tobytes()),'frozen_output_sha256':f2.b.sha(ya.tobytes()),'F3_A_output_sha256':f2.b.sha(yb.tobytes()),'bit_exact':True,'parameters':p,'readback':eb}
 f2.dump(out/'F3-A-manifest.json',{'source':'2c15b88680ff65aff8bd5ac09543fab44b52c785','flags':f2.flags(0),'compiler':f2.command([os.environ.get('CXX','g++'),'--version']).stdout,'frozen_library_sha256':f2.b.sha(lib.read_bytes()),'library_sha256':f2.b.sha(f2.libpath(0).read_bytes()),'lifecycle':'F0.2/F1 fresh create/prepare/reset/ascending IDs except29/internal tempo/play/commit/block128/no preroll','scenarios':rows})
 print('F3-A / frozen F2-C: 10/10 BIT EXACT',flush=True)

def render(v,out):
 runner=f2.b.render.HydraEngineRunner(f2.libpath(v));rows=[]
 scenes=[('clean_guitar',50,'single'),('driven_guitar',75,'single'),('electric_bass_proxy',85,'single'),('low_mono_synth',95,'single'),('poly_synth',75,'multihead'),('drum_machine',85,'pingpong'),('clean_guitar',85,'reverse'),('irregular_percussion',95,'multihead'),('clean_guitar',95,'single'),('driven_guitar',85,'reverse'),('electric_bass_proxy',95,'single'),('low_mono_synth',100,'single'),('clean_guitar',100,'single')]
 for i,(kind,fb,mode) in enumerate(scenes):
  p=f2.params(fb,mode,clean=False)
  if i>=10:p=f2.params(fb,mode,speed=0 if fb==95 else 100,age=100 if fb==95 else 0,bump=100)
  source=np.fromfile(OUT/'inputs'/(kind+'.input.f32'),'<f4').reshape(-1,2)
  x=np.zeros((44100*(60 if i>=10 else 16),2),np.float32);x[:len(source)]=source
  y,m=f2.process(runner,x,p);name=f'scene{i+1:02d}';(out/(name+'.f32')).write_bytes(y.astype('<f4').tobytes())
  m.update(scene=name,source=kind,mode=mode,feedback=fb,sample_rate=44100,active_window=[0,6],synthetic=True,tier='stress_60s' if i>=10 else 'main_16s');rows.append(m)
 f2.dump(out/'renders.json',rows)

def package(out):
 # Same F2 entry point and established Deck, only identity and names change.
 f2.HERE=HERE
 f2.package(out)
 dest=out/'listening'
 key=json.loads((dest/'answer-key.json').read_text())
 names={'Classic C0':'Vintage F3-A','Warm Tape':'Smooth Saturation F3-B','Vintage Progressive':'Harmonic Bloom F3-C'}
 f2.dump(dest/'answer-key.json',{s:{c:names[n] for c,n in slots.items()} for s,slots in key.items()})
 manifest=json.loads((dest/'manifest.json').read_text());manifest['method']=manifest['method'].replace('relative to C0','relative to Vintage F3-A');manifest['provenance']['F2_source']='2c15b88680ff65aff8bd5ac09543fab44b52c785';manifest['provenance']['F3_header_sha256']=f2.b.sha((ROOT/'dsp/core/saturation_calibration_experiment.hpp').read_bytes());manifest['provenance']['flags']={str(v):f2.flags(v) for v in range(3)}
 f2.dump(dest/'manifest.json',manifest)
 f2.dump(dest/'sha256.json',{p.relative_to(dest).as_posix():f2.b.sha(p.read_bytes()) for p in sorted(dest.rglob('*')) if p.is_file() and p.name!='sha256.json'})

def main():
 p=argparse.ArgumentParser();p.add_argument('action',choices=['baseline','contracts','stress','render','package']);p.add_argument('--variant',type=int,choices=range(3),default=0);a=p.parse_args();OUT.mkdir(parents=True,exist_ok=True);dest=OUT/f'variant{a.variant}';dest.mkdir(exist_ok=True)
 if a.action=='baseline':frozen(OUT)
 elif a.action=='contracts':f2.contracts(a.variant,dest)
 elif a.action=='stress':f2.stress(a.variant,dest,OUT/'inputs')
 elif a.action=='render':render(a.variant,dest)
 else:package(OUT)
if __name__=='__main__':main()
