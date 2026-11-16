"""Write byte-level provenance for listening packages."""
import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def render(root, path, scenario, engine, variant, rate, source=None, **build):
    return {
        "scenario_id": scenario,
        "render_engine": engine,
        "render_variant": variant,
        "sample_rate": rate,
        "render_wav_sha256": sha256(root / path),
        **({"source_wav_sha256": sha256(root / source)} if source else {}),
        **build,
    }


def manifest(generator, renders, **origin):
    files = [[path, row["render_wav_sha256"]] for path, row in sorted(renders.items())]
    payload = json.dumps(files, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    return {
        "package_schema_version": 1,
        "package_id": hashlib.sha256(payload).hexdigest(),
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "generator_version": sha256(Path(generator)),
        **origin,
        "renders": renders,
    }
