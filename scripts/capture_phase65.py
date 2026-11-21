"""Phase 6.5 production acquisition. Never flashes; fails closed on incomplete data.

Run only after independent upload/readback verification of the expected production
image. The firmware has no commit query: this script does not invent one.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import subprocess
import time

import serial
from serial.tools import list_ports
from sustained_esp32_test import parse_bench, parse_perf

ROOT = Path(__file__).resolve().parents[1]
BASE = ['vol 10', 'byp 0', 'gen 1', 'src 1', 'mod 1', 'dly 500',
        'fbk 40', 'hds 4', 'mix 50', 'drv 75', 'nlv 10', 'ftd 8',
        'wwd 6', 'dps 0', 'tps 50', 'tpa 50', 'ppg 0', 'frz 0',
        'frv 0', 'wrp 0', 'rev 0', 'spr 0', 'revmode 0', 'revdiv 0',
        'tempo 120', 'transport play']
SCENARIOS = {
    'delay': [],
    'reverse-quarter': ['rev 1'],
    'reverse-eighth': ['rev 1', 'revdiv 3'],
    'reverse-sixteenth': ['rev 1', 'revdiv 4'],
    'reverse-feedback95': ['rev 1', 'fbk 95'],
    'multihead': ['hds 7', 'fbk 95'],
    'stress': ['wrp 1', 'spr 1', 'rev 1', 'fbk 50'],
}


def require(pattern, text):
    match = re.search(pattern, text)
    if not match:
        raise RuntimeError('Missing or incorrect field: ' + pattern)
    return match


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--serial', required=True, help='USB device serial, not COM number')
    p.add_argument('--firmware-sha256', required=True)
    p.add_argument('--verified-upload-log', type=Path, required=True)
    p.add_argument('--expected-commit', required=True)
    p.add_argument('--expected-source-sha256', required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--blocks', type=int, default=10000)
    p.add_argument('--sustain-seconds', type=int, default=300)
    a = p.parse_args()
    if a.blocks < 10000 or a.sustain_seconds < 300:
        p.error('Acceptance requires >=10000 blocks and >=300 seconds')
    firmware = ROOT / '.pio/build/esp32-s3-devkitc-1/firmware.bin'
    if hashlib.sha256(firmware.read_bytes()).hexdigest() != a.firmware_sha256:
        raise RuntimeError('Local firmware SHA mismatch')
    upload = a.verified_upload_log.read_text(encoding='utf-8')
    require('Hash of data verified', upload)
    require('mac:\\s*' + re.escape(a.serial.lower()), upload.lower())
    image = firmware.read_bytes()
    if image[32:36] != bytes.fromhex('3254cdab'):
        raise RuntimeError('Unsupported ESP app descriptor layout')
    expected_elf = image[176:208].hex()
    devices = [d for d in list_ports.comports()
               if d.vid == 0x303a and d.pid == 0x1001 and d.serial_number == a.serial]
    if len(devices) != 1:
        raise RuntimeError('Expected exactly one matching USB device')
    a.output.mkdir(parents=True, exist_ok=True)
    (a.output / 'benchmarks').mkdir(exist_ok=True)
    report = {'started_utc': datetime.now(timezone.utc).isoformat(),
              'head': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
              'firmware_sha256': a.firmware_sha256, 'port': devices[0].device,
              'hwid': devices[0].hwid, 'status': 'INCOMPLETE', 'scenarios': {},
              'sustain': [], 'commands': [], 'flash_performed_by_script': False,
              'physical_listening': 'NOT_TESTED', 'midi': 'PHYSICAL_MIDI_CLOCK_NOT_TESTED',
              'limitations': ['No external waveform capture or physical clock measurement',
                              'Timer excludes I2S blocking and parameter update',
                              'StackMinWords firmware label actually contains ESP-IDF bytes',
                              'Historical eighth/sixteenth used enum 1/2 (whole/half), current uses 3/4',
                              'Single PCM snapshots do not prove every block finite or nonsilent']}
    s = serial.Serial(port=None, baudrate=115200, timeout=.1)
    s.port = devices[0].device
    s.dtr = False
    s.rts = False
    previous_blocks = None

    def save():
        (a.output / 'results.json').write_text(json.dumps(report, indent=2), encoding='utf-8')

    def command(cmd, seconds=.22, until=None):
        s.write((cmd + '\n').encode())
        end = time.monotonic() + seconds
        raw = b''
        while time.monotonic() < end:
            raw += s.read(16384)
            if until and until.encode() in raw:
                break
        text = raw.decode('utf-8', errors='strict').replace('\r', '')
        entry = {'utc': datetime.now(timezone.utc).isoformat(), 'command': cmd, 'response': text}
        report['commands'].append(entry)
        save()
        if re.search(r'BOOT START|Guru Meditation|panic|watchdog|stack overflow|BENCH TIMEOUT', text, re.I):
            raise RuntimeError('Reset/crash/timeout detected')
        if until and until not in text:
            raise RuntimeError('Incomplete response: ' + cmd)
        if not text.strip():
            raise RuntimeError('No response: ' + cmd)
        return text

    def perf():
        nonlocal previous_blocks
        raw = command('perf', 1, '========================================')
        v = parse_perf(raw)
        for key in ('blocks', 'overruns', 'i2s_failures', 'heap_free', 'psram_free', 'peak_us'):
            if key not in v:
                raise RuntimeError('Missing perf ' + key)
        for pattern in (r'Block Size\s*:\s*128 frames', r'Sample Rate\s*:\s*44100 Hz', r'CPU Clock\s*:\s*240 MHz'):
            require(pattern, raw)
        v['stack_min_bytes'] = int(require(r'Audio Stack Min Free:\s*(\d+)', raw)[1])
        v['full_work_peak_us'] = int(require(r'Full Work Peak\s*:\s*(\d+)', raw)[1])
        v['full_work_overruns'] = int(require(r'Full Work Overruns\s*:\s*(\d+)', raw)[1])
        if previous_blocks is not None and v['blocks'] <= previous_blocks:
            raise RuntimeError('Audio stopped or reset detected')
        previous_blocks = v['blocks']
        if v['overruns'] or v['i2s_failures'] or v['full_work_overruns']:
            raise RuntimeError('Runtime deadline/I2S failure')
        return v

    def audio_health(active=True):
        raw = command('audiohealth', 3, '=== AUDIOHEALTH END ===')
        require(r'TxBytes=1024 TxError=0\b', raw)
        require(r'SaturatedWords=0\b', raw)
        values = {key: int(require(key + r'=(\d+)', raw)[1])
                  for key in ('NonZeroWords', 'PeakAbsPcm32', 'CompletedUs', 'StackScanUs', 'FullWorkUs')}
        if active and values['NonZeroWords'] == 0:
            raise RuntimeError('Active source produced an entirely silent PCM block')
        return values

    try:
        s.open()
        command('?', 1, 'load <preset>')
        identity = command('identity', 2, '=== IDENTITY END ===')
        require(r'FirmwareCommit=' + re.escape(a.expected_commit) + r'\n', identity)
        require(r'SourceSHA256=' + re.escape(a.expected_source_sha256) + r'\n', identity)
        require(r'ELFSHA256=' + expected_elf + r'\n', identity)
        require(r'CPU=240 SampleRate=44100 BlockFrames=128 PSRAMBytes=8388608 FlashBytes=16777216 I2SInitialized=1', identity)
        report['identity'] = identity
        report['initial_perf'] = perf()
        for c in ('vol 10', 'byp 1', 'src 1', 'gen 0'):
            command(c)
        off = audio_health(active=False)
        if off['NonZeroWords'] != 0:
            raise RuntimeError('Bypassed disabled generator is not silent')
        command('gen 1')
        on = [audio_health(active=False) for _ in range(10)]
        if not any(v['NonZeroWords'] > 0 for v in on):
            raise RuntimeError('Enabled generator never produced nonzero PCM')
        report['generator_control'] = {'off': off, 'on': on}
        for name, overrides in SCENARIOS.items():
            for c in BASE + overrides:
                reply = command(c)
                if c.startswith('revdiv '):
                    require('Reverse division: ' + c.split()[1], reply)
                if c.startswith('revmode '):
                    require('Reverse mode: ' + c.split()[1], reply)
            health = audio_health()
            report.setdefault('audio_health', {})[name] = health
            raw = command(f'bench {a.blocks} 256', max(60, a.blocks * .015 + 15), '=========================================')
            (a.output / 'benchmarks' / (name + '.txt')).write_text(raw, encoding='utf-8')
            require(rf'Blocks={a.blocks} Warmup=256\b', raw)
            expected = {'src': 1, 'gen': 1, 'byp': 0, 'mod': 1, 'dly': 500, 'fbk': 40,
                        'hds': 4, 'tps': 50, 'tpa': 50, 'drv': 75, 'wrp': 0, 'rev': 0, 'spr': 0}
            for c in overrides:
                key, value = c.split()
                if key in expected:
                    expected[key] = int(value)
            for key, value in expected.items():
                require(rf'\b{key}={value}\b', raw)
            v = parse_bench(raw)
            required = ('mean_us', 'p50_us', 'p95_us', 'p99_us', 'peak_us', 'overruns',
                        'i2s_fail_delta', 'heap_start', 'heap_end', 'psram_start', 'psram_end')
            if any(k not in v for k in required):
                raise RuntimeError('Incomplete benchmark metrics')
            v.update({'stack_min_bytes': int(require(r'StackMinWords=(\d+)', raw)[1]),
                      'max_parameter_update_us': int(require(r'MaxParamUpdUs=(\d+)', raw)[1]),
                      'heap_min': int(require(r'HeapMin=(\d+)', raw)[1]),
                      'saturated_histogram_bins': int(require(r'SatBins=(\d+)', raw)[1]),
                      'blocks': a.blocks, 'warmup': 256, 'commands': BASE + overrides})
            v['wall_ms'] = float(require(r'WallMs=([\d.]+)', raw)[1])
            v['effective_frames_per_second'] = a.blocks * 128 / (v['wall_ms'] / 1000)
            v['pass'] = (v['overruns'] == v['i2s_fail_delta'] == v['saturated_histogram_bins'] == 0
                         and v['heap_end'] >= v['heap_start'] and v['psram_end'] >= v['psram_start']
                         and v['stack_min_bytes'] >= 4096 and v['peak_us'] < 128 / 44100 * 1e6)
            report['scenarios'][name] = v
            save()
            print(name, json.dumps(v), flush=True)
            if not v['pass']:
                raise RuntimeError('Benchmark gate failed: ' + name)
            perf()
        stress_commands = ['tempo 90', 'tempo 120', 'tempo 140', 'tempo 90',
                           *[f'tempo {v}' for v in range(90, 141, 10)], 'tempo 90',
                           'dly 100', 'dly 1500', 'fbk 20', 'fbk 95',
                           'tps 10', 'tps 90', 'tpa 10', 'tpa 90',
                           'ton 10', 'ton 90', 'hbp 0', 'hbp 90', 'mix 10', 'mix 90',
                           'byp 1', 'byp 0', 'mod 0', 'mod 1',
                           'wrp 0', 'rev 1', 'rev 0', 'rev 1',
                           'transport pause', 'transport play', 'transport stop',
                           'transport play', 'transport reset', 'transport status']
        for c in stress_commands:
            command(c)
        report['stress_perf'] = perf()
        report['stress_audio_health'] = audio_health()
        for c in BASE + ['rev 1', 'fbk 95']:
            command(c)
        started = time.monotonic()
        while True:
            sample = perf()
            sample['elapsed_seconds'] = time.monotonic() - started
            sample['audio_health'] = audio_health()
            report['sustain'].append(sample)
            save()
            print('sustain', sample['elapsed_seconds'], sample['blocks'], flush=True)
            if sample['elapsed_seconds'] >= a.sustain_seconds:
                break
            time.sleep(min(10, a.sustain_seconds - sample['elapsed_seconds']))
        first, last = report['sustain'][0], report['sustain'][-1]
        if last['heap_free'] < first['heap_free'] or last['psram_free'] < first['psram_free']:
            raise RuntimeError('Sustain memory loss')
        report['sustain_blocks'] = last['blocks'] - first['blocks']
        report['status'] = 'MEASURED_GATES_PASS_AUDIO_CAPTURE_PENDING'
    except BaseException as error:
        report['status'] = 'FAIL_OR_INCOMPLETE'
        report['error'] = repr(error)
        raise
    finally:
        if s.is_open:
            for c in ('gen 0', 'vol 10', 'fbk 40', 'rev 0', 'wrp 0', 'spr 0'):
                try:
                    command(c)
                except Exception as error:
                    report.setdefault('cleanup_errors', []).append(repr(error))
            s.close()
        report['finished_utc'] = datetime.now(timezone.utc).isoformat()
        save()


if __name__ == '__main__':
    main()
