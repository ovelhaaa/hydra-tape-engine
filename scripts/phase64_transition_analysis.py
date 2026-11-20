#!/usr/bin/env python3
"""Phase 6.4 parameter-transition analysis at 44.1 kHz and 48 kHz.

Reproduces the `parameter_steps` gate from scripts/validate_bias_engine.py and
attributes the measured step to individual parameter events.

Gate: for each event second, take y[center-256 : center+512] and measure
max |diff|. Limit 0.03. The stimulus is a continuous 110 Hz tone at 0.02 so
the ordinary waveform slope is ~3e-4, far below the measured step.
"""
import argparse
import json
from pathlib import Path
import sys

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import render_phase6_review as render  # noqa: E402

BASE_PARAMS = {0: 0, 1: 0, 2: 0, 3: 25, 4: 0, 5: 50, 6: 50, 7: 0, 8: 0, 11: 1,
               12: 125, 13: 35, 14: 100, 15: 100, 16: 1, 19: 0, 20: 100,
               23: 0, 25: 0, 30: 0}
EVENTS = [
    {"time_sec": 2, "type": "param", "id": 3, "value": 100, "label": "drive 25->100"},
    {"time_sec": 3, "type": "param", "id": 5, "value": 0, "label": "tape speed 50->0"},
    {"time_sec": 4, "type": "param", "id": 6, "value": 100, "label": "tape age 50->100"},
    {"time_sec": 5, "type": "param", "id": 20, "value": 0, "label": "tone 100->0"},
    {"time_sec": 6, "type": "param", "id": 7, "value": 100, "label": "head bump 0->100"},
]


def stimulus(rate, seconds=6.0):
    t = np.arange(int(seconds * rate)) / rate
    return np.repeat((0.02 * np.sin(2 * np.pi * 110 * t))[:, None], 2, axis=1).astype(np.float32)


def run(lib, rate, events, seconds=8.0):
    cfg = dict(stimulus=stimulus(rate, seconds), params=BASE_PARAMS, sample_rate=rate,
               tempo_source=0, events=events)
    return render.run_simulation(lib, cfg)["output"]


def gate_step(y, rate, second):
    center = int(second * rate)
    window = y[center - 256:center + 512]
    d = np.abs(np.diff(window.astype(np.float64), axis=0))
    idx = int(np.unravel_index(np.argmax(d), d.shape)[0])
    return float(d.max()), center - 256 + idx


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--library", type=Path, default=ROOT / "build-native/libhydra_abi_test_library.dll")
    parser.add_argument("--output", type=Path, default=ROOT / "artifacts/phase64/transitions")
    args = parser.parse_args()
    lib = render.HydraEngineRunner(args.library)
    args.output.mkdir(parents=True, exist_ok=True)
    report = {"cases": {}}

    for rate in (44100, 48000):
        old = render.RATE
        render.RATE = rate
        try:
            control = run(lib, rate, [])
            full = run(lib, rate, EVENTS)
            entry = {"rate": rate, "all_events": {}, "isolated": {}, "control": {}}
            for ev in EVENTS:
                step, sample = gate_step(full, rate, ev["time_sec"])
                cstep, _ = gate_step(control, rate, ev["time_sec"])
                # local waveform context around the boundary
                center = int(ev["time_sec"] * rate)
                before = full[center - 3:center + 4, 0].astype(float).tolist()
                entry["all_events"][ev["label"]] = {
                    "gate_step": step, "sample": sample,
                    "control_gate_step": cstep, "rate": rate,
                    "samples_around_boundary": before,
                    "local_abs_peak": float(np.max(np.abs(full[center - rate // 50:center + rate // 50]))),
                }
            for ev in EVENTS:
                y = run(lib, rate, [ev])
                step, sample = gate_step(y, rate, ev["time_sec"])
                center = int(ev["time_sec"] * rate)
                entry["isolated"][ev["label"]] = {
                    "gate_step": step, "sample": sample,
                    "samples_around_boundary": y[center - 3:center + 4, 0].astype(float).tolist(),
                    "delta_vs_control_at_boundary": float(np.max(np.abs(y[center] - control[center]))),
                }
            entry["control"]["gate_step_max_over_event_seconds"] = float(
                max(gate_step(control, rate, ev["time_sec"])[0] for ev in EVENTS))
            report["cases"][str(rate)] = entry
            best = max(entry["all_events"].items(), key=lambda kv: kv[1]["gate_step"])
            print(f"{rate}: max step {best[1]['gate_step']:.5f} at '{best[0]}' "
                  f"(control {best[1]['control_gate_step']:.5f}); "
                  f"isolated: " + ", ".join(f"{k.split()[0]}={v['gate_step']:.5f}"
                                            for k, v in entry["isolated"].items()), flush=True)
        finally:
            render.RATE = old

    (args.output / "transitions.json").write_text(json.dumps(report, indent=2, allow_nan=False),
                                                  encoding="utf-8")
    print("wrote", args.output / "transitions.json")
    failures = [f"{rate}/{mode}/{label}" for rate, entry in report["cases"].items()
                for mode in ("all_events", "isolated")
                for label, value in entry[mode].items()
                if not np.isfinite(value["gate_step"]) or value["gate_step"] > 0.03]
    if failures:
        raise SystemExit("Transition step exceeds unchanged 0.03 limit: " + ", ".join(failures))


if __name__ == "__main__":
    main()
