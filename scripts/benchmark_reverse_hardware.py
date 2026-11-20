"""Physical ESP32-S3 benchmark for Phase 6.3 Reverse real-time work.

Every value is parsed from the board; nothing is simulated or defaulted.
Requires the production firmware (env:esp32-s3-devkitc-1).
"""
import argparse
import json
import re
import time
from pathlib import Path

import serial

BASE = [
    'byp 0', 'gen 1', 'src 1', 'mod 1', 'dly 500', 'fbk 40', 'hds 4', 'mix 50',
    'drv 75', 'nlv 10', 'ftd 8', 'wwd 6', 'dps 0', 'tps 50', 'tpa 50', 'ppg 0',
    'frz 0', 'frv 0', 'wrp 0', 'rev 0', 'spr 0', 'transport play',
]
SCENARIOS = {
    'delay': ['rev 0', 'hds 4', 'fbk 40'],
    'reverse_quarter': ['rev 1', 'revmode 0', 'revdiv 0', 'fbk 40'],
    'reverse_eighth': ['revdiv 1'],
    'reverse_sixteenth': ['revdiv 2'],
    'reverse_feedback95': ['revdiv 0', 'fbk 95'],
    'multihead': ['rev 0', 'hds 7'],
    'reverse_bpm137': ['rev 1', 'revdiv 0', 'fbk 40', 'bpm 137'],
    'reverse_bpm90': ['bpm 90'],
    'stress_warp_spring': ['rev 1', 'fbk 50', 'wrp 1', 'spr 1'],
}
BENCH_RE = {
    'mean_us': r'MeanUs=([0-9.]+)',
    'min_us': r'MinUs=(-?[0-9]+)',
    'p50_us': r'P50Us=([0-9]+)',
    'p95_us': r'P95Us=([0-9]+)',
    'p99_us': r'P99Us=([0-9]+)',
    'peak_us': r'PeakUs=(-?[0-9]+)',
    'overruns': r'Overruns=([0-9]+)',
    'i2s_fail_delta': r'I2SFailDelta=([0-9]+)',
    'heap_start': r'HeapStart=([0-9]+)',
    'heap_end': r'HeapEnd=([0-9]+)',
    'psram_start': r'PsramStart=([0-9]+)',
    'psram_end': r'PsramEnd=([0-9]+)',
    'max_param_update_us': r'MaxParamUpdUs=(-?[0-9]+)',
    'blocks': r'Blocks=([0-9]+)',
    'budget_p99_pct': r'BudgetP99Pct=([0-9.]+)',
    'stack_min_words': r'StackMinWords=([0-9]+)',
}


def command(link, text, seconds=0.5, until=None):
    link.reset_input_buffer()
    link.write((text + '\n').encode())
    data = b''
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        chunk = link.read(65536)
        data += chunk
        if until and until.encode() in data:
            break
        if until:
            time.sleep(0.005)
    out = data.decode(errors='replace')
    if until and until not in out:
        raise RuntimeError(f'missing terminator for {text!r}: {out[-400:]}')
    return out


def parse_bench(raw):
    values = {}
    for key, pattern in BENCH_RE.items():
        match = re.search(pattern, raw)
        if match:
            values[key] = float(match.group(1)) if '.' in match.group(1) else int(match.group(1))
    for required in ('mean_us', 'p99_us', 'overruns', 'blocks'):
        if required not in values:
            raise RuntimeError(f'bench output missing {required}: {raw[-500:]}')
    return values


def parse_perf(raw):
    values = {}
    for key, pattern in {
        'overruns': r'DSP Budget Overruns\s*:\s*([0-9]+)',
        'i2s_failures': r'I2S TX Write Failures\s*:\s*([0-9]+)',
        'blocks': r'Blocks Processed\s*:\s*([0-9]+)',
        'avg_us': r'Avg DSP Block Time\s*:\s*([0-9]+)',
        'peak_us': r'Peak DSP Block Time\s*:\s*([0-9]+)',
        'heap': r'Internal Free Heap\s*:\s*([0-9]+)',
        'psram': r'SPIRAM \(PSRAM\) Free\s*:\s*([0-9]+)',
    }.items():
        match = re.search(pattern, raw)
        if match:
            values[key] = int(match.group(1))
    return values


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--blocks', type=int, default=10000)
    parser.add_argument('--warmup', type=int, default=256)
    parser.add_argument('--sustain', type=int, default=300)
    parser.add_argument('--skip-sustain', action='store_true')
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)

    link = serial.Serial(args.port, 115200, timeout=0.1)
    link.dtr = False
    link.rts = False
    report = {'physical': True, 'port': args.port, 'blocks': args.blocks,
              'warmup': args.warmup, 'scenarios': {}, 'sustain': None}
    try:
        command(link, 'perf', seconds=1.0)
        for entry in BASE:
            command(link, entry)
        for name, setups in SCENARIOS.items():
            for entry in setups:
                command(link, entry)
            time.sleep(0.6)
            raw = command(link, f'bench {args.blocks} {args.warmup}', seconds=120,
                          until='=========================================')
            report['scenarios'][name] = {'commands': setups, 'bench': parse_bench(raw),
                                         'raw': raw}
            b = report['scenarios'][name]['bench']
            print(f"{name}: mean={b['mean_us']:.1f} p95={b['p95_us']} p99={b['p99_us']} "
                  f"peak={b['peak_us']} overruns={b['overruns']} maxparam={b['max_param_update_us']}us",
                  flush=True)

        # Parameter-update probe: disable then re-enable delay, then read the
        # accumulated MaxParamUpdUs through a short bench.
        command(link, 'dly 0')
        time.sleep(0.5)
        command(link, 'dly 500')
        time.sleep(0.5)
        probe_raw = command(link, 'bench 256 16', seconds=60, until='=========================================')
        report['parameter_update_probe'] = {'bench': parse_bench(probe_raw), 'raw': probe_raw}
        print('param probe max=', report['parameter_update_probe']['bench']['max_param_update_us'], flush=True)

        if not args.skip_sustain:
            command(link, 'resetperf')
            command(link, 'wrp 0')
            command(link, 'spr 0')
            command(link, 'rev 1')
            command(link, 'revdiv 0')
            command(link, 'fbk 40')
            command(link, 'dly 500')
            start = parse_perf(command(link, 'perf', seconds=0.8))
            end_time = time.monotonic() + args.sustain
            while time.monotonic() < end_time:
                time.sleep(min(20, max(0, end_time - time.monotonic())))
            finish = parse_perf(command(link, 'perf', seconds=0.8))
            report['sustain'] = {'seconds': args.sustain, 'start': start, 'end': finish}
            print('sustain overruns delta=', finish['overruns'] - start['overruns'], flush=True)
    finally:
        for entry in ('gen 0', 'rev 0', 'wrp 0', 'spr 0', 'fbk 40'):
            try:
                command(link, entry)
            except Exception:
                pass
        link.close()
    args.output.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(f'wrote {args.output}', flush=True)


if __name__ == '__main__':
    main()
