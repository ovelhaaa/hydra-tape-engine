"""Validate the actual listening files before making a distributable ZIP."""
import argparse
import csv
import hashlib
import json
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
    if len(scenarios) != 20 or [int(k[:2]) for k in scenarios] != list(range(1, 21)):
        raise ValueError("Expected the ordered 20 scenarios")
    if len(manifest["reference_commit"]) != 40 or not manifest["api_version"]:
        raise ValueError("Missing provenance")
    total = 0
    for key, scenario in scenarios.items():
        for kind in ("original_wav", "processed_wav", "telemetry_csv"):
            path = (directory / scenario[f"{kind}_file"]).resolve()
            if not path.is_relative_to(directory) or not path.is_file() or not path.stat().st_size:
                raise ValueError(f"Missing/empty or unsafe file: {path}")
            if hashlib.sha256(path.read_bytes()).hexdigest() != scenario[f"{kind}_sha256"]:
                raise ValueError(f"SHA-256 mismatch: {path.name}")
            total += path.stat().st_size
            if kind.endswith("wav"):
                with wave.open(str(path)) as wav:
                    frames = round(scenario["duration_sec"] * scenario["sample_rate_hz"])
                    if (wav.getnframes(), wav.getframerate(), wav.getnchannels(), wav.getsampwidth()) != (frames, scenario["sample_rate_hz"], 2, 3):
                        raise ValueError(f"Truncated/incompatible WAV: {path.name}")
            else:
                with path.open(encoding="utf-8", newline="") as stream:
                    rows = list(csv.DictReader(stream))
                expected_rows = (round(scenario["duration_sec"] * scenario["sample_rate_hz"]) + 511) // 512
                if len(rows) != expected_rows or float(rows[0]["time_sec"]) != 0:
                    raise ValueError(f"Incomplete telemetry: {key}")
                times = [float(row["time_sec"]) for row in rows]
                if any(b <= a for a, b in zip(times, times[1:])):
                    raise ValueError(f"Non-monotonic telemetry: {key}")
        for field in ("dsp_params", "markers", "analysis_metrics", "events", "tempo_source", "random_seed"):
            if field not in scenario:
                raise ValueError(f"Missing {field}: {key}")
        if scenario.get("reference_commit") != manifest["reference_commit"]:
            raise ValueError(f"Scenario provenance mismatch: {key}")
    return manifest, total

def package(directory, archive=None):
    directory = Path(directory)
    manifest, total = validate(directory)
    reports = directory / "reports"
    reports.mkdir(exist_ok=True)
    (reports / "audio-analysis.json").write_text(json.dumps({k: v["analysis_metrics"] for k, v in manifest["scenarios"].items()}, indent=2, allow_nan=False), encoding="utf-8")
    (reports / "validation-summary.md").write_text(f"# Listening Lab validation\n\nCommit: `{manifest['reference_commit']}`\n\n20 scenarios; 40 PCM24 stereo WAVs; 20 CSVs; {total} audio/telemetry bytes.\nAll hashes, frame counts and telemetry lengths verified.\nHuman listening: PENDING_HUMAN_AUDITION. Metrics are observations, not human approval.\n", encoding="utf-8")
    shutil.copyfile(ROOT / "docs/PHASE6_LISTENING_GUIDE.md", directory / "listening-guide.md")
    shutil.copyfile(ROOT / "tools/phase6-listening-lab/README.md", directory / "README.md")
    shutil.copytree(ROOT / "tools/phase6-listening-lab", directory / "player", dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns("tests", "node_modules"))
    validate(directory)
    if archive:
        result = shutil.make_archive(str(Path(archive).with_suffix("")), "zip", directory)
        print(f"ZIP: {result} ({Path(result).stat().st_size} bytes)")
    print(f"VALID: 20 scenarios, 40 WAVs, 20 CSVs, {total} bytes; {manifest['reference_commit']}")

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
