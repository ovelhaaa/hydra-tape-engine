"""Physical Phase 6.3 CLI acquisition; no simulated or defaulted metrics."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time
import serial
from serial.tools import list_ports

ROOT = Path(__file__).resolve().parents[1]

def main():
    p = argparse.ArgumentParser(__doc__)
    p.add_argument('--port', required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--blocks', type=int, default=10000)
    p.add_argument('--profile', action='store_true')
    p.add_argument('--sustain', type=int, default=0)
    a = p.parse_args()
    a.output.mkdir(parents=True, exist_ok=True)
    identity = next(x for x in list_ports.comports() if x.device == a.port)
    report = {'physical': True, 'port': a.port, 'hwid': identity.hwid,
              'head': subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),
              'hashes': {}, 'commands': [], 'sustain_seconds': a.sustain}
    for path in ['dsp/core/tape_core.cpp', 'dsp/core/reverse_windows.hpp', 'src/main.cpp', '.pio/build/esp32-s3-devkitc-1/firmware.bin']:
        report['hashes'][path] = hashlib.sha256((ROOT/path).read_bytes()).hexdigest()
    s = serial.Serial(port=None, baudrate=115200, timeout=.1)
    s.port=a.port; s.dtr=False; s.rts=False; s.open()
    def cmd(command, seconds=.25, until=None):
        s.reset_input_buffer(); s.write((command+'\n').encode())
        data=b''; end=time.monotonic()+seconds
        while time.monotonic()<end:
            data += s.read(16384)
            if until and until.encode() in data: break
        raw=data.decode(errors='replace')
        report['commands'].append({'command':command,'response':raw})
        (a.output/'raw.txt').open('a',encoding='utf-8').write('\nCOMMAND '+command+'\n'+raw)
        (a.output/'capture.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
        if until and until not in raw: raise RuntimeError('Missing response terminator: '+command)
        return raw
    try:
        cmd('perf', 1)
        baseline=['byp 0','gen 1','src 1','mod 1','dly 500','fbk 40','hds 4','mix 50','drv 75','nlv 10',
                  'ftd 8','wwd 6','dps 0','tps 50','tpa 50','ppg 0','frz 0','frv 0','wrp 0','rev 0','spr 0','transport play']
        for c in baseline: cmd(c)
        scenarios={'delay': ['rev 0'], 'reverse_quarter':['rev 1','revmode 0','revdiv 0'],
                   'reverse_eighth':['revdiv 1'], 'reverse_sixteenth':['revdiv 2'],
                   'reverse_feedback95':['revdiv 0','fbk 95'],
                   'multihead':['rev 0','hds 7'],
                   'reverse_bpm':['rev 1','fbk 40','bpm 137'],
                   'stress_routed':['wrp 1','spr 1','rev 1','fbk 50']}
        for name, commands in scenarios.items():
            for c in commands: cmd(c)
            report['scenario']=name
            if a.profile:
                raw=cmd('stageprof 1000 256', 30, '=====================================================')
            else:
                raw=cmd(f'bench {a.blocks} 256', max(60,a.blocks*.02), '=========================================')
                if f'Blocks={a.blocks} ' not in raw: raise RuntimeError('Incorrect block count')
            (a.output/(name+'.txt')).write_text(raw,encoding='utf-8')
            print(name, raw,flush=True)
        if a.sustain:
            # Warp masks Reverse, so sustain explicitly returns to Reverse.
            for c in ['wrp 0','spr 0','rev 1','fbk 95','revdiv 0']: cmd(c)
            cmd('perf',1)
            end=time.monotonic()+a.sustain
            while time.monotonic()<end:
                cmd('perf',.5)
                time.sleep(min(10,max(0,end-time.monotonic())))
            print(cmd(f'bench {a.blocks} 256',max(60,a.blocks*.02),'========================================='),flush=True)
    finally:
        for c in ['gen 0','rev 0','wrp 0','spr 0','fbk 40']: cmd(c)
        s.close()

if __name__=='__main__': main()
