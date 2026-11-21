#!/usr/bin/env python3
"""Measure the audio delta introduced by the Phase 6.4 numerical fix.

Builds two native libraries from source -- the last approved Phase 6.3 DSP
(7431a6d) and the working tree -- renders identical stimuli through both, and
reports float and 24-bit deltas. Nothing is normalised or gain-matched.
"""
import argparse
import hashlib
import io
import json
import shutil
import subprocess
import sys
import tarfile
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import musical_sources  # noqa: E402
import render_phase6_review as render  # noqa: E402

BEFORE_COMMIT = "7431a6db05abfde97838721cd089f94c7fed5047"


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
        except TypeError:
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


CASES = [
    ("Clean guitar", "clean_guitar", {3: 25}, []),
    ("Driven guitar", "clean_guitar", {3: 90, 6: 100}, []),
    ("Conventional delay", "clean_guitar", {12: 420, 13: 60}, []),
    ("Reverse 1/4", "clean_guitar", {23: 1, 36: 0, 37: 0, 12: 500, 13: 40}, []),
    ("Reverse 1/8", "clean_guitar", {23: 1, 36: 0, 37: 3, 12: 500, 13: 40}, []),
    ("Reverse high feedback", "clean_guitar", {23: 1, 36: 0, 37: 0, 13: 95}, []),
    ("Multi-Head", "drum_machine", {16: 7, 21: 1, 13: 45}, []),
    ("Tape speed transition", "clean_guitar", {3: 40},
     [{"time_sec": 1.0, "type": "param", "id": 5, "value": 70.0},
      {"time_sec": 2.5, "type": "param", "id": 5, "value": 30.0}]),
    ("BPM change", "clean_guitar", {12: 500, 13: 40},
     [{"time_sec": 1.5, "type": "bpm", "bpm": 140.0},
      {"time_sec": 3.0, "type": "bpm", "bpm": 100.0}]),
    ("Long feedback", "clean_guitar", {13: 88},
     [{"time_sec": 1.0, "type": "param", "id": 4, "value": 0.0}]),
]


def pcm24(audio):
    integer = np.clip(np.round(audio.astype(np.float64) * 8388607.0), -8388608, 8388607)
    return integer.astype(np.int32)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "artifacts/phase64/audio-delta")
    parser.add_argument("--work", type=Path, default=ROOT / "artifacts/tmp/phase64-before")
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
    if before.api_version != after.api_version:
        raise RuntimeError("API version mismatch")

    args.output.mkdir(parents=True, exist_ok=True)
    report = {"before_commit": args.before_commit,
              "before_library_sha256": before_hash,
              "after_library_sha256": after_hash,
              "before_build": before_cmd, "after_build": after_cmd,
              "cases": {}}
    for index, (title, source, params, events) in enumerate(CASES, 1):
        seed = 6400 + index
        np.random.seed(seed)
        stimulus = musical_sources.get_musical_source(source, 6, render.RATE, bpm=120)
        config = dict(stimulus=stimulus, params=params, initial_bpm=120,
                      tempo_source=0, events=events, clock_pulses=[],
                      sample_rate=render.RATE)
        a = render.run_simulation(before, config)["output"]
        b = render.run_simulation(after, config)["output"]
        delta = b.astype(np.float64) - a.astype(np.float64)
        pa, pb = pcm24(a), pcm24(b)
        diff = np.abs(pa - pb)
        report["cases"][title] = {
            "seed": seed,
            "float_max_abs": float(np.max(np.abs(delta))),
            "float_rmse": float(np.sqrt(np.mean(delta ** 2))),
            "float_bit_exact": bool(np.array_equal(a, b)),
            "pcm24_bit_exact": bool(np.array_equal(pa, pb)),
            "pcm24_samples_changed": int(np.count_nonzero(diff)),
            "pcm24_total_samples": int(diff.size),
            "pcm24_max_lsb": int(diff.max()),
            "float_abs_max_signal": float(np.max(np.abs(b))),
        }
        print(title, report["cases"][title], flush=True)

    (args.output / "delta.json").write_text(json.dumps(report, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
