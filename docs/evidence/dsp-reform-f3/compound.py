import run as f
import numpy as np
for v in range(3):
 runner=f.f2.b.render.HydraEngineRunner(f.f2.libpath(v));rows={}
 for rate in [44100,48000]:
  for fb in [95,100]:
   x=np.zeros((rate*60,2),np.float32);t=np.arange(rate)/rate;x[:rate]=(.35*np.sin(2*np.pi*40*t)*np.minimum(t/.005,1))[:,None]
   y,m=f.f2.process(runner,x,f.f2.params(fb,'multihead_pingpong',speed=100,age=0,bump=100),rate);tail=f.f2.gates.tail_metrics(y,rate)
   m.update(tail=tail,stable=bool(tail['finite'] and tail['last_minus_previous_db']<=.05 and tail['peak_ratio']<=1.02 and tail['limiter_ceiling_samples']==0));rows[f'{rate}_fb{fb}']=m
 f.f2.dump(f.OUT/f'variant{v}/compound.json',rows);assert all(r['stable'] for r in rows.values())
 print('compound 60s PASS',v,flush=True)
