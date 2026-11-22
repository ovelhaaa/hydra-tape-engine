"""Validate the actual listening files before making a distributable ZIP."""
import argparse
import csv
import hashlib
import json
import math
import re
from pathlib import Path
import shutil
import wave

ROOT = Path(__file__).resolve().parents[1]

def validate(directory):
    directory = Path(directory).resolve()
    manifest = json.loads((directory / "manifest.json").read_text(encoding="utf-8"))
    if manifest.get("schema_version") != 1 or manifest.get("human_listening_signoff") != "PENDING_HUMAN_AUDITION":
        raise ValueError("Invalid manifest schema or agent-assigned human signoff")
    scenarios = manifest["scenarios"]
    expected = {"Hydra Phase 6.2 Bias A/B": 60,
                "Hydra Phase 6.3 Reverse A/B": 10,
                "Hydra Phase 6.4 Final A/B": 10,
                "Hydra Phase 7.1 DSP Integrity A/B": 10}.get(manifest.get("suite"), 20)
    if len(scenarios) != expected or [int(k[:2]) for k in scenarios] != list(range(1, expected + 1)):
        raise ValueError(f"Expected the ordered {expected} scenarios")
    if not re.fullmatch(r"[a-f0-9]{40}", manifest["reference_commit"]) or not manifest["api_version"]:
        raise ValueError("Missing provenance")
    total = 0
    seen = set()
    for key, scenario in scenarios.items():
        for kind in ("original_wav", "processed_wav", "telemetry_csv"):
            path = (directory / scenario[f"{kind}_file"]).resolve()
            if not path.is_relative_to(directory) or not path.is_file() or not path.stat().st_size:
                raise ValueError(f"Missing/empty or unsafe file: {path}")
            if path in seen:
                raise ValueError(f"Reused scenario file: {path}")
            seen.add(path)
            if hashlib.sha256(path.read_bytes()).hexdigest() != scenario[f"{kind}_sha256"]:
                raise ValueError(f"SHA-256 mismatch: {path.name}")
            total += path.stat().st_size
            if kind.endswith("wav"):
                with wave.open(str(path)) as wav:
                    frames = round(scenario["duration_sec"] * scenario["sample_rate_hz"])
                    if (wav.getnframes(), wav.getframerate(), wav.getnchannels(), wav.getsampwidth()) != (frames, scenario["sample_rate_hz"], 2, 3):
                        raise ValueError(f"Truncated/incompatible WAV: {path.name}")
                    if len(wav.readframes(frames)) != frames * 6:
                        raise ValueError(f"Truncated physical PCM data: {path.name}")
            else:
                with path.open(encoding="utf-8", newline="") as stream:
                    rows = list(csv.DictReader(stream))
                expected_rows = (round(scenario["duration_sec"] * scenario["sample_rate_hz"]) + 511) // 512
                if len(rows) != expected_rows or float(rows[0]["time_sec"]) != 0:
                    raise ValueError(f"Incomplete telemetry: {key}")
                times = [float(row["time_sec"]) for row in rows]
                if any(b <= a for a, b in zip(times, times[1:])):
                    raise ValueError(f"Non-monotonic telemetry: {key}")
                if any(abs(time - i * 512 / scenario["sample_rate_hz"]) > 0.000051 for i, time in enumerate(times)):
                    raise ValueError(f"Premature/incorrect telemetry timestamps: {key}")
        for field in ("dsp_params", "markers", "analysis_metrics", "events", "tempo_source", "random_seed"):
            if field not in scenario:
                raise ValueError(f"Missing {field}: {key}")
        if scenario.get("reference_commit") != manifest["reference_commit"]:
            raise ValueError(f"Scenario provenance mismatch: {key}")
        if not scenario["markers"] or any(not marker.get("label") or not math.isfinite(marker["time"]) or not 0 <= marker["time"] <= scenario["duration_sec"] for marker in scenario["markers"]):
            raise ValueError(f"Missing/invalid musical timeline markers: {key}")
    return manifest, total

def package(directory, archive=None):
    directory = Path(directory)
    manifest, total = validate(directory)
    reports = directory / "reports"
    reports.mkdir(exist_ok=True)
    (reports / "audio-analysis.json").write_text(json.dumps({k: v["analysis_metrics"] for k, v in manifest["scenarios"].items()}, indent=2, allow_nan=False), encoding="utf-8")
    count = len(manifest["scenarios"])
    (reports / "validation-summary.md").write_text(f"# Listening Lab validation\n\nCommit: `{manifest['reference_commit']}`\n\n{count} scenarios; {2 * count} PCM24 stereo WAVs; {count} CSVs; {total} audio/telemetry bytes.\nAll hashes, frame counts and telemetry lengths verified.\nHuman listening: PENDING_HUMAN_AUDITION. Metrics are observations, not human approval.\n", encoding="utf-8")
    guide = "PHASE6_BIAS_AB_LISTENING_GUIDE.md" if manifest.get("suite") == "Hydra Phase 6.2 Bias A/B" else "PHASE6_LISTENING_GUIDE.md"
    if manifest.get("suite") == "Hydra Phase 6.3 Reverse A/B":
        guide = "PHASE6_REVERSE_OPTIMIZATION_LISTENING_GUIDE.md"
    if manifest.get("suite") == "Hydra Phase 6.4 Final A/B":
        guide = "PHASE6_FINAL_LISTENING_GUIDE.md"
    if manifest.get("suite") == "Hydra Phase 7.1 DSP Integrity A/B":
        guide = "PHASE7_1_LISTENING_GUIDE.md"
    shutil.copyfile(ROOT / "docs" / guide, directory / "listening-guide.md")
    if manifest.get("suite") == "Hydra Phase 6.3 Reverse A/B":
        report = ROOT / "docs/PHASE6_REVERSE_REALTIME_OPTIMIZATION.md"
        if report.exists():
            shutil.copyfile(report, reports / report.name)
            shutil.copyfile(ROOT / "docs" / guide, reports / guide)
    if manifest.get("suite") == "Hydra Phase 6.4 Final A/B":
        reports.mkdir(exist_ok=True)
        for name in ("PHASE6_NUMERICAL_EQUIVALENCE.md", "PHASE6_FEEDBACK_STABILITY.md",
                     "PHASE6_TRANSITION_ANALYSIS.md", "PHASE6_RELEASE_DECISION.md",
                     "PHASE6_RESET_CONTRACT.md"):
            source = ROOT / "docs" / name
            if source.exists():
                shutil.copyfile(source, reports / name)
    if manifest.get("suite") == "Hydra Phase 6.2 Bias A/B":
        report = ROOT / "docs/PHASE6_BIAS_ANTIALIASING_REPORT.md"
        if report.exists():
            shutil.copyfile(report, reports / report.name)
            shutil.copyfile(ROOT / "docs/PHASE6_BIAS_HARDWARE_FOLLOWUP.md", reports / "PHASE6_BIAS_HARDWARE_FOLLOWUP.md")
            shutil.copyfile(ROOT / "docs/PHASE6_BIAS_HUMAN_AUDITION.md", reports / "PHASE6_BIAS_HUMAN_AUDITION.md")
            shutil.copyfile(ROOT / "docs" / guide, reports / guide)
            evidence = reports / "evidence"
            evidence.mkdir(exist_ok=True)
            for name in ("phase6-bias-antialiasing.json", "phase6-bias-engine.json", "phase6-bias-regressions.json",
                         "phase6-bias-hardware.json", "phase6-bias-hardware-variants.json",
                         "phase6-bias-linux-regressions.json", "phase6-bias-moment-probe.json",
                         "phase6-bias-human-reviews-f0c16cd.json", "phase6-bias-human-reviewed-manifest-f0c16cd.json",
                         "phase6-bias-human-signoff.json"):
                shutil.copyfile(ROOT / "docs/evidence" / name, evidence / name)
            shutil.copytree(ROOT / "docs/evidence/phase6-bias-plots", evidence / "phase6-bias-plots", dirs_exist_ok=True)
            shutil.copytree(ROOT / "docs/evidence/phase6-bias-hardware", evidence / "phase6-bias-hardware", dirs_exist_ok=True)
            offline_guide = directory / "listening-guide.md"
            offline_guide.write_text(offline_guide.read_text(encoding="utf-8").replace(
                "](PHASE6_BIAS_HARDWARE_FOLLOWUP.md)", "](reports/PHASE6_BIAS_HARDWARE_FOLLOWUP.md)"), encoding="utf-8")
    shutil.copyfile(ROOT / "tools/phase6-listening-lab/README.md", directory / "README.md")
    shutil.copytree(ROOT / "tools/phase6-listening-lab", directory / "player", dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns("tests", "node_modules"))
    validate(directory)
    if archive:
        result = shutil.make_archive(str(Path(archive).with_suffix("")), "zip", directory)
        print(f"ZIP: {result} ({Path(result).stat().st_size} bytes)")
    print(f"VALID: {count} scenarios, {2 * count} WAVs, {count} CSVs, {total} bytes; {manifest['reference_commit']}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--archive", type=Path)
    parser.add_argument("--validate-only", action="store_true")
    args = parser.parse_args()
    if args.validate_only:
        print(validate(args.directory)[1])
    else:
        package(args.directory, args.archive)
