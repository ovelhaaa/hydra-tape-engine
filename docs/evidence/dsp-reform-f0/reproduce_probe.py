"""Diagnostic attempt, NOT the missing original F0 render recipe.

Reconstruct seven known stimuli, apply the cited Phase 6 runner defaults, and
report differences. Never updates historical hashes or overwrites an output dir.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, required=True,
                        help="Checkout of the historical main source (6d94af5)")
    parser.add_argument("--library", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True, help="New directory only")
    args = parser.parse_args()
    sys.path.insert(0, str(args.source_root.resolve() / "scripts"))
    import numpy as np
    import musical_sources
    import render_phase6_review as render

    args.output.mkdir(parents=True, exist_ok=False)
    manifest = json.loads(Path(__file__).with_name("baseline_renders_manifest.json").read_text(encoding="utf-8"))
    render.RATE = manifest["sample_rate"]
    runner = render.HydraEngineRunner(args.library.resolve())
    rows = []
    for name, scene in manifest["scenarios"].items():
        if scene["source_kind"] == "impulse":
            continue  # Unknown historical recipe: do not invent it.
        np.random.seed(manifest["seed"])
        stimulus = (np.zeros((int(scene["duration_sec"] * render.RATE), 2), np.float32)
                    if scene["source_kind"] == "silence" else
                    musical_sources.get_musical_source(scene["source_kind"], scene["duration_sec"], render.RATE))
        result = render.run_simulation(runner, {
            "sample_rate": render.RATE, "stimulus": stimulus,
            "params": {int(k): v for k, v in scene["params_overrides"].items()},
        })
        wav = args.output / (name + ".wav")
        render.write_wav_24bit(wav, result["output"], render.RATE)
        rows.append({"scenario": name,
                     "stimulus_match": hashlib.sha256(stimulus.tobytes()).hexdigest() == scene["stimulus_sha256"],
                     "wav_match": render.sha256_file(wav) == scene["output_wav_sha256"],
                     "actual_wav_sha256": render.sha256_file(wav),
                     "effective_parameters_probe": result["parameters"]})
    report = {"diagnostic_only": True, "numpy_version": np.__version__,
              "library_sha256": runner.library_sha256, "rows": rows}
    (args.output / "probe.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0 if all(row["wav_match"] and row["stimulus_match"] for row in rows) else 1


if __name__ == "__main__":
    raise SystemExit(main())
