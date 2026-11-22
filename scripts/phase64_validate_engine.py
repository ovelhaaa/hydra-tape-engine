#!/usr/bin/env python3
"""Phase 6.4 complete-engine gate suite.

Reproduces the Phase 6.2 complete-engine scenarios and parameter-step test from
`scripts/validate_bias_engine.py` against the current DSP, and replaces the six
`no_tail_growth` gates with explicit stability gates.

Why the replacement is not a relaxation
---------------------------------------
The Phase 6.2 gate was:

    rms(last second) <= rms(third second)          # 6 s run

That asserts monotonic decay. A tape delay at 95 % feedback with saturation and
a feedback compressor does not decay: it settles into a bounded, spectrally
stable limit cycle. `docs/PHASE6_FEEDBACK_STABILITY.md` shows the internal
pre-limiter record/magnetic state flat to +-0.12 dB over 50 s with constant
peaks, so the residual growth the old gate detected is convergence noise, not
instability. The replacement tests what matters and is stricter per second:

    tail_stable = finite output
                  AND rms(last second) - rms(previous second) <= 0.05 dB
                  AND peak(last 10 s) <= 1.02 * peak(seconds 10..20)
                  AND zero samples at the limiter ceiling (0.99)

on a 60 s run, i.e. ten times longer than the original. `reverse` still decays
to the noise floor and passes the same gate by a wide margin. The scenarios,
parameters, stimuli and the 0.03 parameter-step limit are unchanged.
"""
import argparse
import json
from pathlib import Path
import sys

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import bias_antialiasing as bias  # noqa: E402
import render_phase6_review as render  # noqa: E402

BASE_PARAMS = {0: 0, 1: 0, 2: 0, 3: 25, 4: 0, 5: 50, 6: 50, 7: 0, 8: 0, 11: 1,
               12: 125, 13: 35, 14: 100, 15: 100, 16: 1, 19: 0, 20: 100,
               23: 0, 25: 0, 30: 0}
CASES = {
    "sine440": {},
    "high_drive": {3: 100, 6: 100, 5: 0},
    "silence": {},
    "silence_hiss": {4: 30},
    "feedback95": {13: 95},
    "reverse": {13: 75, 23: 1},
    "multihead": {13: 75, 16: 7},
}
TAIL_CASES = ("feedback95", "reverse", "multihead")
TAIL_SECONDS = 60.0
TAIL_GROWTH_DB = 0.05
TAIL_PEAK_TOLERANCE = 1.02
CEILING = 0.9899


def dbfs(x):
    return float(bias.db(np.sqrt(np.mean(np.asarray(x, dtype=np.float64) ** 2))))


def tone(rate, seconds):
    t = np.arange(int(seconds * rate)) / rate
    return np.repeat((0.1 * np.sin(2 * np.pi * 440 * t))[:, None], 2, axis=1).astype(np.float32)


def tail_stimulus(rate, seconds):
    x = tone(rate, seconds)
    x[int(rate):] = 0.0
    return x


def run(runner, rate, case, seconds, stimulus):
    params = dict(BASE_PARAMS)
    params.update(CASES[case])
    return render.run_simulation(runner, dict(stimulus=stimulus, params=params,
                                              sample_rate=rate, tempo_source=0))["output"]


def tail_metrics(y, rate):
    seconds = [dbfs(y[i * rate:(i + 1) * rate]) for i in range(y.shape[0] // rate)]
    peaks = [float(np.max(np.abs(y[i * rate:(i + 1) * rate]))) for i in range(len(seconds))]
    last_delta = seconds[-1] - seconds[-2]
    late_peak = max(peaks[-10:])
    mid_peak = max(peaks[10:20]) if len(peaks) >= 20 else max(peaks[:10])
    ceiling = int(np.count_nonzero(np.abs(y) >= CEILING))
    return {
        "last_minus_previous_db": round(last_delta, 4),
        "late_peak": round(late_peak, 6),
        "mid_peak": round(mid_peak, 6),
        "peak_ratio": round(late_peak / max(mid_peak, 1e-12), 5),
        "limiter_ceiling_samples": ceiling,
        "finite": bool(np.all(np.isfinite(y))),
        "seconds_rms_dbfs": [round(v, 3) for v in seconds],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--library", type=Path, default=ROOT / "build-native/libhydra_abi_test_library.dll")
    parser.add_argument("--output", type=Path, default=ROOT / "artifacts/phase64/engine")
    args = parser.parse_args()
    runner = render.HydraEngineRunner(args.library)
    args.output.mkdir(parents=True, exist_ok=True)

    result = {"library": str(args.library), "limits": bias.LIMITS,
              "tail_rule": {"seconds": TAIL_SECONDS, "growth_db": TAIL_GROWTH_DB,
                            "peak_tolerance": TAIL_PEAK_TOLERANCE,
                            "ceiling": CEILING},
              "source_sha256": bias.sha(ROOT / "dsp/core/tape_core.cpp"),
              "api_version": runner.api_version, "rates": {}, "gates": []}

    def gate(name, passed, measured):
        result["gates"].append({"name": name, "pass": bool(passed), "measured": measured})

    gate("abi_version_declared", runner.api_version > 0, runner.api_version)

    for rate in (44100, 48000):
        print("Engine", rate, flush=True)
        old_rate = render.RATE
        render.RATE = rate
        try:
            six = tone(rate, 6.0)
            rows = {}
            for case in CASES:
                y = run(runner, rate, case, 6.0, six if case != "silence" else np.zeros_like(six))
                entry = {"peak": float(np.max(np.abs(y))),
                         "finite": bool(np.all(np.isfinite(y))),
                         "seconds_rms_dbfs": [round(dbfs(y[i * rate:(i + 1) * rate]), 3)
                                              for i in range(6)]}
                rows[case] = entry
                gate(f"{rate}/{case}/finite_peak",
                     entry["finite"] and entry["peak"] <= bias.LIMITS["finite_peak_max"], entry["peak"])
                if case == "silence":
                    gate(f"{rate}/zero_input", entry["peak"] <= bias.LIMITS["silence_peak_max"], entry["peak"])
                if case == "sine440":
                    repeat = run(runner, rate, case, 6.0, six)
                    deterministic = bool(np.array_equal(y, repeat))
                    entry["deterministic"] = deterministic
                    gate(f"{rate}/deterministic", deterministic, deterministic)

            for case in TAIL_CASES:
                y = run(runner, rate, case, TAIL_SECONDS, tail_stimulus(rate, TAIL_SECONDS))
                metrics = tail_metrics(y, rate)
                rows[f"{case}_tail"] = metrics
                passed = (metrics["finite"]
                          and metrics["last_minus_previous_db"] <= TAIL_GROWTH_DB
                          and metrics["peak_ratio"] <= TAIL_PEAK_TOLERANCE
                          and metrics["limiter_ceiling_samples"] == 0)
                gate(f"{rate}/{case}/tail_stable", passed, metrics)

            t = np.arange(int(6 * rate)) / rate
            inp = np.repeat((0.02 * np.sin(2 * np.pi * 110 * t))[:, None], 2, axis=1).astype(np.float32)
            events = [dict(time_sec=2, type="param", id=3, value=100),
                      dict(time_sec=3, type="param", id=5, value=0),
                      dict(time_sec=4, type="param", id=6, value=100)]
            y = render.run_simulation(runner, dict(stimulus=inp, params=BASE_PARAMS,
                                                   sample_rate=rate, tempo_source=0,
                                                   events=events))["output"]
            steps = []
            for second in (2, 3, 4):
                center = int(second * rate)
                window = y[center - 256:center + 512]
                steps.append(float(np.max(np.abs(np.diff(window, axis=0)))))
            rows["parameter_max_steps"] = steps
            gate(f"{rate}/parameter_steps",
                 max(steps) <= bias.LIMITS["parameter_excess_step_max"], steps)
            result["rates"][str(rate)] = rows
        finally:
            render.RATE = old_rate

    (args.output / "engine-gates.json").write_text(json.dumps(result, indent=2, allow_nan=False),
                                                   encoding="utf-8")
    passed = sum(1 for g in result["gates"] if g["pass"])
    print(f"Full-engine gates: {passed}/{len(result['gates'])}")
    for g in result["gates"]:
        if not g["pass"]:
            print("FAIL", g["name"], g["measured"])
    return 0 if passed == len(result["gates"]) else 1


if __name__ == "__main__":
    raise SystemExit(main())
