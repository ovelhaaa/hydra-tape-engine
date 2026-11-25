#!/usr/bin/env python3
"""Package clean firmware builds only after the workflow's mandatory gates."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
ROOT = Path(__file__).resolve().parents[1]
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser()
    p.add_argument('--output', type=Path, default=ROOT/'artifacts/hydra-vintage-live-v0.1-firmware')
    p.add_argument('--gates', type=Path, required=True)
    a=p.parse_args()
    commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    subprocess.run(['git','diff','--exit-code','HEAD','--','src','include','lib','dsp/core','host/native/esp32','platformio.ini','scripts/build_esp32_dsp.py'],cwd=ROOT,check=True)
    assert a.gates.is_dir()
    static=json.loads((a.gates/'static.json').read_text())
    assert static['static_matrix_bit_exact'] and static['frozen_F3_A_bit_exact'] and static['musical_static_bit_exact']
    assert static['canonical_core_sha256']==sha(ROOT/'dsp/core/tape_core.cpp'), 'firmware core differs from qualified core'
    for pair in ('00','10','01','11'):
        parity=json.loads((a.gates/f'parity-{pair}.json').read_text())
        assert parity['status']=='PASS' and parity['max_abs']<=1e-5 and parity['rmse']<=1e-6
    for mode in ('production','live'):
        engine=json.loads((a.gates/f'{mode}-engine/engine-gates.json').read_text())
        assert all(g['pass'] for g in engine['gates'])
        feedback=json.loads((a.gates/f'{mode}-feedback/feedback-stability.json').read_text())
        assert all(e['finite'] and e['internal_stable'] and e['stage_output_matches_full_engine'] and e['classification']!='unstable growth' for e in feedback['cases'].values())

    a.output.mkdir(parents=True,exist_ok=True)
    manifests={}
    for env,f4,f5 in [('esp32-s3-hydra-live',1,1),('esp32-s3-hydra-vintage-reference',0,0)]:
        source=ROOT/'.pio/build'/env
        dest=a.output/env
        dest.mkdir(exist_ok=True)
        identity=source/'hydra_firmware_identity.h'
        assert f'#define HYDRA_FIRMWARE_COMMIT "{commit}"' in identity.read_text(), identity
        assert f'#define HYDRA_FIRMWARE_BUILD_NAME "{env}"' in identity.read_text()
        for flag in [f'HYDRA_DSP_REFORM_F4_TRANSITIONS={f4}',f'HYDRA_DSP_REFORM_F5_TIMING_VARIANT={f5}','HYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING=1','HYDRA_DSP_REFORM_F2_VARIANT=2','HYDRA_DSP_REFORM_F3_VARIANT=0']:
            assert flag in identity.read_text(), flag
        for name in ('firmware.bin','firmware.elf','hydra_firmware_identity.h'):
            assert (source/name).is_file(),source/name
        for name in ('firmware.bin','firmware.elf','firmware.map','bootloader.bin','partitions.bin','boot_app0.bin','flasher_args.json','hydra_firmware_identity.h'):
            if (source/name).is_file(): shutil.copy2(source/name,dest/name)
        manifests[env]={'commit':commit,'flags':{'ClassicC':1,'F2':2,'F3':0,'F4':f4,'F5':f5},'files':{f.name:{'sha256':sha(f),'bytes':f.stat().st_size} for f in dest.iterdir() if f.is_file()}}
    shutil.copy2(ROOT/'docs/HYDRA_LIVE_V01_HARDWARE_TEST.md',a.output/'HARDWARE_TEST.md')
    for report in a.gates.rglob('*'):
        if report.is_file() and (report.suffix == '.json' or report.parent == a.gates):
            target=a.output/'gates'/report.relative_to(a.gates)
            target.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(report,target)
    manifest={'commit':commit,'status':'READY_FOR_PHYSICAL_VALIDATION','physical':'PENDING_PHYSICAL','firmware':manifests}
    (a.output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    hashes={str(f.relative_to(a.output)).replace('\\','/'):sha(f) for f in a.output.rglob('*') if f.is_file() and f.name!='sha256.json'}
    (a.output/'sha256.json').write_text(json.dumps(hashes,indent=2)+'\n')
    for name,digest in hashes.items(): assert sha(a.output/name)==digest
    print(json.dumps(manifest,indent=2))
if __name__=='__main__': main()
