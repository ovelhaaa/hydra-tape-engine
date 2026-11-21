"""Physical ESP32-S3 TapeCore stage profile for Reverse real-time work.

Requires the diagnostic firmware built from env:esp32-s3-stageprofile.
No simulated or defaulted metrics: every value is parsed from the board.
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
    'delay': [],
    'reverse_quarter': ['rev 1', 'revmode 0', 'revdiv 0'],
    'reverse_eighth': ['revdiv 1'],
    'reverse_sixteenth': ['revdiv 2'],
    'reverse_feedback95': ['revdiv 0', 'fbk 95'],
    'multihead': ['rev 0', 'hds 7'],
}
STAGE_RE = re.compile(r'^([a-z_]+)\s+([0-9.]+)\s+([0-9.]+)%\s+([0-9.]+)%\s*$')


def command(link, text, seconds=0.4, until=None):
    link.reset_input_buffer()
    link.write((text + '\n').encode())
    data = b''
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        data += link.read(65536)
        if until and until.encode() in data:
            break
        if until:
            time.sleep(0.01)
    out = data.decode(errors='replace')
    if until and until not in out:
        raise RuntimeError(f'missing terminator for {text!r}: {out[-300:]}')
    return out


def parse_profile(raw):
    stages = {}
    for line in raw.splitlines():
        match = STAGE_RE.match(line.strip())
        if match and match.group(1) != 'total':
            stages[match.group(1)] = {
                'us_per_block': float(match.group(2)),
                'percent_core': float(match.group(3)),
                'percent_budget': float(match.group(4)),
            }
    if not stages:
        raise RuntimeError(f'no stages parsed: {raw[-400:]}')
    return stages


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--blocks', type=int, default=1000)
    parser.add_argument('--warmup', type=int, default=256)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)

    link = serial.Serial(args.port, 115200, timeout=0.1)
    link.dtr = False
    link.rts = False
    report = {'physical': True, 'port': args.port, 'blocks': args.blocks,
              'warmup': args.warmup, 'scenarios': {}}
    try:
        command(link, 'perf', seconds=0.8)
        for entry in BASE:
            command(link, entry)
        for name, setups in SCENARIOS.items():
            for entry in setups:
                command(link, entry)
            time.sleep(0.6)
            before = command(link, 'perf', seconds=0.6)
            raw = command(link, f'prof2 {args.blocks} {args.warmup}', seconds=120,
                          until='=====================================================')
            after = command(link, 'perf', seconds=0.6)
            report['scenarios'][name] = {
                'commands': setups,
                'stages': parse_profile(raw),
                'perf_before': before,
                'perf_after': after,
                'raw': raw,
            }
            print(f'{name}: ' + ', '.join(
                f'{stage}={values["us_per_block"]:.1f}us'
                for stage, values in report['scenarios'][name]['stages'].items()
                if values['us_per_block'] > 20.0), flush=True)
    finally:
        for entry in ('rev 0', 'wrp 0', 'spr 0', 'gen 0'):
            try:
                command(link, entry)
            except Exception:
                pass
        link.close()
    args.output.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(f'wrote {args.output}', flush=True)


if __name__ == '__main__':
    main()
