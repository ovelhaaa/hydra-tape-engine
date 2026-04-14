#!/usr/bin/env python3
import argparse
import math
import subprocess
import sys


def run_capture(cmd):
    out = subprocess.check_output(cmd, text=True)
    values = []
    for line in out.strip().splitlines():
        l, r = line.split(',')
        values.append((float(l), float(r)))
    return values


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--native', required=True)
    ap.add_argument('--wasm-js', required=True)
    ap.add_argument('--max-abs-threshold', type=float, default=1e-5)
    ap.add_argument('--rmse-threshold', type=float, default=1e-6)
    args = ap.parse_args()

    native = run_capture([args.native])
    wasm = run_capture(['node', args.wasm_js])

    if len(native) != len(wasm):
      print(f'length mismatch: native={len(native)} wasm={len(wasm)}')
      return 2

    max_abs = 0.0
    err_sq_sum = 0.0
    n = 0
    for (nl, nr), (wl, wr) in zip(native, wasm):
        dl = abs(nl - wl)
        dr = abs(nr - wr)
        max_abs = max(max_abs, dl, dr)
        err_sq_sum += dl * dl + dr * dr
        n += 2

    rmse = math.sqrt(err_sq_sum / n) if n else 0.0
    print(f'max_abs={max_abs:.12e}')
    print(f'rmse={rmse:.12e}')

    if max_abs > args.max_abs_threshold or rmse > args.rmse_threshold:
        print('FAIL: tolerance exceeded')
        return 1

    print('PASS: within tolerance')
    return 0


if __name__ == '__main__':
    sys.exit(main())
