"""Capture real ESP32 CLI results without substituting zeros for missing data.

Requires explicit --port and a previously uploaded, identified firmware image.
Never flashes a device. Keeps raw serial replies and host firmware/source hashes.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import time

import serial
from sustained_esp32_test import parse_bench, parse_perf

ROOT=Path(__file__).resolve().parents[1]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',required=True)
    parser.add_argument('--blocks',type=int,default=3000)
    parser.add_argument('--sustain-seconds',type=int,default=120)
    parser.add_argument('--output',type=Path,default=ROOT/'artifacts/bias-antialiasing/hardware')
    args=parser.parse_args(); args.output.mkdir(parents=True,exist_ok=True)
    result={'commit':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),
            'port':args.port,'source_sha256':hashlib.sha256((ROOT/'dsp/core/tape_core.cpp').read_bytes()).hexdigest(),
            'firmware_sha256':hashlib.sha256((ROOT/'.pio/build/esp32-s3-devkitc-1/firmware.bin').read_bytes()).hexdigest(),
            'method':f'Physical serial CLI bench:{args.blocks} blocks/256warmup,44100Hz128frames240MHz; timed audio processing excludes blocking I2S write and parameter update. No allocation or stack instrumentation.',
            'physical':True,'scenarios':{},'sustained':[], 'stack':None,'allocation_count':None}
    stream=serial.Serial(); stream.port=args.port; stream.baudrate=115200; stream.timeout=.1
    stream.dtr=False; stream.rts=False; stream.open()
    def command(text,wait=.25,bench=False):
        stream.reset_input_buffer(); stream.write((text+'\n').encode())
        end=time.monotonic()+(max(60,args.blocks*.02) if bench else wait)
        raw=b''
        while time.monotonic()<end:
            raw+=stream.read(4096)
            if bench and b'I2SFailDelta=' in raw and raw.rstrip().endswith(b'========================================='): break
        return raw.decode('utf-8',errors='replace')
    def save(): (args.output/'results.json').write_text(json.dumps(result,indent=2))
    def measured(name):
        raw=command(f'bench {args.blocks} 256',bench=True)
        (args.output/f'{name}.txt').write_text(raw,encoding='utf-8')
        metrics=parse_bench(raw)
        required=('mean_us','p95_us','p99_us','peak_us','overruns','i2s_fail_delta','heap_start','heap_end','psram_start','psram_end')
        missing=[key for key in required if key not in metrics]
        if missing: raise RuntimeError(f'{name}: missing hardware fields {missing}; raw reply preserved')
        if not re.search(rf'Blocks={args.blocks}\b',raw): raise RuntimeError('Incorrect measured block count')
        metrics['p99_below_70_percent']=metrics['p99_us']<(128/44100*1e6*.7)
        metrics['zero_overruns_i2s']=metrics['overruns']==0 and metrics['i2s_fail_delta']==0
        metrics['heap_min']=int(re.search(r'HeapMin=(\d+)',raw).group(1))
        metrics['max_parameter_update_us_cumulative']=int(re.search(r'MaxParamUpdUs=(\d+)',raw).group(1))
        result['scenarios'][name]=metrics;save(); print(name,json.dumps(metrics),flush=True)
    try:
        initial=command('perf',1)
        (args.output/'initial-perf.txt').write_text(initial,encoding='utf-8')
        result['initial_perf']=parse_perf(initial)
        # Known parameters for equivalent candidate scenarios, not persisted to flash.
        baseline=['byp 0','gen 1','src 1','mod 1','dly 500','fbk 40','hds 4','mix 50','drv 75','nlv 10',
                  'ftd 8','wwd 6','dps 0','tps 50','tpa 50','ppg 0','frz 0','frv 0','wrp 0','rev 0','spr 0','transport play']
        configurations={
            'delay':[], 'reverse_rhythmic':['rev 1','revmode 0','revdiv 0'],
            'reverse_free':['rev 1','revmode 1'],
            'reverse_feedback95':['rev 1','revmode 0','fbk 95'],
            'multihead_feedback95':['hds 7','fbk 95'],
            'full_chain':['wrp 1','spr 1'],
            'full_chain_reverse':['wrp 1','spr 1','rev 1','revmode 0','revdiv 0','fbk 50']}
        result['configuration_commands']={name:baseline+overrides for name,overrides in configurations.items()}
        for name,overrides in configurations.items():
            replies=''.join(command(c) for c in baseline+overrides)
            (args.output/f'{name}-setup.txt').write_text(replies,encoding='utf-8')
            measured(name)
        end=time.monotonic()+args.sustain_seconds
        while time.monotonic()<end:
            raw=command('perf',.5); sample=parse_perf(raw)
            sample['host_time_monotonic']=time.monotonic();result['sustained'].append(sample)
            with (args.output/'sustained-perf.txt').open('a',encoding='utf-8') as file: file.write(raw)
            save();print('sustained',json.dumps(sample),flush=True)
            time.sleep(min(15,max(0,end-time.monotonic())))
        measured('full_chain_reverse_after_sustain')
        final=command('perf',1);result['final_perf']=parse_perf(final)
        (args.output/'final-perf.txt').write_text(final,encoding='utf-8')
        # Leave board in standard delay mode with the test generator off.
        for c in ('gen 0','rev 0','wrp 0','spr 0','fbk 40'): command(c)
        save()
    finally:stream.close()


if __name__=='__main__':main()
