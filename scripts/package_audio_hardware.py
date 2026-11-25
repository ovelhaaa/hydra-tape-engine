#!/usr/bin/env python3
"""Package the hardware target after the unchanged Live release gates."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
ENV = 'esp32-s3-hydra-live-hwtest'
FLAGS = {'HYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING': 1,
         'HYDRA_DSP_REFORM_F2_VARIANT': 2, 'HYDRA_DSP_REFORM_F3_VARIANT': 0,
         'HYDRA_DSP_REFORM_F4_TRANSITIONS': 1, 'HYDRA_DSP_REFORM_F5_TIMING_VARIANT': 1,
         'HYDRA_AUDIO_HWTEST': 1}

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def require(condition, message):
    if not condition:
        raise RuntimeError(message)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--gates', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=ROOT/'artifacts/hydra-live-v01-pcm1808-pcm5102-hwtest')
    args = parser.parse_args()
    gates = args.gates
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    require(not subprocess.check_output(['git','status','--porcelain','--untracked-files=no'],cwd=ROOT,text=True).strip(), 'tracked checkout must be clean')
    static = json.loads((gates/'static.json').read_text())
    require(all(static[k] for k in ('static_matrix_bit_exact','frozen_F3_A_bit_exact','musical_static_bit_exact')), 'static gates')
    require(static['canonical_core_sha256']==sha(ROOT/'dsp/core/tape_core.cpp'), 'core provenance')
    for pair in ('00','10','01','11'):
        parity=json.loads((gates/f'parity-{pair}.json').read_text())
        require(parity['status']=='PASS' and parity['max_abs']<=1e-5 and parity['rmse']<=1e-6, 'parity '+pair)
    for mode in ('production','live'):
        engine=json.loads((gates/f'{mode}-engine/engine-gates.json').read_text())
        require(all(g['pass'] for g in engine['gates']), mode+' engine')
        feedback=json.loads((gates/f'{mode}-feedback/feedback-stability.json').read_text())
        require(all(e['finite'] and e['internal_stable'] and e['stage_output_matches_full_engine'] and e['classification']!='unstable growth' for e in feedback['cases'].values()), mode+' feedback')
    for name in ('native','wasm'):
        log=(gates/f'{name}.txt').read_text(encoding='utf-8-sig')
        require('100% tests passed' in log and 'audio_hardware_pcm24_gpio' in log, name+' ctest')
    assembly=(gates/'audio-task-disassembly.txt').read_text(encoding='utf-8-sig')
    require(all(word in assembly for word in ('g_hw_route','g_adc_state','memw','l32i','s32i')), 'mailbox disassembly missing')
    require('__atomic_' not in assembly, 'unexpected out-of-line atomic helper in audio object')
    source=ROOT/'.pio/build'/ENV
    identity=(source/'hydra_firmware_identity.h').read_text()
    require(f'#define HYDRA_FIRMWARE_COMMIT "{commit}"' in identity,'identity commit/dirty')
    require(f'#define HYDRA_FIRMWARE_BUILD_NAME "{ENV}"' in identity,'identity environment')
    for flag,value in FLAGS.items():
        require(f'{flag}={value}' in identity,flag)
    paths=subprocess.check_output(['git','ls-files','src','include','lib','dsp/core','host/native/esp32',
                                   'platformio.ini','scripts/build_esp32_dsp.py'],cwd=ROOT,text=True).splitlines()
    digest=hashlib.sha256()
    for name in sorted(paths):
        digest.update(name.encode()+b'\0'+(ROOT/name).read_bytes()+b'\0')
    require(f'#define HYDRA_FIRMWARE_SOURCE_SHA256 "{digest.hexdigest()}"' in identity, 'identity source digest')
    for name in ('firmware.bin','firmware.elf'):
        require((source/name).stat().st_size>0,name)
    dest=args.output
    dest.mkdir(parents=True,exist_ok=True)
    require(not any(dest.iterdir()), 'use a fresh package directory')
    for name in ('firmware.bin','firmware.elf','firmware.map','bootloader.bin','partitions.bin','boot_app0.bin','flasher_args.json','hydra_firmware_identity.h'):
        if (source/name).is_file():
            shutil.copy2(source/name,dest/name)
    shutil.copytree(gates,dest/'gates')
    for name in ('HYDRA_PCM1808_PCM5102_WIRING.md','HYDRA_LIVE_V01_AUDIO_HARDWARE_AUDIT.md'):
        shutil.copy2(ROOT/'docs'/name,dest/name)
    pinout={'MCLK':14,'BCLK':15,'LRCK':16,'DAC_DIN':17,'ADC_DOUT':18}
    (dest/'pinout.json').write_text(json.dumps(pinout,indent=2)+'\n')
    manifest={'commit':commit,'environment':ENV,'flags':FLAGS,'pinout':pinout,
              'sample_rate':44100,'slots':2,'slot_bits':32,'adc_payload_bits':24,
              'nominal_mclk':11289600,'nominal_bclk':2822400,
              'clock_source':'PLL_160M, N=14, B=76, A=441',
              'platform':'pioarduino 55.03.36','arduino':'3.3.6','idf_headers':'5.5.2',
              'status':'READY_FOR_HARDWARE_WIRING — PENDING_PHYSICAL_CLOCK_VERIFICATION',
              'module_headers':'PENDING_MODULE_INSPECTION_BEFORE_SOLDERING_OR_POWER',
              'physical_clock_measured':False,'physical_cpu_and_listening':'PENDING',
              'core_sha256':sha(ROOT/'dsp/core/tape_core.cpp')}
    (dest/'manifest.json').write_text(json.dumps(manifest,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
    hashes={p.relative_to(dest).as_posix():sha(p) for p in dest.rglob('*') if p.is_file()}
    (dest/'sha256.json').write_text(json.dumps(hashes,indent=2)+'\n')
    for name,digest in hashes.items():
        require(sha(dest/name)==digest,name+' hash')
    print(json.dumps(manifest,indent=2))

if __name__=='__main__':
    main()
