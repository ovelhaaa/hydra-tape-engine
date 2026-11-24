"""F3 guards, unchanged A/C0/Warm, Warp, ABI, effective flags and object size."""
import run as f
import os, subprocess, json
from pathlib import Path
import numpy as np
dest=f.OUT/'compatibility';dest.mkdir(parents=True,exist_ok=True)
compiler=os.environ.get('CXX','g++');rows={}
for flags,fail in [([],False),(['-DHYDRA_DSP_REFORM_F3_VARIANT=0'],False),(['-DHYDRA_DSP_REFORM_F3_VARIANT=1'],True),(['-DHYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING=1','-DHYDRA_DSP_REFORM_F3_VARIANT=2'],True),(['-DHYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING=0','-DHYDRA_DSP_REFORM_F2_VARIANT=2','-DHYDRA_DSP_REFORM_F3_VARIANT=1'],True),([*f.f2.flags(1)],False),([*f.f2.flags(2)],False),(['-DHYDRA_DSP_REFORM_F3_VARIANT=-1'],True),(['-DHYDRA_DSP_REFORM_F3_VARIANT=3'],True)]:
 r=subprocess.run([compiler,'-std=c++17','-fsyntax-only','-x','c++',*flags,'-include',str(f.ROOT/'dsp/core/saturation_calibration_experiment.hpp'),'-'],input='',capture_output=True,text=True)
 assert bool(r.returncode)==fail;rows[str(flags)]={'exit':r.returncode,'stderr':r.stderr}
f.f2.dump(dest/'selection-contract.json',rows)
old=json.loads((f.HERE.parent/'dsp-reform-f0/baseline_renders_manifest.json').read_text());preserved={}
for name,flags in [('production-A',[]),('Classic-C0',['-DHYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING=1']),('Warm',['-DHYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING=1','-DHYDRA_DSP_REFORM_F2_VARIANT=1'])]:
 runners=[]
 for label,src in [('frozen',f.OUT/'frozen-f2-source'),('F3',f.ROOT)]:
  lib=dest/(name+'-'+label+('.dll' if os.name=='nt' else '.so'))
  f.f2.command([compiler,'-O3','-DNDEBUG','-std=c++17','-ffp-contract=off','-shared',*(['-fPIC'] if os.name!='nt' else []),*flags,'-I'+str(src/'include'),'-I'+str(src/'dsp/core'),src/'dsp/core/tape_core.cpp',src/'dsp/core/hydra_dsp_c_api.cpp','-o',lib],dest/(name+'-'+label+'.log'))
  runners.append(f.f2.b.render.HydraEngineRunner(lib))
 result={}
 for scene,recipe in old['scenarios'].items():
  p=f.f2.b.PARAMS.copy()
  for k,v in recipe['params_overrides'].items():p[int(k)]=v
  x=f.f2.b.stimulus(recipe);a,ea=f.f2.b.process(runners[0],x,p);b,eb=f.f2.b.process(runners[1],x,p)
  assert np.array_equal(a,b) and ea==eb
  result[scene]={'bit_exact':True,'output_sha256':f.f2.b.sha(a.tobytes())}
 preserved[name]=result
f.f2.dump(dest/'preserved.json',preserved)
x=np.zeros((441000,2),np.float32);source=np.fromfile(f.OUT/'inputs/clean_guitar.input.f32','<f4').reshape(-1,2);x[:len(source)]=source
p=f.f2.params(85);p[30]=1;p[34]=85;ys=[];sizes={};commands={}
for v in range(3):
 runner=f.f2.b.render.HydraEngineRunner(f.f2.libpath(v));runner.lib.hydra_dsp_get_param_count.restype=f.f2.C.c_uint32
 assert runner.api_version==0x010700 and runner.lib.hydra_dsp_get_param_count()==38
 ys.append(f.f2.process(runner,x,p)[0])
 obj=next(p for p in (f.f2.native(v)/'CMakeFiles/hydra_dsp_core.dir/dsp/core').glob('tape_core.cpp.*') if p.suffix in ['.obj','.o'])
 sizes[str(v)]={'size':f.f2.command(['size',obj]).stdout,'symbols':f.f2.command(['nm','-S','--size-sort',obj]).stdout}
 for platform,build in [('native',f.f2.native(v)),('wasm',f.f2.wasm(v))]:
  actual=json.loads((build/'compile_commands.json').read_text());core=[r['command'] for r in actual if Path(r['file']).name in ['tape_core.cpp','hydra_dsp_c_api.cpp']]
  assert core and all(all(flag in cmd for flag in f.f2.flags(v)) and '-ffp-contract=off' in cmd for cmd in core)
  commands[f'{v}-{platform}']=actual
assert all(np.array_equal(ys[0],y) for y in ys)
f.f2.dump(dest/'warp.json',{'bit_exact':True,'parameters':p,'output_sha256':f.f2.b.sha(ys[0].tobytes()),'api':'1.7/38'})
f.f2.dump(dest/'sizes.json',sizes);f.f2.dump(dest/'commands.json',commands)
print('PASS A/C0/Warm 10/10 each, Warp exact, ABI, guards, flags',flush=True)
