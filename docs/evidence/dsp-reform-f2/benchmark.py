"""Desktop indicator only: balanced candidate order, same deterministic input."""
import run as f
import numpy as np
out=f.ROOT/'artifacts/dsp-reform-f2'
source=np.fromfile(out/'inputs/clean_guitar.input.f32','<f4').reshape(-1,2)
x=np.zeros((44100*12,2),np.float32);x[:len(source)]=source
runners=[f.b.render.HydraEngineRunner(f.libpath(v)) for v in range(3)]
rows={str(v):[] for v in range(3)}
for v in [0,1,2,2,1,0,1,2,0]:
 _,m=f.process(runners[v],x,f.params(85,'multihead',clean=False))
 rows[str(v)].append({'library_sha256':runners[v].library_sha256,'block_us_P50_P95_P99_peak':m['block_call_us_P50_P95_P99_peak'],'elapsed_seconds':m['elapsed_seconds']})
f.dump(out/'native-benchmark.json',{'physical_status':'PENDING_PHYSICAL','note':'Desktop wall-clock ctypes process call, block128/44100; excludes parameter setup, includes system scheduling. Not ESP32 DSP/full-work timing; no acceptance of hardware inferred.','input_sha256':f.b.sha(x.tobytes()),'parameters':f.params(85,'multihead',clean=False),'raw':rows,'median_block_us_P50_P95_P99_peak':{k:np.median([r['block_us_P50_P95_P99_peak'] for r in rs],axis=0).tolist() for k,rs in rows.items()}})
print('Native desktop indicator saved; physical validation pending')
