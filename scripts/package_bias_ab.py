"""Render controlled bias comparisons for the existing offline Listening Lab."""
import argparse
import csv
import hashlib
import json
import platform
import subprocess
from pathlib import Path

import numpy as np
import musical_sources
import package_listening_lab
import render_phase6_review as render

ROOT = Path(__file__).resolve().parents[1]
SUITE = "Hydra Phase 6.2 Bias A/B"
VARIANTS = ("bias_off", "sine", "bandlimited", "oversampled", "averaged")


def cases():
    # Each pair shares these exact parameters, stimulus, seed, events and duration.
    yield "Clean guitar isolated notes", "clean_guitar", {}, "notes"
    yield "Clean guitar chords", "clean_guitar", {}, "chords"
    yield "Driven guitar", "driven_guitar", {3: 100}, None
    yield "Monophonic synthesizer", "mono_synth", {}, None
    yield "Polyphonic synthesizer", "poly_synth", {}, None
    yield "Drum machine", "drum_machine", {}, None
    yield "Moderate feedback", "clean_guitar", {13: 45}, "tail"
    yield "High feedback", "clean_guitar", {13: 95}, "tail"
    yield "Reverse V2", "clean_guitar", {23: 1, 36: 0}, None
    yield "Multi-Head", "drum_machine", {16: 7}, None
    yield "Digital silence with hiss", "silence", {4: 30}, None
    yield "440 Hz sine", "sine440", {4: 0, 0: 0, 1: 0}, None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--libraries", type=Path, default=ROOT / "artifacts/bias-antialiasing/libraries.json")
    parser.add_argument("--output", type=Path, default=ROOT / "artifacts/listening-lab/phase6-bias-ab")
    parser.add_argument("--archive", type=Path)
    args = parser.parse_args()
    libraries = json.loads(args.libraries.read_text(encoding="utf-8"))
    runners = {name: render.HydraEngineRunner(Path(libraries[name])) for name in ("baseline", *VARIANTS)}
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    manifest = dict(schema_version=1, suite=SUITE, reference_commit=commit,
                    api_version=runners["baseline"].api_version,
                    native_library_sha256=runners["baseline"].library_sha256,
                    working_tree_dirty=bool(subprocess.check_output(["git", "status", "--porcelain", "--untracked-files=no"], cwd=ROOT, text=True).strip()),
                    source_sha256=render.sha256_file(ROOT / "dsp/core/tape_core.cpp"),
                    baseline_model_sha256=render.sha256_file(ROOT / "dsp/diagnostics/tape_bias_baseline.inc"),
                    variant_library_sha256={name: runner.library_sha256 for name, runner in runners.items()},
                    environment=dict(os=platform.platform(), python=platform.python_version(), numpy=np.__version__),
                    random_seed=6200, sample_rate_hz=render.RATE, block_size_frames=render.BLOCK,
                    human_listening_signoff="PENDING_HUMAN_AUDITION", scenarios={})
    args.output.mkdir(parents=True, exist_ok=True)
    for index, (title, source, params, shape) in enumerate(cases(), 1):
        seed = 6200 + index
        np.random.seed(seed)
        t = np.arange(6 * render.RATE) / render.RATE
        if source == "silence":
            stimulus = np.zeros((len(t), 2), dtype=np.float32)
        elif source == "sine440":
            stimulus = np.repeat((.1 * np.sin(2 * np.pi * 440 * t))[:, None], 2, axis=1).astype(np.float32)
        else:
            stimulus = musical_sources.get_musical_source(source, 6, render.RATE, bpm=120)
        if shape == "notes":
            stimulus[(t % 1) >= .3] = 0
        elif shape == "chords":
            # Transpose the plucked source by a major third and fifth with the
            # same interpolation/gain for all variants; repeat it to fill six seconds.
            positions = np.arange(len(stimulus), dtype=float)
            voices = [stimulus]
            for semitones in (4, 7):
                phase = positions * 2 ** (semitones / 12) % len(stimulus)
                voices.append(np.column_stack([np.interp(phase, positions, stimulus[:, channel]) for channel in range(2)]))
            stimulus = np.mean(voices, axis=0).astype(np.float32)
        elif shape == "tail":
            stimulus[t >= 2] = 0
        config = dict(stimulus=stimulus, params=params, initial_bpm=120, tempo_source=0, events=[])
        baseline = render.run_simulation(runners["baseline"], config)
        for variant in VARIANTS:
            result = render.run_simulation(runners[variant], config)
            number = len(manifest["scenarios"]) + 1
            key = f"{number:02d}_{index:02d}_{variant}"
            entry = dict(name=f"{title}: baseline vs {variant}", group=f"Bias A/B — {variant}",
                         description="A / Original = triangular baseline output; B / Processed = variant output. Same gain; no normalization.",
                         duration_sec=6.0, bpm=120.0, tempo_source=0, reference_commit=commit,
                         random_seed=seed, events=[], clock_pulses=[], sample_rate_hz=render.RATE,
                         source_material=source, comparison_variant=variant, human_review_state="NOT REVIEWED",
                         stimulus_sha256=hashlib.sha256(stimulus.tobytes()).hexdigest(),
                         dsp_params={str(k): float(v) for k, v in result["parameters"].items()},
                         markers=[dict(time=0.0, label="Audio start"), dict(time=6.0, label="Audio end")],
                         analysis_metrics=dict(baseline_rms=float(np.sqrt(np.mean(baseline["output"].astype(float)**2))),
                                               variant_rms=float(np.sqrt(np.mean(result["output"].astype(float)**2))),
                                               variant_peak=float(np.max(np.abs(result["output"])))) )
            if shape == "tail":
                entry["markers"].append(dict(time=2.0, label="Input silenced"))
            for kind, audio in (("original_wav", baseline["output"]), ("processed_wav", result["output"])):
                path = args.output / f"{key}_{kind}.wav"
                render.write_wav_24bit(path, audio, render.RATE)
                entry[kind + "_file"] = path.name
                entry[kind + "_sha256"] = render.sha256_file(path)
            path = args.output / f"{key}_telemetry.csv"
            with path.open("w", encoding="utf-8", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=list(result["telemetry"][0]))
                writer.writeheader()
                writer.writerows(result["telemetry"])
            entry["telemetry_csv_file"] = path.name
            entry["telemetry_csv_sha256"] = render.sha256_file(path)
            manifest["scenarios"][key] = entry
            print(key, flush=True)
    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2, allow_nan=False), encoding="utf-8")
    package_listening_lab.package(args.output, args.archive)


if __name__ == "__main__":
    main()
