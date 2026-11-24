from pathlib import Path
import json,sys
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parent))
import baseline as b
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
BANDS=[(40,80),(80,150),(150,300),(300,1000),(1000,4000),(4000,22051)]
def bands(y):
 y=np.asarray(y,dtype=np.float64)
 if y.ndim==1:y=y[:,None]
 # Rectangular FFT, channel powers summed; Parseval one-sided weighting.
 p=np.abs(np.fft.rfft(y,axis=0))**2/len(y)**2
 p[1:-1]*=2
 f=np.fft.rfftfreq(len(y),1/44100)
 return {f'{lo}-{hi}':float(p[(f>=lo)&(f<hi)].sum()) for lo,hi in BANDS}
def db(x):return float(10*np.log10(max(x,1e-30)))
def main():
 import argparse
 p=argparse.ArgumentParser();p.add_argument('--library',type=Path,required=True);p.add_argument('--artifacts',type=Path,required=True);a=p.parse_args()
 result={'method':'Measured canonical C++ unit impulses; no analytic replacement. Loop windows are repeat-centred, include tails/noise and nonlinear/modulation effects; not an LTI transfer.', 'filters':{},'loops':{}}
 stages=['HPF','feedback_bump','feedback_gap','electronics_filters','C_playback_filters','C_all_filters','playback_bump']
 for path in sorted((a.artifacts/'filters').glob('*.f32')):
  _,sp,age,st=path.stem.split('_'); y=np.fromfile(path,'<f4');h=np.fft.rfft(y.astype(float)); f=np.fft.rfftfreq(len(y),1/44100)
  probes={str(hz):float(20*np.log10(max(abs(h[hz]),1e-15))) for hz in [40,60,80,100,130,150,200,300,1000,4000,8000,12000]}
  result['filters'][f'speed{sp}_age{age}_{stages[int(st)]}']={'sha256':b.sha(path.read_bytes()),'db_at_hz':probes,'peak_hz':int(f[np.argmax(abs(h))]),'peak_db':float(20*np.log10(max(abs(h)))),'tail_last_100ms_rms':float(np.sqrt(np.mean(y[-4410:].astype(float)**2))),'cumulative_filter_only_db':{str(n):{k:v*n for k,v in probes.items()} for n in [1,2,4,8]}}
 runner=b.render.HydraEngineRunner(a.library.resolve())
 for feedback in [50,75,85,100]:
  for kind in ['impulse','tone200']:
   x=np.zeros((44100*12,2),np.float32)
   if kind=='impulse': x[0]=.1
   else:
    t=np.arange(4410)/44100;x[:4410]=(.1*np.sin(2*np.pi*200*t))[:,None]
   params=b.PARAMS.copy()
   for i,v in {0:0,1:0,2:0,4:0,6:50,7:50,8:0,12:500,13:feedback,14:100,15:100,16:4,19:0,23:0,25:0,30:0}.items():params[i]=v
   y,e=b.process(runner,x,params);rows=[]
   for n in range(1,21):
    start=int((n*.5-.10)*44100);stop=int((n*.5+.40)*44100);seg=y[start:stop];power=bands(seg)
    row={'repeat_window':n,'start_frame':start,'stop_frame':stop,'peak':float(np.max(abs(seg))),'rms':float(np.sqrt(np.mean(seg.astype(float)**2))),'band_mean_square':power}
    spec=np.abs(np.fft.rfft(seg.astype(float),axis=0)).mean(axis=1)
    freqs=np.fft.rfftfreq(len(seg),1/44100)
    row['spectrum_db_at_hz']={str(f):float(20*np.log10(max(float(np.interp(f,freqs,spec)),1e-30))) for f in [40,60,80,100,130,150,200,300,1000,4000,8000,12000]}
    if rows:row['band_delta_db_vs_previous']={k:db(v)-db(rows[-1]['band_mean_square'][k]) for k,v in power.items()}
    # Coherent 50ms window within 100ms burst; harmonic projection includes loop noise/modulation.
    if kind=='tone200':
     z=y[int((n*.5+.025)*44100):int((n*.5+.075)*44100)].mean(axis=1);t=np.arange(len(z))/44100
     amps=[abs(np.sum(z*np.exp(-2j*np.pi*200*k*t))) for k in range(1,11)]
     row['harmonic_ratio_2_to_10_over_fundamental']=float(np.sqrt(np.sum(np.square(amps[1:])))/max(amps[0],1e-15))
    rows.append(row)
   name=f'{kind}_fb{feedback}';result['loops'][name]={'parameters':params,'readback':e,'finite':bool(np.isfinite(y).all()),'input_sha256':b.sha(x.tobytes()),'output_sha256':b.sha(y.tobytes()),'peak':float(np.max(abs(y))),'repeats':rows}
   b.render.write_wav_24bit(a.artifacts/(name+'.wav'),y,44100)
 b.dump(HERE/'spectral-measurements.json',result)
if __name__=='__main__':main()
