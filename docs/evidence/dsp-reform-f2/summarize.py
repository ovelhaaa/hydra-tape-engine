"""Compact tracked evidence from complete local outputs; no baseline regeneration."""
import json,xml.etree.ElementTree as ET
from pathlib import Path
import run as f
out=f.ROOT/'artifacts/dsp-reform-f2';here=f.HERE
filters=[json.loads((out/f'variant{v}/filters.json').read_text()) for v in range(3)]
loops=[json.loads((out/f'variant{v}/loops.json').read_text()) for v in range(3)]
spectral={'note':'Full stage spectra/phase and complete loop windows in CI JSONs. Ratios in dB are measured output, not preference.','calibration_hpf_hz':[300,260,280],'filters':{},'loop_repeats':{}}
for key in filters[0]:
 if key.endswith('_combined') or key=='speed50_age50_bump50_tone50_HPF':
  spectral['filters'][key]={str(v):filters[v][key] for v in range(3)}
for key,base in loops[0].items():
 rows=[]
 for index in [0,1,3,7,15]:
  r0=base['repeats'][index];row={'repeat':index+1,'variants':{}}
  for v in range(3):
   r=loops[v][key]['repeats'][index];row['variants'][str(v)]={k:r[k] for k in ['band_mean_square','band_relative','rms','peak','spectrum_db_at_hz']}
   row['variants'][str(v)]['band_delta_db_vs_C0']={k:f.db(x)-f.db(r0['band_mean_square'][k]) for k,x in r['band_mean_square'].items()}
   if 'estimated_harmonics_2_10_ratio' in r:row['variants'][str(v)]['estimated_harmonics_2_10_ratio']=r['estimated_harmonics_2_10_ratio']
  rows.append(row)
 spectral['loop_repeats'][key]=rows
f.dump(here/'spectral-summary.json',spectral)
validation={'frozen_F1':'f3281d0e9230ad151bcad94c574042adc7996467','core_sha256':f.b.sha((f.ROOT/'dsp/core/tape_core.cpp').read_bytes()),'calibration_header_sha256':f.b.sha((f.ROOT/'dsp/core/feedback_calibration_experiment.hpp').read_bytes()),'hardware':'PENDING_PHYSICAL','human':'PENDING_HUMAN','variants':{}}
for v in range(3):
 folder=out/f'variant{v}';stress=json.loads((folder/'stress.json').read_text());engine=json.loads((folder/'engine/engine-gates.json').read_text());compound=json.loads((folder/'compound.json').read_text())
 assert len(stress)==154 and all(r['stable'] for r in stress.values())
 assert len(engine['gates'])==27 and all(r['pass'] for r in engine['gates'])
 parity=json.loads((folder/'parity.json').read_text());assert parity['status']=='PASS'
 row={'library_sha256':f.b.sha(f.libpath(v).read_bytes()),'api_version_hex':'0x010700','parameter_count':38,'parity':parity,'ctest':{},'engine_27_of_27':True,'stress_cases':len(stress),'tails_60s':sum(r['duration_seconds']==60 for r in stress.values()),'instrumentation_bit_exact_all':all(r['instrumentation_bit_exact'] for r in stress.values()),'stress_all_stable':True,'compound_60s_cases':len(compound),'compound_pass':all(r['stable'] for r in compound.values()),'global_output_peak':max(r['peak'] for r in stress.values()),'internal_stage_peak_max':[max(r['internal_peaks_fbL_recL_magL_fbR_recR_magR_rawRecL_rawRecR'][i] for r in stress.values()) for i in range(8)],'active_input_overload_cases':{k:{field:r[field] for field in ['limiter_ceiling_samples','tail_ceiling_samples','reference_C0_ceiling_samples','overload_qualification']} for k,r in stress.items() if r['limiter_ceiling_samples']},'tail_ceiling_samples_total':sum(r['tail_ceiling_samples'] for r in stress.values())}
 for platform in ['native','wasm']:
  tests=list(ET.parse(folder/f'contracts-{platform}/ctest.xml').iter('testcase'))
  row['ctest'][platform]={'tests':len(tests),'passed':sum(t.find('failure') is None for t in tests),'failed':[t.attrib['name'] for t in tests if t.find('failure') is not None],'qualification':json.loads((folder/f'contracts-{platform}/qualification.json').read_text())}
 validation['variants'][str(v)]=row
validation['c0_bit_exact']=json.loads((out/'c0-bit-exact.json').read_text())
validation['wrap_reference']=json.loads((out/'wrap-reference.json').read_text())
validation['production_A']=json.loads((out/'compatibility/production-A.json').read_text())
validation['warp']=json.loads((out/'compatibility/warp-negative-control.json').read_text())
validation['object_sizes']=json.loads((out/'compatibility/object-sizes.json').read_text())
f.dump(here/'validation-summary.json',validation)
f.dump(here/'render-manifest.json',{'blind_manifest':json.loads((out/'listening/manifest.json').read_text()),'private_provenance_location':'CI artifact render-provenance.json, outside blind listening folder','file_integrity':json.loads((out/'listening/sha256.json').read_text())})
print('PASS compact evidence: all gates present, no inferred human decision')
