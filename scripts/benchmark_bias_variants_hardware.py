"""Flash isolated magnetic variants on an explicitly selected test ESP32-S3.

Uses a git-archive project copy under artifacts; never edits production DSP.
Restores the production firmware in finally. Requires PlatformIO Python and pyserial.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time
import zipfile

import serial
from bias_antialiasing import model_sources
from sustained_esp32_test import parse_bench

ROOT=Path(__file__).resolve().parents[1]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',required=True)
    parser.add_argument('--pio-python',required=True)
    parser.add_argument('--blocks',type=int,default=1000)
    args=parser.parse_args()
    out=ROOT/'artifacts/bias-antialiasing/hardware-variants';out.mkdir(parents=True,exist_ok=True)
    project=ROOT/'artifacts/bias-antialiasing/hardware-project'
    project.mkdir(exist_ok=True)
    archive=out/'project.zip'
    subprocess.run(['git','archive','HEAD','-o',str(archive)],check=True)
    with zipfile.ZipFile(archive) as z:z.extractall(project)
    sources,_=model_sources()
    env=dict(os.environ,PYTHONUTF8='1',PYTHONIOENCODING='utf-8')
    report={'commit':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),
            'port':args.port,'blocks':args.blocks,'warmup':256,'physical':True,'variants':{},
            'method':'Same ESP32-S3,240MHz44100Hz stereo128frames. Identical CLI configurations per variant. Firmware processing excludes blocking I2S write and parameter update. Native costs must not be substituted for these results.'}
    def save(): (out/'results.json').write_text(json.dumps(report,indent=2))
    def pio(directory,log):
        command=[args.pio_python,'-m','platformio','run','-e','esp32-s3-devkitc-1','-t','upload','--upload-port',args.port]
        with log.open('w',encoding='utf-8') as stream:
            completed=subprocess.run(command,cwd=directory,stdout=stream,stderr=subprocess.STDOUT,env=env,timeout=300)
        return completed.returncode
    try:
        for name,source in sources.items():
            print('Physical variant:',name,flush=True)
            folder=out/name;folder.mkdir(exist_ok=True)
            (project/'dsp/core/tape_core.cpp').write_text(source,encoding='utf-8')
            entry={'source_sha256':hashlib.sha256(source.encode()).hexdigest()};report['variants'][name]=entry;save()
            code=pio(project,folder/'build-upload.txt');entry['build_upload_returncode']=code
            if code:save();continue
            entry['firmware_sha256']=hashlib.sha256((project/'.pio/build/esp32-s3-devkitc-1/firmware.bin').read_bytes()).hexdigest()
            time.sleep(4)
            stream=serial.Serial();stream.port=args.port;stream.baudrate=115200;stream.timeout=.1;stream.dtr=False;stream.rts=False
            stream.open()
            def command(text,bench=False):
                stream.reset_input_buffer();stream.write((text+'\n').encode())
                raw=b'';end=time.monotonic()+(35 if bench else .2)
                while time.monotonic()<end:
                    raw+=stream.read(4096)
                    if bench and b'I2SFailDelta=' in raw and raw.rstrip().endswith(b'========================================='):break
                return raw.decode('utf-8',errors='replace')
            try:
                setup=['byp 0','gen 1','src 1','mod 1','dly 500','fbk 40','hds 4','mix 50','drv 75','nlv 10',
                       'ftd 8','wwd 6','dps 0','tps 50','tpa 50','ppg 0','frz 0','frv 0','wrp 0','rev 0','spr 0','transport play']
                entry['setup_commands']=setup
                (folder/'setup.txt').write_text(''.join(command(c) for c in setup),encoding='utf-8')
                for case,commands in {'delay':[], 'reverse_rhythmic':['rev 1','revmode 0','revdiv 0']}.items():
                    for c in commands:command(c)
                    raw=command(f'bench {args.blocks} 256',bench=True)
                    (folder/f'{case}.txt').write_text(raw,encoding='utf-8')
                    metrics=parse_bench(raw)
                    required=('mean_us','p95_us','p99_us','peak_us','overruns','i2s_fail_delta','heap_start','heap_end','psram_start','psram_end')
                    blocks=re.search(r'Blocks=(\d+)\b',raw)
                    count_ok=bool(blocks and int(blocks.group(1))==args.blocks)
                    entry[case]={'metrics':metrics,'missing_fields':[k for k in required if k not in metrics], 'correct_block_count':count_ok}
                    print(name,case,json.dumps(entry[case]),flush=True);save()
                    if entry[case]['missing_fields'] or not count_ok:break
            finally:stream.close()
            save()
    finally:
        print('Restoring production firmware',flush=True)
        report['production_restore_returncode']=pio(ROOT,out/'restore-production.txt');save()
        if report['production_restore_returncode']==0:
            time.sleep(4)
            restore=serial.Serial();restore.port=args.port;restore.baudrate=115200;restore.timeout=.2
            restore.dtr=False;restore.rts=False;restore.open()
            try:
                restore.write(b'gen 0\nrev 0\nwrp 0\nspr 0\n')
            finally:restore.close()
    if report['production_restore_returncode']!=0:raise SystemExit('Production firmware restore failed')


if __name__=='__main__':main()
