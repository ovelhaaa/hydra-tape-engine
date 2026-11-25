"""No preset/API writes: frozen production A, Warp control, flags and sizes."""
import sys,os,subprocess,json
from pathlib import Path
import run as f
import numpy as np
out=f.ROOT/'artifacts/dsp-reform-f2';dest=out/'compatibility';dest.mkdir(exist_ok=True)
compiler=os.environ.get('CXX','g++');rows={}
for flags,expected in [([],0),(['-DHYDRA_DSP_REFORM_F2_VARIANT=0'],0),(['-DHYDRA_DSP_REFORM_F2_VARIANT=1'],1),(['-DHYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING=0','-DHYDRA_DSP_REFORM_F2_VARIANT=2'],1),(['-DHYDRA_DSP_REFORM_F2_VARIANT=3'],1),(['-DHYDRA_DSP_REFORM_F2_VARIANT=-1'],1)]:
 r=subprocess.run([compiler,'-std=c++17','-fsyntax-only','-x','c++',*flags,'-include',str(f.ROOT/'dsp/core/feedback_calibration_experiment.hpp'),'-'],input='',capture_output=True,text=True)
 assert bool(r.returncode)==bool(expected);rows[str(flags)]={'exit':r.returncode,'stderr':r.stderr}
f.dump(dest/'selection-contract.json',rows)
frozen=out/'frozen-f1-source';libraries=[]
for name,src in [('production-A-frozen',frozen),('production-A-F2',f.ROOT)]:
 lib=dest/(name+('.dll' if os.name=='nt' else '.so'))
 f.command([compiler,'-O3','-DNDEBUG','-std=c++17','-ffp-contract=off','-shared',*(['-fPIC'] if os.name!='nt' else []),'-I'+str(src/'include'),'-I'+str(src/'dsp/core'),src/'dsp/core/tape_core.cpp',src/'dsp/core/hydra_dsp_c_api.cpp','-o',lib],dest/(name+'.log'))
 libraries.append(f.b.render.HydraEngineRunner(lib))
old=json.loads((f.HERE.parent/'dsp-reform-f0/baseline_renders_manifest.json').read_text())
result={}
for name,scene in old['scenarios'].items():
 p=f.b.PARAMS.copy()
 for k,v in scene['params_overrides'].items():p[int(k)]=v
 x=f.b.stimulus(scene);a,ea=f.b.process(libraries[0],x,p);b,eb=f.b.process(libraries[1],x,p)
 assert np.array_equal(a,b) and ea==eb
 result[name]={'bit_exact':True,'output_sha256':f.b.sha(a.tobytes()),'input_sha256':f.b.sha(x.tobytes())}
f.dump(dest/'production-A.json',result)
sources=out/'inputs';x=np.zeros((44100*10,2),np.float32);source=np.fromfile(sources/'clean_guitar.input.f32','<f4').reshape(-1,2);x[:len(source)]=source
p=f.params(85);p[30]=1;p[34]=85
ys=[f.process(f.b.render.HydraEngineRunner(f.libpath(v)),x,p)[0] for v in range(3)]
for v in range(3):
 runner=f.b.render.HydraEngineRunner(f.libpath(v))
 runner.lib.hydra_dsp_get_param_count.restype=f.C.c_uint32
 assert runner.api_version==0x010700 and runner.lib.hydra_dsp_get_param_count()==38
assert all(np.array_equal(ys[0],y) for y in ys)
f.dump(dest/'warp-negative-control.json',{'all_variants_bit_exact':True,'parameters':p,'output_sha256':f.b.sha(ys[0].tobytes())})
sizes={}
for v in range(3):
 build=f.native(v);paths=list(build.glob('CMakeFiles/hydra_dsp_core.dir/dsp/core/tape_core.cpp.*'));obj=next(p for p in paths if p.suffix in ['.obj','.o'])
 raw=f.command(['size',obj]).stdout
 symbols=f.command(['nm','-S','--size-sort',obj]).stdout
 sizes[str(v)]={'object_bytes':obj.stat().st_size,'size':raw,'process_and_update_symbols':[s for s in symbols.splitlines() if 'processStereoBlock' in s or 'updateFilters' in s]}
f.dump(dest/'object-sizes.json',sizes)
# Record real effective flags for ALL compiled core translation units.
commands={}
for v in range(3):
 for platform,build in [('native',f.native(v)),('wasm',f.wasm(v))]:
  commands[f'{v}-{platform}']=json.loads((build/'compile_commands.json').read_text())
  actual=[r['command'] for r in commands[f'{v}-{platform}'] if Path(r['file']).name in ['tape_core.cpp','hydra_dsp_c_api.cpp']]
  assert actual and all(all(flag in cmd for flag in f.flags(v)) and '-ffp-contract=off' in cmd for cmd in actual)
f.dump(dest/'compile-commands.json',commands)
print('PASS production A 10/10 exact, Warp exact, selection guards, effective flags',flush=True)
