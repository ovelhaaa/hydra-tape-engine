"""Offline dynamic bench, actual instrumented canonical engine. No DSP edits."""
import run as f
import numpy as np
from scipy.signal import resample_poly

def stats(y):
 y=np.asarray(y,dtype=float);r=float(np.sqrt(np.mean(y*y)));p=float(abs(y).max())
 return {'rms':r,'peak':p,'crest_db':float(20*np.log10(max(p,1e-30)/max(r,1e-30)))}

def tones(y,rate,hz):
 z=np.asarray(y,dtype=float).mean(axis=1);t=np.arange(len(z))/rate
 return [float(2*abs(np.sum(z*np.exp(-2j*np.pi*h*t)))/len(z)) for h in hz]

def main():
 f.OUT.mkdir(parents=True,exist_ok=True)
 for v in range(3):
  dest=f.OUT/f'variant{v}';dest.mkdir(exist_ok=True)
  # Separate diagnostic destination avoids overwriting the stress probe.
  diag=dest/'dynamics-probe';diag.mkdir(exist_ok=True)
  instrument=f.f2.make_instrumented(v,diag);plain=f.f2.b.render.HydraEngineRunner(f.f2.libpath(v));rows={}
  for rate in [44100,48000]:
   for fb in [0,25,50,65,75,85,95,100]:
    for level in [-24,-12,-6,0]:
     for kind in ['sine','two_tone','burst']:
      x=np.zeros((rate*4,2),np.float32);t=np.arange(rate)/rate;gain=10**(level/20)
      z=np.sin(2*np.pi*1000*t) if kind!='two_tone' else .5*(np.sin(2*np.pi*900*t)+np.sin(2*np.pi*1100*t))
      if kind=='burst':z*=np.where(t<.25,.25,np.where(t<.5,1,.25))
      x[:rate]=(gain*z)[:,None]
      captured=[];lib=instrument.lib;original=lib.hydra_dsp_process
      def call(*args):
       status=original(*args);n=args[-1]
       rec=np.ctypeslib.as_array(lib.phase64_diag_data(),shape=(n*17,)).reshape(n,17)
       captured.append(rec.copy());return status
      lib.hydra_dsp_process=call
      try:y,m=f.f2.process(instrument,x,f.f2.params(fb),rate,True)
      finally:lib.hydra_dsp_process=original
      yp,_=f.f2.process(plain,x,f.f2.params(fb),rate);assert np.array_equal(y,yp)
      rec=np.concatenate(captured)
      # L/R loop feedback, pre-clamp record, magnetics, monitor L before limiter,
      # final L/R output. No monitor clipping is attributed to the loop.
      stages={'loop_feedback':rec[:,[4,11]],'record_pre_clamp':rec[:,[15,16]],'magnetic_write':rec[:,[6,12]],'monitor_pre_limiter_L':rec[:,2:3],'final_output':y}
      stage={name:stats(a) for name,a in stages.items()}
      repeats=[]
      for n in range(1,7):
       start=int((.5*n+.05)*rate);end=min(start+int(.2*rate),len(y));seg=y[start:end]
       if len(seg):repeats.append(dict(repeat=n,**stats(seg)))
      for n in range(1,len(repeats)):repeats[n]['incremental_rms_gain_db']=float(20*np.log10(max(repeats[n]['rms'],1e-30)/max(repeats[n-1]['rms'],1e-30)))
      segment=y[int(.65*rate):int(.85*rate)]
      harmonic=tones(segment,rate,[1000*k for k in range(1,11)]) if kind=='sine' else None
      imd=tones(segment,rate,[900,1100,700,1300,200,2000]) if kind=='two_tone' else None
      # A single .25s step cannot isolate an envelope in this memoryless knee;
      # retain 10ms RMS windows around arrival/recovery in the complete loop.
      windows=[stats(y[i:i+rate//100])['rms'] for i in range(rate//2,rate+rate//2,rate//100)] if kind=='burst' else None
      rows[f'{rate}_fb{fb}_{level}dB_{kind}']={'rate':rate,'feedback':fb,'level_dbfs_peak':level,'kind':kind,'input_sha256':m['input_sha256'],'output_sha256':m['output_sha256'],'stages':stage,'repeats':repeats,'harmonic_amplitudes_1_10':harmonic,'estimated_THD_2_10':float(np.linalg.norm(harmonic[1:])/max(harmonic[0],1e-30)) if harmonic else None,'IMD_amplitudes_900_1100_700_1300_200_2000':imd,'estimated_IMD3_ratio':float(np.linalg.norm(imd[2:4])/max(np.linalg.norm(imd[:2]),1e-30)) if imd else None,'burst_10ms_rms_from_500ms':windows,'monitor_ceiling_samples':int(np.count_nonzero(abs(y)>=.9899)),'true_peak_estimate_4x':float(abs(resample_poly(y.astype(float),4,1,axis=0)).max()),'instrumentation_bit_exact':True,'finite':True}
    f.f2.dump(dest/'dynamics.json',rows)
    print(f'dynamics v{v} {rate} fb{fb}',flush=True)
 print('Dynamic matrix complete',flush=True)
if __name__=='__main__':main()
