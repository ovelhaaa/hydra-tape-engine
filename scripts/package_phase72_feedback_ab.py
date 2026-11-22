"""Phase 7.2 A/B/C tape feedback routing listening experiment.

A: exact approved Phase 7.1 main commit, ordinary DSP.
A0: new branch with experiment disabled (MUST BE FLOAT BIT-EXACT to A).
B: compile-time isolated clean read-head feedback source.
C: compile-time independent but *degraded* read-head feedback source.
Generate separate A/B and A/C paired archives for the existing two-way player.

Every case uses identical synthesized input, parameters, event positions and
random state. Results are listening evidence, not a replacement for human review.
"""
import argparse
import csv
import hashlib
import json
import platform
import shutil
import subprocess
import sys
from pathlib import Path

import numpy as np

import musical_sources
import package_listening_lab
import package_reverse_ab as common
import render_phase6_review as render

ROOT = Path(__file__).resolve().parents[1]
APPROVED_COMMIT = "24977b67e1ab8441080a29d9e5d40114c966ac50"
SUITE = "Hydra Phase 7.2 Feedback Routing A/B"
CLASSIC_SUITE = "Hydra Phase 7.2 Classic Playback A/C"
EXPERIMENT_DEFINE = "-DHYDRA_PHASE72_EXPERIMENTAL_FEEDBACK_ROUTING=1"
CLASSIC_DEFINE = "-DHYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING=1"

DEFAULT = {
    0: 8, 1: 6, 2: 5, 3: 30, 4: 12,
    5: 50, 6: 45, 7: 30,
    11: 1, 12: 500, 13: 55, 14: 75, 15: 70,
    16: 4, 17: 120, 20: 50, 21: 0,
    23: 0, 25: 0, 30: 0, 36: 0, 37: 0,
}

# The 48kHz approved source has three read-head taps; use a diverse
# musical repertoire, both low/high feedback, monitor transitions and the
# fully separate Warp feedback source as a bit-exact negative control.
CASES = [
    ("Clean repeat", "clean_guitar",
     {13: 45}, [], 6, False),
    ("Aged and dark repeats", "clean_guitar",
     {6: 85, 20: 20, 13: 70}, [], 6, False),
    ("Monitor delay level sweep", "clean_guitar",
     {13: 65, 15: 90},
     [{"time_sec": 2.0, "type": "param", "id": 15, "value": 20.0},
      {"time_sec": 4.0, "type": "param", "id": 15, "value": 90.0}], 6, False),
    ("Monitor texture level sweep", "clean_guitar",
     {13: 65, 14: 90},
     [{"time_sec": 2.0, "type": "param", "id": 14, "value": 10.0},
      {"time_sec": 4.0, "type": "param", "id": 14, "value": 90.0}], 6, False),
    ("Hiss after music stops", "clean_guitar",
     {4: 70, 6: 70, 13: 65}, [], 6, True),
    # The first driven-guitar/drive80/feedback85 reference hit the approved
    # engine's hard limiter ceiling (0.99); do not weaken the safety check.
    # Keep a high-feedback case with a less aggressive source and record gain.
    ("High feedback", "clean_guitar",
     {3: 30, 13: 85, 15: 65}, [], 6, False),
    ("Ping-pong Multi-Head", "drum_machine",
     {16: 7, 21: 1, 13: 65}, [], 6, False),
    ("Reverse blend transition", "clean_guitar",
     {13: 65},
     [{"time_sec": 2.0, "type": "param", "id": 23, "value": 1.0},
      {"time_sec": 4.0, "type": "param", "id": 23, "value": 0.0}], 6, False),
    ("Warp negative control", "clean_guitar",
     {30: 1, 31: 0.5, 32: 80, 34: 70, 13: 45}, [], 6, False),
    ("Long feedback residual", "clean_guitar",
     {4: 45, 13: 88, 15: 60}, [], 60, True),
]


def build_shared(source_root, output, experiment_define=None):
    compiler = common.compiler_path()
    args = [compiler, "-O3", "-DNDEBUG", "-std=c++17",
            "-ffp-contract=off", "-shared", "-fPIC"]
    if experiment_define is not None:
        if experiment_define not in (EXPERIMENT_DEFINE, CLASSIC_DEFINE):
            raise ValueError("Unknown DSP experiment define")
        args.append(experiment_define)
    if sys.platform == "win32":
        args.append("-Wl,--export-all-symbols")
    args += [f"-I{source_root / 'include'}",
             f"-I{source_root / 'dsp/core'}",
             str(source_root / "dsp/core/tape_core.cpp"),
             str(source_root / "dsp/core/hydra_dsp_c_api.cpp"),
             "-o", str(output)]
    subprocess.run(args, check=True)
    return hashlib.sha256(output.read_bytes()).hexdigest(), args


def audio_metrics(audio, rate):
    """Descriptive output metrics; not an unapproved acceptance threshold."""
    out = []
    frames = len(audio)
    for start in range(0, frames, rate):
        segment = audio[start:min(start + rate, frames)].astype(np.float64)
        rms = float(np.sqrt(np.mean(np.square(segment))))
        mono = np.mean(segment, axis=1)
        spectrum = np.abs(np.fft.rfft(mono)) ** 2
        freqs = np.fft.rfftfreq(len(mono), 1.0 / rate)
        total = float(np.sum(spectrum))
        fraction = float(np.sum(spectrum[freqs >= 3000.0]) / total) if total else 0.0
        centroid = float(np.sum(freqs * spectrum) / total) if total else 0.0
        out.append({"second": start // rate,
                    "rms_dbfs": float(20 * np.log10(max(rms, 1e-12))),
                    "peak": float(np.max(np.abs(segment))),
                    "hf_energy_ratio_above_3khz": fraction,
                    "spectral_centroid_hz": centroid})
    return out


def ensure_safe(audio, title):
    if not np.all(np.isfinite(audio)):
        raise RuntimeError(f"{title}: non-finite audio")
    peak = float(np.max(np.abs(audio)))
    if peak >= 0.989999:
        raise RuntimeError(f"{title}: limiter ceiling {peak}, refusing misleading WAV")
    return peak


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before-commit", default=APPROVED_COMMIT)
    parser.add_argument("--variant", choices=("isolated", "classic"),
                        default="isolated",
                        help="B uses uncolored head, C uses colored playback-head feedback")
    parser.add_argument("--output", type=Path,
                        default=ROOT / "artifacts/listening-lab/phase7-2-feedback")
    parser.add_argument("--archive", type=Path,
                        default=ROOT / "artifacts/hydra-phase7-2-feedback.zip")
    parser.add_argument("--work", type=Path,
                        default=ROOT / "artifacts/tmp/phase72-feedback")
    args = parser.parse_args()
    is_classic = args.variant == "classic"
    experiment_define = CLASSIC_DEFINE if is_classic else EXPERIMENT_DEFINE
    suite = CLASSIC_SUITE if is_classic else SUITE
    variant_label = "C" if is_classic else "B"
    current = subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    if current == args.before_commit:
        raise RuntimeError("Candidate commit equals approved reference")
    changed = subprocess.check_output(
        ["git", "status", "--porcelain", "--untracked-files=no"],
        cwd=ROOT, text=True).strip()
    if changed:
        raise RuntimeError("Commit source files before producing listening evidence")

    common.extract_commit(args.before_commit, args.work)
    build = args.work / "build"
    build.mkdir(exist_ok=True)
    ref_path = build / "approved.dll"
    candidate_default_path = build / "branch_default.dll"
    candidate_experiment_path = build / (
        "branch_classic.dll" if is_classic else "branch_isolated.dll")

    ref_hash, ref_cmd = build_shared(args.work, ref_path)
    default_hash, default_cmd = build_shared(ROOT, candidate_default_path)
    experiment_hash, experiment_cmd = build_shared(
        ROOT, candidate_experiment_path, experiment_define)

    approved = render.HydraEngineRunner(ref_path)
    candidate_default = render.HydraEngineRunner(candidate_default_path)
    alternate = render.HydraEngineRunner(candidate_experiment_path)
    if len({approved.api_version, candidate_default.api_version,
            alternate.api_version}) != 1:
        raise RuntimeError("Experiment unexpectedly changed the public DSP ABI")

    if args.output.exists():
        shutil.rmtree(args.output)
    args.output.mkdir(parents=True)
    manifest = dict(
        schema_version=1, suite=suite, reference_commit=current,
        before_commit=args.before_commit, api_version=alternate.api_version,
        source_sha256=render.sha256_file(ROOT / "dsp/core/tape_core.cpp"),
        before_library_sha256=ref_hash, default_library_sha256=default_hash,
        after_library_sha256=experiment_hash, compile_time_experiment=experiment_define,
        experimental_variant=args.variant,
        random_seed=7200, sample_rate_hz=render.RATE,
        block_size_frames=render.BLOCK, working_tree_dirty=False,
        environment=dict(os=platform.platform(), python=platform.python_version(),
                         numpy=np.__version__),
        human_listening_signoff="PENDING_HUMAN_AUDITION", scenarios={})
    comparisons = {}
    for i, (name, source, override, events, seconds, silence) in enumerate(CASES, 1):
        seed = 7200 + i
        np.random.seed(seed)
        stimulus = musical_sources.get_musical_source(source, seconds, render.RATE, bpm=120)
        if silence:
            stimulus[render.RATE:] = 0.0
        params = dict(DEFAULT)
        params.update(override)
        config = dict(stimulus=stimulus, params=params, initial_bpm=120,
                      tempo_source=0, events=events, clock_pulses=[],
                      sample_rate=render.RATE)
        a = render.run_simulation(approved, config)
        c = render.run_simulation(candidate_default, config)
        b = render.run_simulation(alternate, config)
        if not np.array_equal(a["output"], c["output"]):
            delta = common.metrics(a["output"], c["output"])
            raise RuntimeError(
                f"{name}: experiment-OFF must match approved main bit-exact: {delta}")
        before_peak = ensure_safe(a["output"], name + "/approved")
        after_peak = ensure_safe(b["output"], name + "/" + variant_label)
        if name == "Warp negative control" and not np.array_equal(a["output"], b["output"]):
            raise RuntimeError("Warp audio changed, despite an independent routing path")
        delta = common.metrics(a["output"], b["output"])
        comparisons[name] = dict(
            **delta, approved_peak=before_peak, experimental_peak=after_peak,
            experimental_variant=args.variant,
            approved_per_second=audio_metrics(a["output"], render.RATE),
            experimental_per_second=audio_metrics(b["output"], render.RATE))
        key = f"{i:02d}_feedback_{i:02d}"
        entry = dict(
            name=name, group=("Phase 7.2 Classic Playback A/C" if is_classic
                              else "Phase 7.2 Feedback Routing A/B"),
            description=("A=approved Phase 7.1; " + variant_label + "=" +
                        ("independent degraded playback-head feedback"
                         if is_classic else "independent uncolored read-head feedback") +
                        ". Identical source, seed, gains and transport; no normalization."),
            duration_sec=float(seconds), bpm=120.0, tempo_source=0,
            reference_commit=current, random_seed=seed, events=events,
            clock_pulses=[], sample_rate_hz=render.RATE,
            source_material=source, comparison_variant=(
                "classic-playback-feedback" if is_classic else "isolated-feedback"),
            comparison_case=i, comparison_title=name,
            human_review_state="NOT REVIEWED",
            stimulus_sha256=hashlib.sha256(stimulus.tobytes()).hexdigest(),
            dsp_params={str(k): float(v) for k, v in b["parameters"].items()},
            markers=([dict(time=0.0, label="Audio start"),
                      dict(time=float(seconds), label="Audio end")] +
                     [dict(time=float(e["time_sec"]), label=f"Change parameter {e['id']}")
                      for e in events] +
                     ([dict(time=1.0, label="Input becomes silent")] if silence else [])),
            analysis_metrics=dict(
                before_rms=float(np.sqrt(np.mean(a["output"].astype(float) ** 2))),
                after_rms=float(np.sqrt(np.mean(b["output"].astype(float) ** 2))),
                after_peak=after_peak, **delta))
        for key_name, audio in (("original_wav", a["output"]), ("processed_wav", b["output"])):
            wav_path = args.output / f"{key}_{key_name}.wav"
            render.write_wav_24bit(wav_path, audio, render.RATE)
            entry[key_name + "_file"] = wav_path.name
            entry[key_name + "_sha256"] = render.sha256_file(wav_path)
        csv_path = args.output / f"{key}_telemetry.csv"
        with csv_path.open("w", encoding="utf-8", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(b["telemetry"][0]))
            writer.writeheader()
            writer.writerows(b["telemetry"])
        entry["telemetry_csv_file"] = csv_path.name
        entry["telemetry_csv_sha256"] = render.sha256_file(csv_path)
        manifest["scenarios"][key] = entry
        print(name, delta, flush=True)

    (args.output / "manifest.json").write_text(
        json.dumps(manifest, indent=2, allow_nan=False), encoding="utf-8")
    reports = args.output / "reports"
    reports.mkdir(exist_ok=True)
    (reports / "phase72_feedback_comparison.json").write_text(
        json.dumps(dict(
            approved_commit=args.before_commit, experimental_commit=current,
            approved_binary_sha256=ref_hash, default_binary_sha256=default_hash,
            experimental_binary_sha256=experiment_hash,
            variant=args.variant, experiment_define=experiment_define,
            builds=dict(approved=ref_cmd, default=default_cmd,
                        experimental=experiment_cmd),
            monitor_independent_route_is_experimental=True,
            production_reference_proved_bit_exact=True,
            comparisons=comparisons), indent=2, allow_nan=False),
        encoding="utf-8")
    package_listening_lab.package(args.output, args.archive)
    digest = render.sha256_file(args.archive)
    args.archive.with_suffix(".zip.sha256").write_text(
        f"{digest}  {args.archive.name}\n", encoding="ascii")
    print(f"ZIP SHA-256: {digest}")

if __name__ == "__main__":
    main()
