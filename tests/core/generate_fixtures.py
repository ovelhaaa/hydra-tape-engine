#!/usr/bin/env python3
import argparse
import csv
import math
import random
from pathlib import Path
from typing import List, Tuple

SAMPLE_RATE = 48_000
FRAMES = 512


def write_csv(path: Path, data: List[Tuple[float, float]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('w', encoding='utf-8', newline='') as f:
        writer = csv.writer(f)
        for l, r in data:
            writer.writerow((f'{l:.9f}', f'{r:.9f}'))


def impulse() -> List[Tuple[float, float]]:
    return [(1.0 if i == 0 else 0.0, -1.0 if i == 0 else 0.0) for i in range(FRAMES)]


def sine_1khz() -> List[Tuple[float, float]]:
    out = []
    for i in range(FRAMES):
        t = i / SAMPLE_RATE
        l = 0.5 * math.sin(2.0 * math.pi * 1000.0 * t)
        r = 0.5 * math.sin(2.0 * math.pi * 1000.0 * t + math.pi / 2.0)
        out.append((l, r))
    return out


def sweep_log() -> List[Tuple[float, float]]:
    out = []
    f0 = 20.0
    f1 = 20_000.0
    duration = FRAMES / SAMPLE_RATE
    k = math.log(f1 / f0) / duration
    for i in range(FRAMES):
        t = i / SAMPLE_RATE
        phase = 2.0 * math.pi * f0 * ((math.exp(k * t) - 1.0) / k)
        l = 0.45 * math.sin(phase)
        r = 0.45 * math.sin(phase + 0.15)
        out.append((l, r))
    return out


def seeded_noise() -> List[Tuple[float, float]]:
    rng = random.Random(1337)
    return [((rng.random() * 2.0 - 1.0) * 0.25, (rng.random() * 2.0 - 1.0) * 0.25) for _ in range(FRAMES)]


def music_excerpt() -> List[Tuple[float, float]]:
    # Deterministic synthetic phrase (chord + melody + envelope).
    notes = (220.0, 277.18, 329.63, 440.0)
    out = []
    for i in range(FRAMES):
        t = i / SAMPLE_RATE
        env = min(1.0, t * 30.0) * (1.0 - min(1.0, max(0.0, t - 0.006) * 120.0))
        chord = sum(math.sin(2.0 * math.pi * f * t) for f in notes) / len(notes)
        melody = math.sin(2.0 * math.pi * (660.0 if i < FRAMES // 2 else 523.25) * t)
        l = 0.30 * env * chord + 0.15 * env * melody
        r = 0.27 * env * chord - 0.12 * env * melody
        out.append((l, r))
    return out


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--output-dir', default='tests/fixtures/input')
    args = parser.parse_args()

    outdir = Path(args.output_dir)
    write_csv(outdir / 'impulse.csv', impulse())
    write_csv(outdir / 'sine_1khz.csv', sine_1khz())
    write_csv(outdir / 'sweep_log.csv', sweep_log())
    write_csv(outdir / 'noise_seeded.csv', seeded_noise())
    write_csv(outdir / 'music_excerpt.csv', music_excerpt())
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
