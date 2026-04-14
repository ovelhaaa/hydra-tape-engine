#!/usr/bin/env python3
import argparse
import math
import wave
from pathlib import Path


def read_stereo_pcm16(path: Path):
    with wave.open(str(path), 'rb') as wav_file:
        if wav_file.getnchannels() != 2 or wav_file.getsampwidth() != 2:
            raise ValueError(f'{path} must be stereo 16-bit PCM WAV')
        sample_rate = wav_file.getframerate()
        frames = wav_file.readframes(wav_file.getnframes())

    values = []
    for i in range(0, len(frames), 4):
        left = int.from_bytes(frames[i:i + 2], byteorder='little', signed=True) / 32767.0
        right = int.from_bytes(frames[i + 2:i + 4], byteorder='little', signed=True) / 32767.0
        values.append((left, right))
    return sample_rate, values


def clamp_pcm16(value: float) -> int:
    return max(-32768, min(32767, int(round(value * 32767.0))))


def write_stereo_pcm16(path: Path, values, sample_rate: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), 'wb') as wav_file:
        wav_file.setnchannels(2)
        wav_file.setsampwidth(2)
        wav_file.setframerate(sample_rate)
        frames = bytearray()
        for left, right in values:
            frames += clamp_pcm16(left).to_bytes(2, byteorder='little', signed=True)
            frames += clamp_pcm16(right).to_bytes(2, byteorder='little', signed=True)
        wav_file.writeframes(bytes(frames))


def main() -> int:
    parser = argparse.ArgumentParser(description='Null-test between two stereo WAV files')
    parser.add_argument('--reference', required=True)
    parser.add_argument('--candidate', required=True)
    parser.add_argument('--diff-out')
    parser.add_argument('--max-abs-threshold', type=float, default=1e-4)
    parser.add_argument('--rmse-threshold', type=float, default=2e-5)
    args = parser.parse_args()

    ref_sr, reference = read_stereo_pcm16(Path(args.reference))
    cand_sr, candidate = read_stereo_pcm16(Path(args.candidate))

    if ref_sr != cand_sr:
        raise ValueError(f'sample-rate mismatch: {ref_sr} vs {cand_sr}')
    if len(reference) != len(candidate):
        raise ValueError(f'frame mismatch: {len(reference)} vs {len(candidate)}')

    max_abs = 0.0
    err_sq_sum = 0.0
    diff = []
    for (rl, rr), (cl, cr) in zip(reference, candidate):
        dl = rl - cl
        dr = rr - cr
        max_abs = max(max_abs, abs(dl), abs(dr))
        err_sq_sum += dl * dl + dr * dr
        diff.append((dl, dr))

    rmse = math.sqrt(err_sq_sum / (len(reference) * 2.0)) if reference else 0.0
    status = 'PASS' if max_abs <= args.max_abs_threshold and rmse <= args.rmse_threshold else 'FAIL'

    print(f'max_abs={max_abs:.12e}')
    print(f'rmse={rmse:.12e}')
    print(f'null_test_energy={err_sq_sum:.12e}')
    print(f'status={status}')

    if args.diff_out:
        write_stereo_pcm16(Path(args.diff_out), diff, ref_sr)

    return 0 if status == 'PASS' else 1


if __name__ == '__main__':
    raise SystemExit(main())
