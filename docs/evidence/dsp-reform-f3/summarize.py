"""Strict F3 readiness gates, compact auditable summary; never changes DSP."""
import run as f
import json
import numpy as np
from pathlib import Path
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def main():
 rows={};stress=[read(f.OUT/f'variant{v}/stress.json') for v in range(3)];dyn=[read(f.OUT/f'variant{v}/dynamics.json') for v in range(3)]
 for v in range(3):
  dest=f.OUT/f'variant{v}';s=stress[v];d=dyn[v];clipping={}
  for key,r in s.items():
   base=stress[0][key];delta=r['limiter_ceiling_samples']-base['limiter_ceiling_samples']
   if r['limiter_ceiling_samples'] or base['limiter_ceiling_samples']:
    clipping[key]={'A':base['limiter_ceiling_samples'],'candidate':r['limiter_ceiling_samples'],'delta':delta,'tail':r['tail_ceiling_samples'],'significant_increase':delta>max(3,.1*base['limiter_ceiling_samples'])}
  engine=read(dest/'engine/engine-gates.json');assert len(engine['gates'])==27 and all(g['pass'] for g in engine['gates'])
  parity=read(dest/'parity.json');contracts={platform:read(dest/f'contracts-{platform}/qualification.json') for platform in ['native','wasm']}
  changed=[key for key in d if d[key]['output_sha256']!=dyn[0][key]['output_sha256']]
  representative={key:{'THD':d[key]['estimated_THD_2_10'],'IMD3':d[key]['estimated_IMD3_ratio'],'stages':d[key]['stages'],'repeats':d[key]['repeats'],'true_peak_4x':d[key]['true_peak_estimate_4x']} for key in ['44100_fb85_-6dB_sine','44100_fb85_-6dB_two_tone','48000_fb95_-12dB_sine']}
  row={'stress_cases':len(s),'stress_all_stable':all(r['stable'] for r in s.values()),'tails_60s':sum(r['duration_seconds']==60 for r in s.values()),'instrumentation_bit_exact_all':all(r['instrumentation_bit_exact'] for r in s.values()),'compound':read(dest/'compound.json'),'clipping':clipping,'headroom_preclamp_peak':max(max(r['internal_peaks_fbL_recL_magL_fbR_recR_magR_rawRecL_rawRecR'][-2:]) for r in s.values()),'dynamics_cases':len(d),'dynamics_changed_vs_A':len(changed),'dynamics_finite_exact':all(r['finite'] and r['instrumentation_bit_exact'] for r in d.values()),'parity':parity,'contracts':contracts,'representative_dynamics':representative,'engine':engine}
  row['technical_pass']=bool(row['stress_all_stable'] and all(r['stable'] for r in row['compound'].values()) and not any(r['significant_increase'] for r in clipping.values()) and row['dynamics_finite_exact'] and (v==0 or changed) and parity['status']=='PASS')
  rows[str(v)]=row
 # Verify actual WAV hashes, PCM and matched levels independently of packager.
 dest=f.OUT/'listening';manifest=read(dest/'manifest.json');hashes=read(dest/'sha256.json');assert len(manifest['scenes'])==13
 for name,h in hashes.items():assert f.f2.b.sha((dest/name).read_bytes())==h,name
 from scipy.io import wavfile
 matched_spread=[];raw_count=0
 for scene in manifest['scenes']:
  rms=[]
  for slot in scene['slots']:
   for fmt in ['RAW','MATCHED']:
    path=dest/slot['files'][fmt]['path'];rate,y=wavfile.read(path);assert rate==44100 and y.ndim==2 and y.shape[1]==2 and y.dtype==np.int32
    z=y.astype(float)/2147483648
    if fmt=='MATCHED':rms.append(float(np.sqrt(np.mean(z[:6*rate]**2))));assert abs(z).max()<=.980001
    raw_count+=1
  matched_spread.append(max(rms)-min(rms))
 assert raw_count==78 and max(matched_spread)<1e-7
 baseline=read(f.OUT/'F3-A-manifest.json');assert all(r['bit_exact'] for r in baseline['scenarios'].values())
 result={'status':'OFFLINE_GATES_PASS_PUBLICATION_PENDING' if all(r['technical_pass'] for r in rows.values()) else 'BLOCKED_FOR_HUMAN_LISTENING','hardware':'PENDING_PHYSICAL','human':'PENDING_HUMAN','F2_decision':'F2_C_SELECTED_FOR_F3','baseline_10_bit_exact':True,'variants':rows,'package':{'wav_count':raw_count,'scenes':13,'all_hashes_verified':True,'max_matched_rms_spread':max(matched_spread)},'notes':['Known original A energy assertion remains visible.','Initial Windows Tap scheduling failure retained, rerun reported separately.','RMS or harmonic quantity alone is not quality.','True peak: offline scipy polyphase 4x estimate, not certified reconstruction.']}
 f.f2.dump(f.OUT/'validation-summary.json',result);print(result['status'],flush=True)
 assert all(r['technical_pass'] for r in rows.values()),'Candidate dynamics/clipping gate failed'
if __name__=='__main__':main()
