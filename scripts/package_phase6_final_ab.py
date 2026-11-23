"""Render the Phase 6.4 final before/after A/B pairs for the existing Listening Lab.

"Before" is the last human-approved package (7431a6d, Phase 6.3), "after" is the
working tree with the Phase 6.4 numerical and transition fixes. Both are built
from source with the same compiler, stimuli, gains, seeds, sample rate and
events; nothing is normalised or gain-matched.
"""
import argparse
import csv
import hashlib
import io
import json
import platform
import shutil
import subprocess
import sys
import tarfile
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import musical_sources  # noqa: E402
import package_listening_lab  # noqa: E402
import render_phase6_review as render  # noqa: E402

SUITE = "Hydra Phase 6.4 Final A/B"
BEFORE_COMMIT = "7431a6db05abfde97838721cd089f94c7fed5047"
DEFAULT_PARAMS = {0: 8, 1: 6, 3: 25, 4: 10, 5: 50, 6: 50, 11: 1, 12: 500, 13: 35,
                  14: 50, 15: 50, 16: 4, 17: 120, 20: 50, 21: 0, 23: 0,
                  25: 0, 30: 0, 36: 0, 37: 0}


def compiler_path():
    cache = (ROOT / "build-native/CMakeCache.txt").read_text(encoding="utf-8")
    for line in cache.splitlines():
        if line.startswith("CMAKE_CXX_COMPILER:FILEPATH="):
            return line.split("=", 1)[1]
    raise RuntimeError("compiler not found in CMakeCache")


def extract_commit(commit, destination):
    if destination.exists():
        shutil.rmtree(destination)
    destination.mkdir(parents=True)
    data = subprocess.check_output(["git", "archive", commit], cwd=ROOT)
    with tarfile.open(fileobj=io.BytesIO(data)) as archive:
        try:
            archive.extractall(destination, filter="data")
        except TypeError:  # Python < 3.12
            archive.extractall(destination)


def build_library(root, output):
    compiler = compiler_path()
    command = [compiler, "-O3", "-DNDEBUG", "-std=c++17", "-shared", "-fPIC"]
    if sys.platform == "win32":
        command.append("-Wl,--export-all-symbols")
    command += [f"-I{root / 'include'}", f"-I{root / 'dsp/core'}",
                str(root / "dsp/core/tape_core.cpp"),
                str(root / "dsp/core/hydra_dsp_c_api.cpp"), "-o", str(output)]
    subprocess.run(command, check=True, capture_output=True)
    return hashlib.sha256(Path(output).read_bytes()).hexdigest(), command


def cases():
    """The ten priority scenarios of Phase 6.4, in the order the reviewer hears them."""
    yield ("Clean guitar", "clean_guitar", {3: 25}, [], 6.0)
    yield ("Driven guitar", "clean_guitar", {3: 90, 6: 100}, [], 6.0)
    yield ("Conventional delay", "clean_guitar", {12: 420, 13: 60}, [], 6.0)
    yield ("Reverse 1/4", "clean_guitar", {23: 1, 36: 0, 37: 0, 12: 500, 13: 40}, [], 6.0)
    yield ("Reverse 1/8", "clean_guitar", {23: 1, 36: 0, 37: 3, 12: 500, 13: 40}, [], 6.0)
    yield ("Reverse high feedback", "clean_guitar", {23: 1, 36: 0, 37: 0, 13: 95}, [], 6.0)
    yield ("Multi-Head", "drum_machine", {16: 7, 21: 1, 13: 45}, [], 6.0)
    yield ("Tape speed transition", "clean_guitar", {3: 40},
           [{"time_sec": 1.0, "type": "param", "id": 5, "value": 70.0},
            {"time_sec": 2.5, "type": "param", "id": 5, "value": 30.0}], 6.0)
    yield ("BPM change", "clean_guitar", {12: 500, 13: 40},
           [{"time_sec": 1.5, "type": "bpm", "bpm": 140.0},
            {"time_sec": 3.0, "type": "bpm", "bpm": 100.0}], 6.0)
    yield ("Long feedback", "clean_guitar", {13: 88},
           [{"time_sec": 1.0, "type": "param", "id": 4, "value": 0.0}], 6.0)


def metrics(before, after):
    delta = after.astype(np.float64) - before.astype(np.float64)
    max_abs = float(np.max(np.abs(delta))) if delta.size else 0.0
    rmse = float(np.sqrt(np.mean(delta ** 2))) if delta.size else 0.0
    env_before = np.abs(before).max(axis=1)
    env_after = np.abs(after).max(axis=1)
    envelope = float(np.max(np.abs(env_after - env_before))) if delta.size else 0.0
    return {"max_abs": max_abs, "rmse": rmse, "envelope_max_abs": envelope,
            "bit_exact": bool(np.array_equal(before, after))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path,
                        default=ROOT / "artifacts/listening-lab/phase6-final-review")
    parser.add_argument("--archive", type=Path,
                        default=ROOT / "artifacts/hydra-phase6-final-ab.zip")
    parser.add_argument("--work", type=Path, default=ROOT / "artifacts/tmp/phase64-before-ab")
    parser.add_argument("--before-commit", default=BEFORE_COMMIT)
    args = parser.parse_args()

    extract_commit(args.before_commit, args.work)
    build_dir = args.work / "build"
    build_dir.mkdir(exist_ok=True)
    before_lib = build_dir / "before.dll"
    after_lib = build_dir / "after.dll"
    before_hash, before_cmd = build_library(args.work, before_lib)
    after_hash, after_cmd = build_library(ROOT, after_lib)
    before = render.HydraEngineRunner(before_lib)
    after = render.HydraEngineRunner(after_lib)
    if (before.api_version >> 16) != (after.api_version >> 16):
        raise RuntimeError("API version mismatch between before and after")

    if args.output.exists():
        shutil.rmtree(args.output)
    args.output.mkdir(parents=True)
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    manifest = dict(
        schema_version=1, suite=SUITE, reference_commit=commit,
        before_commit=args.before_commit, api_version=after.api_version,
        working_tree_dirty=bool(subprocess.check_output(
            ["git", "status", "--porcelain", "--untracked-files=no"], cwd=ROOT, text=True).strip()),
        source_sha256=render.sha256_file(ROOT / "dsp/core/tape_core.cpp"),
        tanh_table_sha256=render.sha256_file(ROOT / "dsp/core/fast_tanh_table.inc"),
        before_library_sha256=before_hash, after_library_sha256=after_hash,
        environment=dict(os=platform.platform(), python=platform.python_version(),
                         numpy=np.__version__),
        random_seed=6400, sample_rate_hz=render.RATE, block_size_frames=render.BLOCK,
        human_listening_signoff="PENDING_HUMAN_AUDITION", scenarios={})
    comparisons = {}
    for index, (title, source, params, events, seconds) in enumerate(cases(), 1):
        seed = 6400 + index
        np.random.seed(seed)
        stimulus = musical_sources.get_musical_source(source, seconds, render.RATE, bpm=120)
        config = dict(stimulus=stimulus, params=params, initial_bpm=120, tempo_source=0,
                      events=events, clock_pulses=[], sample_rate=render.RATE)
        before_result = render.run_simulation(before, config)
        after_result = render.run_simulation(after, config)
        comparison = metrics(before_result["output"], after_result["output"])
        comparisons[title] = comparison
        key = f"{index:02d}_final_{index:02d}"
        markers = [dict(time=0.0, label="Audio start"), dict(time=float(seconds), label="Audio end")]
        markers += [dict(time=float(e["time_sec"]), label=f"Automation: {e['type']}") for e in events]
        entry = dict(
            name=f"{title}: Phase 6.3 approved vs Phase 6.4 candidate",
            group="Phase 6.4 final A/B",
            description="A / Original = last approved Phase 6.3 output (7431a6d); "
                        "B / Processed = Phase 6.4 candidate. Identical source, gain, BPM, "
                        "events, seed and sample rate; no normalization.",
            duration_sec=float(seconds), bpm=120.0, tempo_source=0,
            reference_commit=commit, random_seed=seed, events=events, clock_pulses=[],
            sample_rate_hz=render.RATE, source_material=source,
            comparison_variant="final", comparison_case=index, comparison_title=title,
            human_review_state="NOT REVIEWED",
            stimulus_sha256=hashlib.sha256(stimulus.tobytes()).hexdigest(),
            dsp_params={str(k): float(v) for k, v in after_result["parameters"].items()},
            markers=markers,
            analysis_metrics=dict(
                before_rms=float(np.sqrt(np.mean(before_result["output"].astype(float) ** 2))),
                after_rms=float(np.sqrt(np.mean(after_result["output"].astype(float) ** 2))),
                after_peak=float(np.max(np.abs(after_result["output"]))),
                **comparison))
        for kind, audio in (("original_wav", before_result["output"]),
                            ("processed_wav", after_result["output"])):
            path = args.output / f"{key}_{kind}.wav"
            render.write_wav_24bit(path, audio, render.RATE)
            entry[kind + "_file"] = path.name
            entry[kind + "_sha256"] = render.sha256_file(path)
        path = args.output / f"{key}_telemetry.csv"
        with path.open("w", encoding="utf-8", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(after_result["telemetry"][0]))
            writer.writeheader()
            writer.writerows(after_result["telemetry"])
        entry["telemetry_csv_file"] = path.name
        entry["telemetry_csv_sha256"] = render.sha256_file(path)
        manifest["scenarios"][key] = entry
        print(key, title, comparison, flush=True)

    (args.output / "manifest.json").write_text(
        json.dumps(manifest, indent=2, allow_nan=False), encoding="utf-8")
    reports = args.output / "reports"
    reports.mkdir(exist_ok=True)
    (reports / "comparison.json").write_text(json.dumps(
        {"before_commit": args.before_commit,
         "before_library_sha256": before_hash, "after_library_sha256": after_hash,
         "before_build": before_cmd, "after_build": after_cmd,
         "comparisons": comparisons}, indent=2), encoding="utf-8")
    package_listening_lab.package(args.output, args.archive)


if __name__ == "__main__":
    main()
