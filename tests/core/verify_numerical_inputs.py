"""Compare runner inputs and compiled LUT as IEEE float32 bits, before DSP tolerances."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess


def capture(command, mode):
    lines = subprocess.check_output(command + [mode], text=True).splitlines()
    return [int(v, 16) for line in lines for v in line.split(',')]


def digest(values):
    return hashlib.sha256(b''.join(struct.pack('<I', v) for v in values)).hexdigest()


def ordered(v):
    return (~v & 0xffffffff) if v & 0x80000000 else v | 0x80000000


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--native', required=True)
    parser.add_argument('--wasm-js', required=True)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    report = {}
    for label, mode, count in [('input', '--dump-input', 4096), ('lut', '--dump-lut', 1025)]:
        native = capture([args.native], mode)
        wasm = capture(['node', args.wasm_js], mode)
        differences = [(i, a, b) for i, (a, b) in enumerate(zip(native, wasm)) if a != b]
        report[label] = dict(native_sha256=digest(native), wasm_sha256=digest(wasm),
                             native_count=len(native), wasm_count=len(wasm),
                             divergent_values=len(differences),
                             first_divergence=differences[0] if differences else None,
                             max_ulp=max((abs(ordered(a)-ordered(b)) for _, a, b in differences), default=0),
                             status='PASS' if native == wasm and len(native) == count else 'FAIL')
    path = Path(args.output)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    return int(any(row['status'] != 'PASS' for row in report.values()))


if __name__ == '__main__':
    raise SystemExit(main())
