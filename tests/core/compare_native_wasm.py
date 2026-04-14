#!/usr/bin/env python3
import argparse
import json
import math
import pathlib
import subprocess
import sys
import wave


def run_capture(cmd):
    out = subprocess.check_output(cmd, text=True)
    values = []
    for line in out.strip().splitlines():
        l, r = line.split(',')
        values.append((float(l), float(r)))
    return values


def write_report_txt(path, report):
    lines = [
        f"test={report['test_name']}",
        f"samples={report['samples']}",
        f"max_abs={report['max_abs']:.12e}",
        f"rmse={report['rmse']:.12e}",
        f"status={report['status']}",
    ]
    path.write_text("\n".join(lines) + "\n", encoding='utf-8')


def clamp_pcm16(value):
    return max(-32768, min(32767, int(round(value))))


def write_stereo_wav(path, values, sample_rate=48000):
    with wave.open(str(path), 'wb') as wav_file:
        wav_file.setnchannels(2)
        wav_file.setsampwidth(2)
        wav_file.setframerate(sample_rate)
        frames = bytearray()
        for left, right in values:
            frames += clamp_pcm16(left * 32767.0).to_bytes(2, byteorder='little', signed=True)
            frames += clamp_pcm16(right * 32767.0).to_bytes(2, byteorder='little', signed=True)
        wav_file.writeframes(bytes(frames))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--native', required=True)
    ap.add_argument('--wasm-js', required=True)
    ap.add_argument('--max-abs-threshold', type=float, default=1e-5)
    ap.add_argument('--rmse-threshold', type=float, default=1e-6)
    ap.add_argument('--test-name', default='native_vs_wasm_equivalence')
    ap.add_argument('--report-json')
    ap.add_argument('--report-txt')
    ap.add_argument('--diff-csv')
    ap.add_argument('--render-wav-dir')
    args = ap.parse_args()

    native = run_capture([args.native])
    wasm = run_capture(['node', args.wasm_js])

    if len(native) != len(wasm):
        print(f'length mismatch: native={len(native)} wasm={len(wasm)}')
        return 2

    max_abs = 0.0
    err_sq_sum = 0.0
    n = 0
    diffs = [] if args.diff_csv else None
    for index, ((nl, nr), (wl, wr)) in enumerate(zip(native, wasm)):
        dl = nl - wl
        dr = nr - wr
        abs_l = abs(dl)
        abs_r = abs(dr)
        max_abs = max(max_abs, abs_l, abs_r)
        err_sq_sum += dl * dl + dr * dr
        n += 2
        if args.diff_csv:
            diffs.append((index, dl, dr, abs_l, abs_r))

    rmse = math.sqrt(err_sq_sum / n) if n else 0.0
    passed = max_abs <= args.max_abs_threshold and rmse <= args.rmse_threshold
    status = 'PASS' if passed else 'FAIL'

    report = {
        'test_name': args.test_name,
        'samples': len(native),
        'max_abs': max_abs,
        'rmse': rmse,
        'status': status,
        'thresholds': {
            'max_abs': args.max_abs_threshold,
            'rmse': args.rmse_threshold,
        },
    }

    print(f"{report['test_name']}: max_abs={max_abs:.12e} rmse={rmse:.12e} status={status}")

    if args.report_json:
        report_json_path = pathlib.Path(args.report_json)
        report_json_path.parent.mkdir(parents=True, exist_ok=True)
        report_json_path.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')

    if args.report_txt:
        report_txt_path = pathlib.Path(args.report_txt)
        report_txt_path.parent.mkdir(parents=True, exist_ok=True)
        write_report_txt(report_txt_path, report)

    if args.diff_csv:
        diff_csv_path = pathlib.Path(args.diff_csv)
        diff_csv_path.parent.mkdir(parents=True, exist_ok=True)
        with diff_csv_path.open('w', encoding='utf-8') as fp:
            fp.write('sample,delta_l,delta_r,abs_delta_l,abs_delta_r\n')
            for idx, dl, dr, abs_l, abs_r in diffs:
                fp.write(f'{idx},{dl:.12e},{dr:.12e},{abs_l:.12e},{abs_r:.12e}\n')

    if args.render_wav_dir:
        wav_dir = pathlib.Path(args.render_wav_dir)
        wav_dir.mkdir(parents=True, exist_ok=True)
        write_stereo_wav(wav_dir / 'native.wav', native)
        write_stereo_wav(wav_dir / 'wasm.wav', wasm)
        write_stereo_wav(wav_dir / 'diff.wav', ((nl - wl, nr - wr) for (nl, nr), (wl, wr) in zip(native, wasm)))

    return 0 if passed else 1


if __name__ == '__main__':
    sys.exit(main())