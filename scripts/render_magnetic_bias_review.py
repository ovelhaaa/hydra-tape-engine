"""Render the Magnetic Bias De-Click A/B package using five explicit Native builds."""

import argparse
import array
import json
from pathlib import Path
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory

import numpy as np
from scipy.signal import welch
import soundfile as sf

import audio_provenance

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tests/core"))
from listening_review import BASE, BLOCK, RATE, SCENES, Scene, little_endian, synthesize

BASELINE = "f8d2851c663d62ae4850087dc5bb255bd8b818be"
ORIGINAL = "3bbcd720a2302cdd8b38d5d1e690d8ee521a9efb"
FLAGS = {
    "baseline": "-O3 -DNDEBUG -std=c++17",
    "strict": "-O2 -fno-fast-math -DNDEBUG -std=c++17",
    "fast": "-O2 -ffast-math -fno-finite-math-only -DNDEBUG -std=c++17",
}
PRIORITY = {
    "tape_clean": (5.0, 7.0, 300287),
    "multi_head_transients": (0.10, 0.15, 6008),
}


def cases():
    warp = {30: 1, 31: 1, 32: 80, 33: .5, 34: 0, 35: 0, 14: 100, 15: 100}
    yield Scene("tape_clean", "excerpt", {3: 5})
    yield Scene("tape_high_drive", "excerpt", {3: 100})
    yield from (s for s in SCENES if s.name in {
        "multi_head_transients", "high_feedback_harmonic", "ping_pong", "wow_flutter_excerpt"
    })
    yield Scene("warp_musical", "harmonic", warp)
    yield Scene("warp_raw_integer", "harmonic", warp | {35: 2})
    yield Scene("warp_feedback_cascade", "impulse", warp | {34: 95}, seconds=30)
    yield Scene("silence_decay", "impulse", {13: 95}, seconds=30)
    yield Scene("warp_feedback_95", "bias_review_impulse", {
        0: 0, 1: 0, 2: 0, 3: 20, 4: 0, 8: 0, 11: 1, 12: 100, 13: 0,
        14: 100, 15: 100, 30: 1, 31: .5, 32: 80, 33: 0, 34: 95, 35: 0,
    }, seconds=10)


def metrics(a, b, start, end):
    x = a[start:end].astype(np.float64)
    y = b[start:end].astype(np.float64)
    d = y - x
    frame, channel = np.unravel_index(abs(d).argmax(), d.shape)
    hz, power = welch(np.stack((x, y)), RATE, axis=1, nperseg=min(8192, len(x)))
    power = power.mean(axis=2)
    magnitude = np.sqrt(power)
    rms = np.sqrt(np.mean(np.stack((x, y)) ** 2, axis=(1, 2)))
    corr = np.corrcoef(x.ravel(), y.ravel())[0, 1] if np.all(rms > 0) else None
    return dict(max_abs=float(abs(d[frame, channel])), peak_frame=int(frame + start), peak_channel=int(channel),
                rmse=float(np.sqrt(np.mean(d * d))), correlation=corr, rms=rms.tolist(),
                peak=np.max(abs(np.stack((x, y))), axis=(1, 2)).tolist(),
                step=np.max(abs(np.diff(np.stack((x, y)), axis=1)), axis=(1, 2)).tolist(),
                null_step=float(np.max(abs(np.diff(d, axis=0)))),
                hf_energy=np.sum(power[:, hz >= 4000], axis=1).tolist(),
                relative_spectral_delta=float(np.linalg.norm(magnitude[1] - magnitude[0]) /
                                              max(np.linalg.norm(magnitude[0]), 1e-30)),
                bias_bins_dbfs={str(f): (10 * np.log10(np.maximum(1e-30,
                    power[:, abs(hz - f).argmin()] * (hz[1] - hz[0])))).tolist() for f in (6000, 18000)},
                rms_by_channel=np.sqrt(np.mean(np.stack((x, y)) ** 2, axis=1)).tolist(),
                stereo_difference_rms=np.sqrt(np.mean(np.stack((x[:, 0] - x[:, 1], y[:, 0] - y[:, 1])) ** 2,
                                                      axis=1)).tolist(),
                last_second_rms=np.sqrt(np.mean(np.stack((a[-RATE:], b[-RATE:])).astype(float) ** 2,
                                                axis=(1, 2))).tolist())


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    variants = ("baseline", "original_strict", "original_fast", "corrected_strict", "corrected_fast")
    for variant in variants:
        cli.add_argument("--" + variant.replace("_", "-"), type=Path, required=True)
    cli.add_argument("--corrected-commit", required=True)
    cli.add_argument("--historical-inputs", type=Path, help="Original release gate folder with PCM and automation")
    cli.add_argument("--output-dir", type=Path, required=True)
    args = cli.parse_args()
    root = args.output_dir
    root.mkdir(parents=True, exist_ok=True)
    manifest = {"suite": "Magnetic Bias De-Click — V1.3"}
    renders = {}
    binary_hashes = {variant: audio_provenance.sha256(getattr(args, variant)) for variant in variants}
    with TemporaryDirectory() as temp:
        pcm = Path(temp) / "output.f32"
        for scene in cases():
            folder = root / scene.name
            folder.mkdir(exist_ok=True)
            source = folder / "input.f32"
            schedule = folder / "automation.txt"
            params = BASE | scene.settings
            if scene.stimulus == "bias_review_impulse":
                params = scene.settings
                samples = np.zeros((scene.seconds * RATE, 2), dtype="<f4")
                samples[0] = 0.8
                samples.tofile(source)
            elif args.historical_inputs:
                original = args.historical_inputs / scene.name
                shutil.copyfile(original / source.name, source)
                shutil.copyfile(original / schedule.name, schedule)
            else:
                source.write_bytes(little_endian(synthesize(scene, None)).tobytes())
            events = list(scene.events)
            if not args.historical_inputs or scene.stimulus == "bias_review_impulse":
                automation = [(0, key, value) for key, value in params.items()]
                automation += [(round(t * RATE / BLOCK) * BLOCK, key, value) for t, key, value in events]
                schedule.write_text("".join(f"{frame} {key} {value}\n" for frame, key, value in sorted(automation)),
                                    encoding="ascii")

            pairs = [("original_fast", "corrected_fast", "original_fast_vs_corrected_fast")]
            if scene.name in PRIORITY:
                pairs.extend([
                    ("original_strict", "corrected_strict", "original_strict_vs_corrected_strict"),
                    ("corrected_strict", "corrected_fast", "corrected_strict_vs_corrected_fast"),
                    ("baseline", "corrected_fast", "baseline_vs_corrected"),
                ])
            audio = {}
            source_hash = audio_provenance.sha256(source)
            for variant in dict.fromkeys(v for first, second, _ in pairs for v in (first, second)):
                subprocess.run([getattr(args, variant).resolve(), source, schedule, pcm], check=True)
                data = np.fromfile(pcm, dtype="<f4").reshape(-1, 2)
                if len(data) != scene.seconds * RATE or not np.isfinite(data).all():
                    raise ValueError(f"Invalid audio: {scene.name}/{variant}")
                audio[variant] = data
                path = f"{scene.name}/{variant}.wav"
                sf.write(root / path, data, RATE, subtype="PCM_24")
                commit = BASELINE if variant == "baseline" else (
                    ORIGINAL if variant.startswith("original") else args.corrected_commit
                )
                renders[path] = audio_provenance.render(
                    root, path, scene.name, "TapeCore Native", variant, RATE, dsp_commit=commit,
                    source_pcm_sha256=source_hash, render_binary_sha256=binary_hashes[variant],
                    build_profile="Release", compiler_flags=FLAGS[variant.split("_")[-1]],
                )
            start, end, marker = PRIORITY.get(scene.name, (0, scene.seconds, None))
            comparisons = {key: metrics(audio[a], audio[b], round(start * RATE), round(end * RATE))
                           for a, b, key in pairs}
            manifest[scene.name] = dict(
                rate=RATE, frames=scene.seconds * RATE, settings=params, events=events,
                comparisons=comparisons,
                listening_markers=[{"time": marker / RATE, "label": "Transiente histórico medido"}]
                if marker is not None else [],
            )
            print(f"Rendered {scene.name}", flush=True)

    manifest["provenance"] = audio_provenance.manifest(
        __file__, renders, baseline_commit=BASELINE, optimized_commit=args.corrected_commit,
    )
    (root / "manifest.json").write_text(json.dumps(manifest, indent=2, allow_nan=False) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
