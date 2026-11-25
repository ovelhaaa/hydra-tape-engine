"""Prepare deterministic F2 inputs and parameter matrix; never renders candidates."""
from pathlib import Path
import argparse
import numpy as np
import baseline as b
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args()
a.output.mkdir(parents=True,exist_ok=False)
rows={}
for kind in ['clean_guitar','driven_guitar','mono_synth','poly_synth','drum_machine','irregular_percussion','electric_bass_proxy','low_mono_synth']:
 np.random.seed(20261123)
 if kind not in ['electric_bass_proxy','low_mono_synth']:
  x=b.musical_sources.get_musical_source(kind,6,44100).astype('<f4')
 else:
  # Explicit synthetic plucked-bass proxy, not a real DI recording.
  x=np.zeros((6*44100,2),np.float32)
  for j,f in enumerate([41.2034,55.,73.4162,82.4069,55.,41.2034,65.4064,82.4069]):
   start=int(j*.65*44100);n=min(int(.6*44100),len(x)-start);t=np.arange(n)/44100
   env=(1-np.exp(-t/.003))*np.exp(-t/.22)
   if kind=='low_mono_synth':env=(1-np.exp(-t/.01))*np.exp(-t/.5)
   y=.25*env*sum(np.sin(2*np.pi*f*k*t)/k**1.5 for k in range(1,9))
   x[start:start+n]=y[:,None]
 (a.output/(kind+'.input.f32')).write_bytes(x.tobytes());b.render.write_wav_24bit(a.output/(kind+'.wav'),x,44100)
 rows[kind]={'synthetic':True,'input_sha256':b.sha(x.tobytes()),'wav':b.wav_info(a.output/(kind+'.wav'))[0]}
scenes=[]
for source in rows:
 for feedback in [50,75,85,100]:
  for mode in ['single','multihead','reverse','pingpong','multihead_pingpong']:
   params=b.PARAMS.copy()
   for i,v in {12:500,13:feedback,14:100,15:100,16:7 if 'multihead' in mode else 4,23:int(mode=='reverse'),21:int('pingpong' in mode)}.items():params[i]=v
   scenes.append({'source':source,'mode':mode,'feedback':feedback,'parameters':params,'tail_seconds':60,'variants':['F2-A_C0','F2-B_NOT_IMPLEMENTED','F2-C_NOT_IMPLEMENTED'],'listening':['RAW','MATCHED_one_constant_gain_per_file']})
b.dump(a.output/'manifest.json',{'sample_rate':44100,'block_size':128,'seed':20261123,'recipe_sha256':b.sha(Path(__file__).read_bytes()),'sources':rows,'scenes':scenes,'human_recordings':'PENDING; synthetic proxies do not establish human preference'})
