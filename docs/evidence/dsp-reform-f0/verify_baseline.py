"""Verify frozen F0 files without rewriting the manifest or rendering audio."""
import argparse
import hashlib
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True,
                        help="Checkout/artifact root containing artifacts/dsp-reform-f0/baseline")
    args = parser.parse_args()
    manifest = json.loads(Path(__file__).with_name("baseline_renders_manifest.json").read_text(encoding="utf-8"))
    rows = []
    for name, scene in manifest["scenarios"].items():
        path = args.root.joinpath(*scene["output_wav"].replace("\\", "/").split("/"))
        actual = hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else None
        rows.append({"scenario": name, "expected": scene["output_wav_sha256"],
                     "actual": actual, "match": actual == scene["output_wav_sha256"]})
    print(json.dumps(rows, indent=2))
    return 0 if len(rows) == 10 and all(row["match"] for row in rows) else 1


if __name__ == "__main__":
    raise SystemExit(main())
