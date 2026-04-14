#!/usr/bin/env python3
import argparse
import csv
import math
from dataclasses import dataclass
from pathlib import Path
from typing import List, Tuple


@dataclass
class Metrics:
    max_abs: float
    rmse: float
    null_test_energy: float


def read_stereo_csv(path: Path) -> List[Tuple[float, float]]:
    values: List[Tuple[float, float]] = []
    with path.open('r', encoding='utf-8') as f:
        reader = csv.reader(f)
        for row in reader:
            if not row:
                continue
            values.append((float(row[0]), float(row[1])))
    return values


def compute_metrics(reference: List[Tuple[float, float]], candidate: List[Tuple[float, float]]) -> Metrics:
    if len(reference) != len(candidate):
        raise ValueError(f'length mismatch: reference={len(reference)} candidate={len(candidate)}')

    max_abs = 0.0
    sum_sq = 0.0
    n = 0
    for (rl, rr), (cl, cr) in zip(reference, candidate):
        dl = rl - cl
        dr = rr - cr
        max_abs = max(max_abs, abs(dl), abs(dr))
        sum_sq += dl * dl + dr * dr
        n += 2

    rmse = math.sqrt(sum_sq / n) if n else 0.0
    return Metrics(max_abs=max_abs, rmse=rmse, null_test_energy=sum_sq)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--reference', required=True)
    parser.add_argument('--candidate', required=True)
    parser.add_argument('--max-abs-threshold', type=float, default=1e-5)
    parser.add_argument('--rmse-threshold', type=float, default=1e-6)
    args = parser.parse_args()

    reference = read_stereo_csv(Path(args.reference))
    candidate = read_stereo_csv(Path(args.candidate))
    metrics = compute_metrics(reference, candidate)

    print(f'max_abs={metrics.max_abs:.12e}')
    print(f'rmse={metrics.rmse:.12e}')
    print(f'null_test_energy={metrics.null_test_energy:.12e}')

    if metrics.max_abs > args.max_abs_threshold or metrics.rmse > args.rmse_threshold:
        print('FAIL: tolerance exceeded')
        return 1

    print('PASS: within tolerance')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
