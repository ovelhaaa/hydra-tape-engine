#!/usr/bin/env python3
import argparse
import array
import json
import math
import pathlib
import subprocess
import sys
import wave


def get_scenarios(native_bin):
    out = subprocess.check_output([native_bin, '--list-scenarios'], text=True)
    scenarios = [line.strip() for line in out.splitlines() if line.strip()]
    if not scenarios:
        raise RuntimeError(f'no scenarios returned by {native_bin} --list-scenarios')
    return scenarios


def run_capture(cmd):
    out = subprocess.check_output(cmd, text=True)
    values = []
    for line in out.strip().splitlines():
        l, r = line.split(',')
        values.append((float(l), float(r)))
    return values


def clamp_pcm16(value):
    return max(-32768, min(32767, int(round(value))))


def write_stereo_wav(path, values, sample_rate=48000):
    with wave.open(str(path), 'wb') as wav_file:
        wav_file.setnchannels(2)
        wav_file.setsampwidth(2)
        wav_file.setframerate(sample_rate)
        data = array.array('h')
        for left, right in values:
            data.append(clamp_pcm16(left * 32767.0))
            data.append(clamp_pcm16(right * 32767.0))
        wav_file.writeframes(data.tobytes())


def compute_metrics(native, wasm):
    max_abs = 0.0
    err_sq_sum = 0.0
    n = 0
    for (nl, nr), (wl, wr) in zip(native, wasm):
        dl = nl - wl
        dr = nr - wr
        max_abs = max(max_abs, abs(dl), abs(dr))
        err_sq_sum += dl * dl + dr * dr
        n += 2
    rmse = math.sqrt(err_sq_sum / n) if n else 0.0
    return max_abs, rmse, err_sq_sum


def main():
    ap = argparse.ArgumentParser(description='Build offline A/B package native vs wasm for auditory review')
    ap.add_argument('--native', required=True)
    ap.add_argument('--wasm-js', required=True)
    ap.add_argument('--null-test-script', default='tests/core/null_test_wav.py')
    ap.add_argument('--output-dir', default='artifacts/ab_review')
    ap.add_argument('--max-abs-threshold', type=float, default=1e-5)
    ap.add_argument('--rmse-threshold', type=float, default=1e-6)
    args = ap.parse_args()

    scenarios = get_scenarios(args.native)

    root = pathlib.Path(args.output_dir)
    root.mkdir(parents=True, exist_ok=True)

    package = {
        'thresholds': {'max_abs': args.max_abs_threshold, 'rmse': args.rmse_threshold},
        'scenarios': [],
    }

    for scenario in scenarios:
        scenario_dir = root / scenario
        scenario_dir.mkdir(parents=True, exist_ok=True)

        native = run_capture([args.native, '--scenario', scenario])
        wasm = run_capture(['node', args.wasm_js, '--scenario', scenario])

        if len(native) != len(wasm):
            raise RuntimeError(f'length mismatch for scenario={scenario}: native={len(native)} wasm={len(wasm)}')

        max_abs, rmse, null_energy = compute_metrics(native, wasm)
        status = 'PASS' if max_abs <= args.max_abs_threshold and rmse <= args.rmse_threshold else 'FAIL'

        native_wav = scenario_dir / 'native.wav'
        wasm_wav = scenario_dir / 'wasm.wav'
        diff_wav = scenario_dir / 'diff.wav'
        write_stereo_wav(native_wav, native)
        write_stereo_wav(wasm_wav, wasm)

        null_cmd = [
            sys.executable,
            args.null_test_script,
            '--reference', str(native_wav),
            '--candidate', str(wasm_wav),
            '--diff-out', str(diff_wav),
            '--max-abs-threshold', str(args.max_abs_threshold),
            '--rmse-threshold', str(args.rmse_threshold),
        ]
        null_proc = subprocess.run(null_cmd, check=False, capture_output=True, text=True)

        summary = {
            'scenario': scenario,
            'samples': len(native),
            'max_abs': max_abs,
            'rmse': rmse,
            'null_test_energy': null_energy,
            'status': status,
            'null_test_status': 'PASS' if null_proc.returncode == 0 else 'FAIL',
            'null_test_output': null_proc.stdout.strip(),
        }
        package['scenarios'].append(summary)

        (scenario_dir / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
        (scenario_dir / 'null_test.txt').write_text(null_proc.stdout + (null_proc.stderr or ''), encoding='utf-8')

    package['status'] = 'PASS' if all(item['status'] == 'PASS' and item['null_test_status'] == 'PASS' for item in package['scenarios']) else 'FAIL'
    (root / 'package_summary.json').write_text(json.dumps(package, indent=2) + '\n', encoding='utf-8')

    lines = [
        '# A/B Offline Review Package',
        '',
        f"Overall status: **{package['status']}**",
        '',
        '| Scenario | max_abs | rmse | null_test_energy | status |',
        '|---|---:|---:|---:|---|',
    ]
    for item in package['scenarios']:
        lines.append(
            f"| {item['scenario']} | {item['max_abs']:.3e} | {item['rmse']:.3e} | {item['null_test_energy']:.3e} | {item['status']} / {item['null_test_status']} |"
        )
    (root / 'README.md').write_text('\n'.join(lines) + '\n', encoding='utf-8')

    print(f"A/B package created in {root} with status={package['status']}")
    return 0 if package['status'] == 'PASS' else 1


if __name__ == '__main__':
    raise SystemExit(main())
