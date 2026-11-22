"""Validate an exported Listening Lab review JSON against a package manifest and files."""
import argparse
import hashlib
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reviews", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--package", type=Path, required=True)
    args = parser.parse_args()

    reviews = json.loads(args.reviews.read_text(encoding="utf-8"))
    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    if reviews.get("schema_version") != 1:
        raise SystemExit("review schema_version must be 1")
    if reviews.get("reference_commit") != manifest.get("reference_commit"):
        raise SystemExit("review and manifest reference commits differ")
    if set(reviews.get("reviews", {})) != set(manifest.get("scenarios", {})):
        raise SystemExit("review and manifest scenario sets differ")

    states = {}
    for scenario_id, review in sorted(reviews["reviews"].items()):
        scenario = manifest["scenarios"][scenario_id]
        expected = [scenario["original_wav_sha256"], scenario["processed_wav_sha256"],
                    scenario["telemetry_csv_sha256"]]
        if reviews["package_hashes"].get(scenario_id) != expected:
            raise SystemExit(f"review package hashes differ: {scenario_id}")
        for kind in ("original_wav", "processed_wav", "telemetry_csv"):
            path = args.package / scenario[f"{kind}_file"]
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            if digest != scenario[f"{kind}_sha256"]:
                raise SystemExit(f"package file hash differs: {path.name}")
        states[review["state"]] = states.get(review["state"], 0) + 1

    print(json.dumps({"reference_commit": reviews["reference_commit"],
                      "scenarios": len(reviews["reviews"]), "states": states,
                      "hashes_verified": True}, sort_keys=True))


if __name__ == "__main__":
    main()
