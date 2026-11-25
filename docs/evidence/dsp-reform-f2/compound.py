"""Additional 60s Multi-Head + Ping-Pong stress, both rates, all candidates."""
import run as f
import numpy as np
out=f.ROOT/'artifacts/dsp-reform-f2'
for v in range(3):
 runner=f.b.render.HydraEngineRunner(f.libpath(v));rows={}
 for rate in [44100,48000]:
  for feedback in [95,100]:
   x=np.zeros((rate*60,2),np.float32);t=np.arange(rate)/rate;x[:rate]=(.35*np.sin(2*np.pi*40*t)*np.minimum(t/.005,1))[:,None]
   p=f.params(feedback,'multihead_pingpong',speed=100,age=0,bump=100)
   y,m=f.process(runner,x,p,rate);tail=f.gates.tail_metrics(y,rate)
   m['tail']=tail;m['stable']=tail['finite'] and tail['last_minus_previous_db']<=.05 and tail['peak_ratio']<=1.02 and tail['limiter_ceiling_samples']==0
   rows[f'{rate}_fb{feedback}']=m
 f.dump(out/f'variant{v}/compound.json',rows)
 assert all(r['stable'] for r in rows.values()),f'Compound stress v{v} failed'
 print('Multi-Head + Ping-Pong 60s PASS',v,flush=True)
