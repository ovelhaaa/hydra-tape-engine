"""Evaluate every original assertion in WASM after the known A energy mismatch."""
import run as f
import os, argparse
p=argparse.ArgumentParser();p.add_argument('--variant',type=int,choices=range(3));args=p.parse_args()
for v in range(3) if args.variant is None else [args.variant]:
 dest=f.OUT/f'variant{v}';exe=dest/'regression-observation.js'
 compiler=os.environ.get('EMXX','C:/emsdk/upstream/emscripten/em++.bat' if os.name=='nt' else 'em++')
 f.f2.command([compiler,'-O3','-std=c++17','-ffp-contract=off',*f.f2.flags(v),'-I'+str(f.ROOT/'include'),'-I'+str(f.ROOT/'dsp/core'),f.HERE.parent/'dsp-reform-f1/regression_observation.cpp',f.f2.wasm(v)/'libhydra_dsp_core.a','-sALLOW_MEMORY_GROWTH=1','-sENVIRONMENT=node','-sEXIT_RUNTIME=1','-o',exe],dest/'wasm-observation-build.log')
 r=f.f2.command(['node',exe],dest/'wasm-observation.log',False)
 failures=[s for s in r.stdout.splitlines() if s.startswith('OBSERVATION_FAIL')]
 assert r.returncode==len(failures) and len(failures)<=1
 assert not failures or 'channel_energy(fullR, 2840, 2960) > 1e-5f' in failures[0]
 assert 'block_exact_L=1 block_exact_R=1' in r.stdout
 f.f2.dump(dest/'wasm-observation.json',{'exit':r.returncode,'failures':failures,'full_split_exact':True,'stdout':r.stdout})
 print('WASM complete observation PASS (known A mismatch retained)',v,flush=True)
