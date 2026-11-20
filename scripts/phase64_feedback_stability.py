#!/usr/bin/env python3
"""Phase 6.4 feedback/tail-stability analysis for the four failing gates.

Reproduces the exact full-engine cases and gates from
`scripts/validate_bias_engine.py`:

    gate: rms_dbfs(last second) <= rms_dbfs(third second)

then runs each case for much longer than the gate does, measures the energy
envelope, the spectral distribution of the residual tail and the growth slope,
and classifies the behaviour as decay, limited sustain, controlled oscillation
or unstable growth.

The gate only sees the post-limiter output. `outputLimiter` saturates above
0.9 and clamps at 0.99, so a bounded output does not by itself prove internal
stability; `--stages` additionally runs the instrumented DSP build and records
the pre-limiter record/feedback/magnetic-write state.
"""
import argparse
import json
import math
import hashlib
import subprocess
from pathlib import Path
import sys

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import render_phase6_review as render  # noqa: E402
import package_reverse_ab as build  # noqa: E402

# Exactly the baseline parameter map used by scripts/validate_bias_engine.py.
BASE_PARAMS = {0: 0, 1: 0, 2: 0, 3: 25, 4: 0, 5: 50, 6: 50, 7: 0, 8: 0, 11: 1,
               12: 125, 13: 35, 14: 100, 15: 100, 16: 1, 19: 0, 20: 100,
               23: 0, 25: 0, 30: 0}

CASES = {
    "feedback95": {13: 95},
    "multihead": {13: 75, 16: 7},
    "reverse": {13: 75, 23: 1},
}


def tone(rate, seconds):
    t = np.arange(int(seconds * rate)) / rate
    return np.repeat((0.1 * np.sin(2 * np.pi * 440 * t))[:, None], 2, axis=1).astype(np.float32)


def stimulus(rate, seconds):
    x = tone(rate, seconds)
    x[int(rate):] = 0.0  # input stops after 1 s, exactly like the gate
    return x


def dbfs(x):
    x = x.astype(np.float64)
    return float(20.0 * np.log10(max(np.sqrt(np.mean(x ** 2)), 1e-12)))


def run(case, rate, seconds, lib):
    params = dict(BASE_PARAMS)
    params.update(CASES[case])
    cfg = dict(stimulus=stimulus(rate, seconds), params=params, sample_rate=rate,
               tempo_source=0)
    return render.run_simulation(lib, cfg)["output"]


def per_second(y, rate):
    n = y.shape[0] // rate
    return [dbfs(y[i * rate:(i + 1) * rate]) for i in range(n)]


def spectrum_of(y, rate, start_sec, stop_sec):
    seg = y[int(start_sec * rate):int(stop_sec * rate)].mean(axis=1)
    window = np.hanning(len(seg))
    mag = np.abs(np.fft.rfft(seg * window))
    freqs = np.fft.rfftfreq(len(seg), 1.0 / rate)
    power = mag ** 2
    total = max(float(power.sum()), 1e-30)
    peaks = []
    for idx in np.argsort(power)[-6:][::-1]:
        peaks.append({"hz": round(float(freqs[idx]), 2),
                      "dbfs": round(float(10 * np.log10(max(power[idx], 1e-30))), 2),
                      "share": round(float(power[idx] / total), 4)})
    return {"peaks": peaks, "n": len(seg)}


def classify(seconds_db, peak_per_second):
    tail = np.array(seconds_db[-min(20, len(seconds_db)):], dtype=float)
    slope = float(np.polyfit(np.arange(len(tail)), tail, 1)[0])
    peak = float(max(peak_per_second))
    finite = bool(np.all(np.isfinite(seconds_db)))
    if not finite:
        return "unstable growth", slope
    if slope < -0.5:
        return "decay", slope
    if abs(slope) <= 0.05:
        if peak > 0.5:
            return "limited sustain", slope
        return "limited sustain", slope
    if slope > 0.05:
        return "unstable growth", slope
    return "transitional", slope


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--library", type=Path, default=ROOT / "build-native/libhydra_abi_test_library.dll")
    parser.add_argument("--seconds", type=float, default=60.0)
    parser.add_argument("--rates", type=int, nargs="+", default=[44100, 48000])
    parser.add_argument("--output", type=Path, default=ROOT / "artifacts/phase64/feedback")
    parser.add_argument("--stages", action="store_true", help="Build a diagnostic copy and compare pre-limiter windows at 5 and 55 seconds")
    args = parser.parse_args()

    lib = render.HydraEngineRunner(args.library)
    args.output.mkdir(parents=True, exist_ok=True)
    report = {"library": str(args.library), "seconds": args.seconds, "cases": {}}
    if args.stages and args.seconds != 60:
        parser.error("--stages requires the canonical 60-second run")
    compiler = build.compiler_path()
    report["provenance"] = dict(commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        source_sha256=render.sha256_file(ROOT / 'dsp/core/tape_core.cpp'),
        library_sha256=render.sha256_file(args.library),
        compiler=subprocess.check_output([compiler, '--version'], text=True))
    if args.stages:
        source = args.output / 'tape_core_instrumented.cpp'
        subprocess.run([sys.executable, str(ROOT / 'scripts/instrument_feedback.py'),
                        str(ROOT / 'dsp/core/tape_core.cpp'), str(source)], check=True)
        executable = args.output / ('feedback_probe.exe' if sys.platform == 'win32' else 'feedback_probe')
        command = [compiler, '-O3', '-DNDEBUG', '-std=c++17', '-I' + str(ROOT / 'include'),
                   '-I' + str(ROOT / 'dsp/core'), str(source), str(ROOT / 'dsp/core/hydra_dsp_c_api.cpp'),
                   str(ROOT / 'tests/core/feedback_probe.cpp'), '-o', str(executable)]
        subprocess.run(command, check=True)
        report['stage_build'] = dict(command=command, source_sha256=render.sha256_file(source),
                                    executable_sha256=render.sha256_file(executable))

    for rate in args.rates:
        for case in CASES:
            # Gate reproduction: the exact 6 s shape used by validate_bias_engine.
            gate_y = run(case, rate, 6.0, lib)
            gate_seconds = per_second(gate_y, rate)
            gate_pass = gate_seconds[-1] <= gate_seconds[2]

            # Extended run for classification.
            y = run(case, rate, args.seconds, lib)
            seconds = per_second(y, rate)
            peak_per_second = [float(np.max(np.abs(y[i * rate:(i + 1) * rate])))
                               for i in range(len(seconds))]
            label, slope = classify(seconds, peak_per_second)
            clipped = int(np.count_nonzero(np.abs(y) >= 0.9899))
            entry = {
                "rate": rate, "case": case,
                "gate_seconds_rms_dbfs": [round(v, 3) for v in gate_seconds],
                "gate_last_le_third": bool(gate_pass),
                "gate_delta_db": round(gate_seconds[-1] - gate_seconds[2], 3),
                "extended_seconds_rms_dbfs": [round(v, 3) for v in seconds],
                "extended_peak_per_second": [round(v, 4) for v in peak_per_second],
                "tail_slope_db_per_s": round(slope, 5),
                "classification": label,
                "finite": bool(np.all(np.isfinite(y))),
                "global_peak": float(np.max(np.abs(y))),
                "limiter_ceiling_samples": clipped,
                "spectrum_last_second": spectrum_of(y, rate, args.seconds - 1, args.seconds),
                "spectrum_mid": spectrum_of(y, rate, args.seconds / 2, args.seconds / 2 + 1),
            }
            if args.stages:
                windows = []
                for start in (5, 55):
                    command = [str(executable.resolve()), '--rate', str(rate), '--seconds', '60',
                               '--window-start', str(start), '--feedback', str(CASES[case][13]),
                               '--heads', str(CASES[case].get(16, 1)), '--reverse', str(CASES[case].get(23, 0))]
                    raw = subprocess.check_output(command, text=True)
                    (args.output / f'{rate}-{case}-{start}s.txt').write_text(raw, encoding='utf-8')
                    windows.append(np.array([[float.fromhex(x) for x in row.split()[1:]]
                                              for row in raw.splitlines()[1:]], dtype=np.float64))
                stage_metrics = {}
                for name, column in (('feedback', 4), ('record', 5), ('magnetic_write', 6)):
                    a, b = (window[:, column] for window in windows)
                    ra, rb = (float(np.sqrt(np.mean(v*v))) for v in (a, b))
                    growth = 20 * math.log10(max(rb, 1e-12) / max(ra, 1e-12))
                    stage_metrics[name] = dict(rms_5s=ra, rms_55s=rb, growth_db=growth,
                                              peak_5s=float(np.max(np.abs(a))), peak_55s=float(np.max(np.abs(b))),
                                              finite=bool(np.all(np.isfinite(a)) and np.all(np.isfinite(b))))
                entry['internal_stages'] = stage_metrics
                entry['internal_stable'] = all(v['finite'] and v['growth_db'] <= 0.05 for v in stage_metrics.values())
            report["cases"][f"{rate}/{case}"] = entry
            print(f"{rate}/{case}: gate={'PASS' if gate_pass else 'FAIL'} "
                  f"delta={entry['gate_delta_db']:+.3f}dB class={label} slope={slope:+.5f}dB/s "
                  f"peak={entry['global_peak']:.4f} clipped={clipped}", flush=True)

    (args.output / "feedback-stability.json").write_text(
        json.dumps(report, indent=2, allow_nan=False), encoding="utf-8")
    print("wrote", args.output / "feedback-stability.json")
    if any(not e['finite'] or e['classification'] == 'unstable growth'
           or (args.stages and not e['internal_stable']) for e in report['cases'].values()):
        raise SystemExit('Feedback diagnostic detected instability; see report')


if __name__ == "__main__":
    main()
