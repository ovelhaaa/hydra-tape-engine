"""Render Phase 6.3 before/after Reverse A/B pairs for the existing Listening Lab.

"Before" is built from HEAD (Phase 6.2 candidate), "after" from the working tree.
Both use the same source tree layout, compiler, stimuli, parameters and seeds.
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
import musical_sources
import package_listening_lab
import render_phase6_review as render

ROOT = Path(__file__).resolve().parents[1]
SUITE = "Hydra Phase 6.3 Reverse A/B"
BEFORE_COMMIT = "5e3d2bfa4ee8c5e6e765f7e11b12c6ffe87063c6"  # Phase 6.2 approved DSP
DEFAULT_PARAMS = {0: 8, 1: 6, 3: 25, 4: 10, 11: 1, 12: 500, 13: 35,
                  14: 50, 15: 50, 16: 4, 17: 120, 21: 0, 23: 0,
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
    clock = [{"frame": 0, "status": 0xFA}] + [
        {"frame": i * 1000, "status": 0xF8} for i in range(288)]
    yield ("Reverse 1/4", "clean_guitar", {23: 1, 36: 0, 37: 0, 12: 500, 13: 40}, [], [])
    yield ("Reverse 1/8", "clean_guitar", {23: 1, 36: 0, 37: 3, 12: 500, 13: 40}, [], [])
    yield ("Reverse 1/16", "clean_guitar", {23: 1, 36: 0, 37: 4, 12: 500, 13: 40}, [], [])
    yield ("Reverse high feedback", "clean_guitar", {23: 1, 36: 0, 37: 0, 13: 95}, [], [])
    yield ("Reverse with BPM change", "clean_guitar", {23: 1, 36: 0, 37: 0, 13: 40},
           [{"time_sec": 3.0, "type": "bpm", "bpm": 137.0}], [])
    yield ("Reverse with MIDI clock", "clean_guitar", {23: 1, 36: 0, 37: 0, 13: 40}, [], clock)
    yield ("Multi-Head", "drum_machine", {23: 0, 16: 7, 21: 1, 13: 45}, [], [])
    yield ("Conventional delay", "clean_guitar", {23: 0, 12: 500, 13: 85}, [], [])
    yield ("Warp", "clean_guitar", {23: 0, 30: 1, 31: 0.5, 32: 80, 34: 95}, [], [])
    yield ("Stress combined", "drum_machine",
           {23: 1, 30: 1, 25: 1, 16: 7, 13: 50, 31: 0.5, 32: 80, 34: 90}, [], [])


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
                        default=ROOT / "artifacts/listening-lab/phase6-reverse-optimization")
    parser.add_argument("--archive", type=Path,
                        default=ROOT / "artifacts/hydra-phase6-reverse-ab.zip")
    parser.add_argument("--work", type=Path, default=ROOT / "artifacts/tmp/phase63-before")
    parser.add_argument("--before-commit", default=BEFORE_COMMIT)
    parser.add_argument("--after-commit", help="Freeze the optimized historical DSP; omitted uses working tree")
    args = parser.parse_args()

    extract_commit(args.before_commit, args.work)
    build_dir = args.work / "build"
    build_dir.mkdir(exist_ok=True)
    before_lib = build_dir / "before.dll"
    after_lib = build_dir / "after.dll"
    before_hash, before_cmd = build_library(args.work, before_lib)
    after_root = ROOT
    if args.after_commit:
        after_root = args.work / 'optimized-source'
        extract_commit(args.after_commit, after_root)
    after_hash, after_cmd = build_library(after_root, after_lib)
    before = render.HydraEngineRunner(before_lib)
    after = render.HydraEngineRunner(after_lib)
    if before.api_version != after.api_version:
        raise RuntimeError("API version mismatch between before and after")

    if args.output.exists():
        shutil.rmtree(args.output)
    args.output.mkdir(parents=True)
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    manifest = dict(
        schema_version=1, suite=SUITE, reference_commit=commit,
        before_commit=args.before_commit,
        after_commit=args.after_commit or commit,
        api_version=after.api_version,
        native_library_sha256=before_hash,
        working_tree_dirty=bool(subprocess.check_output(
            ["git", "status", "--porcelain", "--untracked-files=no"], cwd=ROOT, text=True).strip()),
        source_sha256=render.sha256_file(after_root / "dsp/core/tape_core.cpp"),
        before_library_sha256=before_hash, after_library_sha256=after_hash,
        environment=dict(os=platform.platform(), python=platform.python_version(),
                         numpy=np.__version__),
        random_seed=6300, sample_rate_hz=render.RATE, block_size_frames=render.BLOCK,
        human_listening_signoff="PENDING_HUMAN_AUDITION", scenarios={})
    comparisons = {}
    for index, (title, source, params, events, clock) in enumerate(cases(), 1):
        seed = 6300 + index
        np.random.seed(seed)
        stimulus = musical_sources.get_musical_source(source, 6, render.RATE, bpm=120)
        config = dict(stimulus=stimulus, params=params, initial_bpm=120,
                      tempo_source=2 if clock else 0, events=events, clock_pulses=clock,
                      sample_rate=render.RATE)
        before_result = render.run_simulation(before, config)
        after_result = render.run_simulation(after, config)
        comparison = metrics(before_result["output"], after_result["output"])
        comparisons[title] = comparison
        key = f"{index:02d}_reverse_{index:02d}"
        typical = dict(DEFAULT_PARAMS)
        typical.update(params)
        entry = dict(
            name=f"{title}: Phase 6.2 vs Phase 6.3", group="Reverse A/B — optimization",
            description="A / Original = Phase 6.2 output; B / Processed = Phase 6.3 output. "
                        "Identical source, gain, BPM, events and seed; no normalization.",
            duration_sec=6.0, bpm=120.0, tempo_source=2 if clock else 0,
            reference_commit=commit, random_seed=seed, events=events, clock_pulses=clock,
            sample_rate_hz=render.RATE, source_material=source,
            comparison_variant="optimized", comparison_case=index, comparison_title=title,
            human_review_state="NOT REVIEWED",
            stimulus_sha256=hashlib.sha256(stimulus.tobytes()).hexdigest(),
            dsp_params={str(k): float(v) for k, v in after_result["parameters"].items()},
            markers=[dict(time=0.0, label="Audio start"), dict(time=6.0, label="Audio end")],
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
        print(key, comparison, flush=True)
    (args.output / "manifest.json").write_text(
        json.dumps(manifest, indent=2, allow_nan=False), encoding="utf-8")
    reports = args.output / "reports"
    reports.mkdir(exist_ok=True)
    (reports / "comparison.json").write_text(json.dumps(
        {"before_commit": args.before_commit,
         "after_commit": args.after_commit or commit,
         "before_library_sha256": before_hash, "after_library_sha256": after_hash,
         "before_build": before_cmd, "after_build": after_cmd,
         "comparisons": comparisons}, indent=2), encoding="utf-8")
    package_listening_lab.package(args.output, args.archive)


if __name__ == "__main__":
    main()
