"""Generate Phase 7.1 A/B audio against the approved Phase 6.6 commit.

A = frozen Phase 6.6 candidate; B = Phase 7.1 candidate. Both use exact
source/seed/parameters/event schedule and the same compiler/flags. Neither
render is normalised. This package NEVER implies human musical acceptance.
"""
import argparse
import csv
import hashlib
import json
import platform
import shutil
import subprocess
from pathlib import Path

import numpy as np
import musical_sources
import package_listening_lab
import package_reverse_ab as common
import render_phase6_review as render

ROOT = Path(__file__).resolve().parents[1]
BASELINE = "b3349866b3c8d6c08ccfc4b435a5d89255901686"
SUITE = "Hydra Phase 7.1 DSP Integrity A/B"

# Param IDs follow the frozen C API. All cases use deterministic stereo musical
# stimuli; numeric domain extremes are tested separately and are not passed
# to the player if the old engine is unstable.
CASES = [
    ("Clean guitar", "clean_guitar", {3: 25, 13: 35}, [], 6.0),
    ("Bright tape safe edge", "clean_guitar",
     {5: 55, 6: 0, 20: 50, 13: 40}, [], 6.0),
    ("Aged tape repeats", "clean_guitar",
     {5: 60, 6: 75, 20: 25, 13: 65}, [], 6.0),
    ("Driven guitar", "driven_guitar", {3: 90, 6: 85, 13: 50}, [], 6.0),
    ("Conventional delay", "clean_guitar", {12: 420, 13: 65}, [], 6.0),
    ("Feedback 95", "clean_guitar", {12: 500, 13: 95}, [], 6.0),
    ("Ping-pong Multi-Head", "drum_machine",
     {16: 7, 21: 1, 13: 75}, [], 6.0),
    ("Reverse 1/4", "clean_guitar",
     {23: 1, 36: 0, 37: 0, 12: 500, 13: 65}, [], 6.0),
    ("Warp feedback", "clean_guitar",
     {30: 1, 31: 0.5, 32: 80, 34: 75, 13: 45}, [], 6.0),
    ("Long feedback tail", "clean_guitar",
     {12: 500, 13: 95}, [], 60.0),
]

def _safe_audio(audio, label):
    if not np.all(np.isfinite(audio)):
        raise RuntimeError(f"{label}: non-finite samples; unsafe reference")
    peak = float(np.max(np.abs(audio)))
    if peak >= 0.989999:
        raise RuntimeError(f"{label}: hard limiter ceiling {peak:.8f}; no WAV packaging")
    return peak

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path,
                        default=ROOT / "artifacts/listening-lab/phase7-1-ab")
    parser.add_argument("--archive", type=Path,
                        default=ROOT / "artifacts/hydra-phase7-1-ab.zip")
    parser.add_argument("--work", type=Path,
                        default=ROOT / "artifacts/tmp/phase7-1-before")
    parser.add_argument("--before-commit", default=BASELINE)
    args = parser.parse_args()

    current = subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    if current == args.before_commit:
        raise RuntimeError("Cannot compare a commit against itself")

    # Require a reproducible source tree rather than an uncommitted test.
    dirty = subprocess.check_output(
        ["git", "status", "--porcelain", "--untracked-files=no"],
        cwd=ROOT, text=True).strip()
    if dirty:
        raise RuntimeError("Committed source required; tracked files are modified")

    common.extract_commit(args.before_commit, args.work)
    build = args.work / "build"
    build.mkdir(exist_ok=True)
    before_lib = build / "before.dll"
    after_lib = build / "after.dll"
    # package_reverse_ab.build_library explicitly uses -ffp-contract=off.
    baseline_hash, baseline_cmd = common.build_library(args.work, before_lib)
    candidate_hash, candidate_cmd = common.build_library(ROOT, after_lib)

    before = render.HydraEngineRunner(before_lib)
    after = render.HydraEngineRunner(after_lib)
    if (before.api_version >> 16) != (after.api_version >> 16):
        raise RuntimeError("ABI version mismatch")

    if args.output.exists():
        shutil.rmtree(args.output)
    args.output.mkdir(parents=True)
    manifest = dict(
        schema_version=1, suite=SUITE,
        reference_commit=current, before_commit=args.before_commit,
        api_version=after.api_version,
        source_sha256=render.sha256_file(ROOT / "dsp/core/tape_core.cpp"),
        before_library_sha256=baseline_hash,
        after_library_sha256=candidate_hash,
        random_seed=7100, sample_rate_hz=render.RATE,
        block_size_frames=render.BLOCK, working_tree_dirty=False,
        environment=dict(os=platform.platform(), python=platform.python_version(),
                         numpy=np.__version__),
        human_listening_signoff="PENDING_HUMAN_AUDITION", scenarios={})
    comparison = {}
    for i, (name, source, params, events, seconds) in enumerate(CASES, 1):
        seed = 7100 + i
        np.random.seed(seed)
        stimulus = musical_sources.get_musical_source(
            source, seconds, render.RATE, bpm=120)
        if seconds == 60.0:
            stimulus[render.RATE:] = 0.0
        config = dict(stimulus=stimulus, params=params,
                      initial_bpm=120, tempo_source=0, events=events,
                      clock_pulses=[], sample_rate=render.RATE)
        a = render.run_simulation(before, config)
        b = render.run_simulation(after, config)
        peak_a = _safe_audio(a["output"], f"{name}/approved A")
        peak_b = _safe_audio(b["output"], f"{name}/candidate B")
        score = common.metrics(a["output"], b["output"])
        comparison[name] = dict(**score, peak_a=peak_a, peak_b=peak_b)
        key = f"{i:02d}_phase7_{i:02d}"
        entry = dict(
            name=name, group="Phase 7.1: approved vs corrected",
            description="A=approved Phase 6.6 DSP, B=Phase 7.1 DSP; exact input, "
                        "seed, gain and events; no normalisation.",
            duration_sec=seconds, bpm=120.0, tempo_source=0,
            reference_commit=current, random_seed=seed,
            events=events, clock_pulses=[],
            sample_rate_hz=render.RATE, source_material=source,
            comparison_variant="phase7_integrity", comparison_case=i,
            comparison_title=name, human_review_state="NOT REVIEWED",
            stimulus_sha256=hashlib.sha256(stimulus.tobytes()).hexdigest(),
            dsp_params={str(k): float(v) for k, v in b["parameters"].items()},
            markers=[dict(time=0.0, label="Audio start"),
                     dict(time=seconds, label="Audio end")],
            analysis_metrics=dict(
                before_rms=float(np.sqrt(np.mean(a["output"].astype(float)**2))),
                after_rms=float(np.sqrt(np.mean(b["output"].astype(float)**2))),
                after_peak=peak_b, **score))
        for field, audio in (("original_wav", a["output"]),
                             ("processed_wav", b["output"])):
            path = args.output / f"{key}_{field}.wav"
            render.write_wav_24bit(path, audio, render.RATE)
            entry[field + "_file"] = path.name
            entry[field + "_sha256"] = render.sha256_file(path)
        csv_path = args.output / f"{key}_telemetry.csv"
        with csv_path.open("w", encoding="utf-8", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(b["telemetry"][0]))
            writer.writeheader()
            writer.writerows(b["telemetry"])
        entry["telemetry_csv_file"] = csv_path.name
        entry["telemetry_csv_sha256"] = render.sha256_file(csv_path)
        manifest["scenarios"][key] = entry
        print(f"{key}: {name} — {score}", flush=True)

    (args.output / "manifest.json").write_text(
        json.dumps(manifest, indent=2, allow_nan=False), encoding="utf-8")
    reports = args.output / "reports"
    reports.mkdir(exist_ok=True)
    (reports / "comparison.json").write_text(
        json.dumps(dict(before_commit=args.before_commit,
                        reference_commit=current,
                        before_library_sha256=baseline_hash,
                        after_library_sha256=candidate_hash,
                        before_build=baseline_cmd, after_build=candidate_cmd,
                        comparisons=comparison), indent=2, allow_nan=False),
        encoding="utf-8")
    package_listening_lab.package(args.output, args.archive)
    digest = render.sha256_file(args.archive)
    args.archive.with_suffix(".zip.sha256").write_text(
        f"{digest}  {args.archive.name}\n", encoding="ascii")
    print(f"ZIP SHA-256: {digest}")

if __name__ == "__main__":
    main()
