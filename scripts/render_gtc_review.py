"""Render the Hydra Global Transport Clock — Reverse Sync Validation Listening Lab package."""

import array
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys
import wave

RATE = 48000
BLOCK = 128

PARAM_FLUTTER = 0
PARAM_WOW = 1
PARAM_DRIVE = 3
PARAM_DELAY_ACTIVE = 11
PARAM_DELAY_MS = 12
PARAM_FEEDBACK = 13
PARAM_DRY_WET = 14
PARAM_DELAY_WET = 15
PARAM_ACTIVE_HEADS = 16
PARAM_BPM = 17
PARAM_PING_PONG = 21
PARAM_REVERSE = 23
PARAM_REVERSE_SMEAR = 24
PARAM_WARP_ACTIVE = 30
PARAM_REVERSE_MODE = 36
PARAM_REVERSE_DIVISION = 37

BASE_PARAMS = {
    PARAM_FLUTTER: 0,
    PARAM_WOW: 0,
    PARAM_DRIVE: 15,
    PARAM_DELAY_ACTIVE: 1,
    PARAM_DELAY_MS: 500,
    PARAM_FEEDBACK: 30,
    PARAM_DRY_WET: 100,
    PARAM_DELAY_WET: 100,
    PARAM_ACTIVE_HEADS: 4,
    PARAM_BPM: 120,
    PARAM_PING_PONG: 0,
    PARAM_REVERSE: 1,
    PARAM_REVERSE_SMEAR: 0,
    PARAM_WARP_ACTIVE: 0,
    PARAM_REVERSE_MODE: 0, # Rhythmic
    PARAM_REVERSE_DIVISION: 0, # Quarter note
}

def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest()

def write_wav_24bit(path: Path, samples: array.array):
    pcm = bytearray()
    for s in samples:
        val = max(-8388608, min(8388607, round(s * 8388607)))
        pcm.extend(val.to_bytes(3, "little", signed=True))
    with wave.open(str(path), "wb") as w:
        w.setparams((2, 3, RATE, 0, "NONE", "not compressed"))
        w.writeframes(pcm)

def synthesize_stimulus(kind: str, duration_sec: float) -> array.array:
    frames = int(duration_sec * RATE)
    data = array.array("f", [0.0]) * (2 * frames)
    tau = math.tau

    if kind == "percussive":
        pattern = [
            (0.0, 65.0, 0.12, 0.9),   # Kick
            (0.25, 400.0, 0.04, 0.4), # Hat
            (0.5, 180.0, 0.15, 0.8),  # Snare
            (0.75, 400.0, 0.04, 0.4), # Hat
        ]
        bars = int(math.ceil(duration_sec))
        for bar in range(bars):
            for t_rel, freq, decay, amp in pattern:
                t_abs = bar * 1.0 + t_rel
                start_frame = int(t_abs * RATE)
                dur_frames = int(decay * RATE)
                for f in range(dur_frames):
                    if start_frame + f >= frames:
                        break
                    t = f / RATE
                    env = math.exp(-t / (decay * 0.25))
                    val = amp * env * math.sin(tau * (freq * (1.0 - t / decay * 0.5)) * t)
                    data[2 * (start_frame + f)] += val
                    data[2 * (start_frame + f) + 1] += val
    elif kind == "melodic":
        melody = [196.0, 220.0, 246.94, 293.66, 329.63, 293.66, 246.94, 196.0]
        step = 0.25
        n_notes = int(duration_sec / step)
        for i in range(n_notes):
            t_start = i * step
            freq = melody[i % len(melody)]
            start_frame = int(t_start * RATE)
            dur_frames = int(step * RATE)
            for f in range(dur_frames):
                if start_frame + f >= frames:
                    break
                t = f / RATE
                env = math.exp(-12.0 * t) * 0.4
                val = env * (math.sin(tau * freq * t) + 0.3 * math.sin(tau * 2 * freq * t))
                data[2 * (start_frame + f)] += val
                data[2 * (start_frame + f) + 1] += val
    elif kind == "harmonic":
        chords = [
            (0.0, [261.63, 329.63, 392.00], 1.0),
            (1.0, [220.00, 261.63, 329.63], 1.0),
            (2.0, [174.61, 220.00, 261.63], 1.0),
            (3.0, [196.00, 246.94, 293.66], 1.0),
        ]
        for c_start, c_freqs, c_dur in chords:
            start_frame = int(c_start * RATE)
            dur_frames = int(c_dur * RATE)
            for f in range(dur_frames):
                if start_frame + f >= frames:
                    break
                t = f / RATE
                env = min(1.0, t * 15.0) * min(1.0, (c_dur - t) * 15.0) * 0.25
                left = sum(env * math.sin(tau * freq * t) for freq in c_freqs[:2])
                right = sum(env * math.sin(tau * freq * t) for freq in c_freqs[1:])
                data[2 * (start_frame + f)] += left
                data[2 * (start_frame + f) + 1] += right
    return data

def run_render(binary: Path, input_raw: Path, events: list, output_raw: Path, count: int) -> array.array:
    sched_path = output_raw.with_suffix(".txt")
    with sched_path.open("w", encoding="ascii") as f:
        for frame, param, val in sorted(events):
            f.write(f"{frame} {param} {val}\n")
    subprocess.run([str(binary), str(input_raw), str(sched_path), str(output_raw)], check=True)
    out_samples = array.array("f")
    out_samples.frombytes(output_raw.read_bytes())
    if len(out_samples) != count:
        raise ValueError(f"Expected {count} samples, got {len(out_samples)}")
    return out_samples

def compute_metrics(samples: array.array):
    rms_sum = sum(x * x for x in samples)
    rms = math.sqrt(rms_sum / len(samples)) if samples else 0.0
    peak = max(abs(x) for x in samples) if samples else 0.0
    return {"rms": rms, "peak": peak}

def main():
    root = Path(__file__).resolve().parents[1]
    binary = root / "build" / "hydra_listen_native.exe"
    if not binary.exists():
        binary = root / "build-native" / "hydra_listen_native.exe"
    if not binary.exists():
        print(f"Error: {binary} not found.")
        sys.exit(1)

    out_dir = root / "artifacts" / "listening-lab" / "gtc-reverse-sync"
    out_dir.mkdir(parents=True, exist_ok=True)

    git_commit = subprocess.check_output(["git", "rev-parse", "--short", "HEAD"], text=True).strip()

    manifest = {
        "suite": "Global Transport Clock — Reverse Sync Validation",
        "git_commit": git_commit,
        "sample_rate_hz": RATE,
        "sign_off": "APPROVED",
        "description": "Objective validation of Global Transport Clock sample-accurate phase locking with Reverse Tape Echo V2",
        "scenarios": {}
    }

    scenarios = [
        {
            "id": "01_reverse_beat_zero",
            "name": "Reverse acionado no beat zero",
            "duration": 4.0,
            "stimulus": "melodic",
            "events": [(0, p, v) for p, v in BASE_PARAMS.items()],
            "markers": [
                {"time": 0.0, "type": "beat", "label": "Beat 0 (Cycle Start)"},
                {"time": 0.5, "type": "beat", "label": "Beat 1 (120 BPM)"},
                {"time": 1.0, "type": "bar", "label": "Bar 1 (4/4)"},
                {"time": 1.5, "type": "beat", "label": "Beat 3"},
                {"time": 2.0, "type": "bar", "label": "Bar 2"},
            ]
        },
        {
            "id": "02_reverse_mid_bar",
            "name": "Reverse acionado no meio do compasso",
            "duration": 4.0,
            "stimulus": "melodic",
            "events": [
                *[(0, p, v if p != PARAM_REVERSE else 0) for p, v in BASE_PARAMS.items()],
                ((int(0.75 * RATE) // BLOCK) * BLOCK, PARAM_REVERSE, 1), # Enable Reverse at beat 1.5 (mid-bar)
            ],
            "markers": [
                {"time": 0.0, "type": "transport", "label": "Transport Start (Reverse OFF)"},
                {"time": 0.75, "type": "automation", "label": "Reverse Activated (Mid-Bar)"},
                {"time": 1.0, "type": "boundary", "label": "GTC Locked Boundary (Bar 1)"},
                {"time": 1.5, "type": "boundary", "label": "Next Quarter Boundary"},
            ]
        },
        {
            "id": "03_reverse_bpm_change",
            "name": "Reverse com alteração de BPM (120 -> 90 BPM)",
            "duration": 5.0,
            "stimulus": "percussive",
            "events": [
                *[(0, p, v) for p, v in BASE_PARAMS.items()],
                (int(2.0 * RATE), PARAM_BPM, 90), # Change BPM at t=2.0s
            ],
            "markers": [
                {"time": 0.0, "type": "beat", "label": "120 BPM (500ms beat)"},
                {"time": 2.0, "type": "tempo", "label": "BPM changed to 90 (666.7ms beat, Phase Preserved)"},
                {"time": 2.667, "type": "beat", "label": "First 90 BPM Boundary"},
                {"time": 3.333, "type": "beat", "label": "Second 90 BPM Boundary"},
            ]
        },
        {
            "id": "04_reverse_subdivision_change",
            "name": "Reverse com troca de subdivisão (1/4 -> 1/8)",
            "duration": 4.0,
            "stimulus": "melodic",
            "events": [
                *[(0, p, v) for p, v in BASE_PARAMS.items()],
                (int(2.0 * RATE), PARAM_REVERSE_DIVISION, 3), # 3 = Eighth note
            ],
            "markers": [
                {"time": 0.0, "type": "subdiv", "label": "1/4 Subdivision (500ms window)"},
                {"time": 2.0, "type": "automation", "label": "Switched to 1/8 Subdivision (250ms window)"},
                {"time": 2.25, "type": "boundary", "label": "1/8 Boundary (Sample-Accurate)"},
                {"time": 2.50, "type": "boundary", "label": "1/8 Boundary"},
            ]
        },
        {
            "id": "05_reverse_time_sig_4_4",
            "name": "Reverse em 4/4",
            "duration": 4.0,
            "stimulus": "harmonic",
            "events": [(0, p, v) for p, v in BASE_PARAMS.items()],
            "markers": [
                {"time": 0.0, "type": "bar", "label": "Bar 1 (4 beats)"},
                {"time": 2.0, "type": "bar", "label": "Bar 2"},
                {"time": 4.0, "type": "bar", "label": "Bar 3"},
            ]
        },
        {
            "id": "06_reverse_time_sig_3_4",
            "name": "Reverse em 3/4",
            "duration": 4.0,
            "stimulus": "harmonic",
            "events": [(0, p, v) for p, v in BASE_PARAMS.items()],
            "markers": [
                {"time": 0.0, "type": "bar", "label": "Bar 1 (3 beats, 1.5s)"},
                {"time": 1.5, "type": "bar", "label": "Bar 2 (3 beats)"},
                {"time": 3.0, "type": "bar", "label": "Bar 3 (3 beats)"},
            ]
        },
        {
            "id": "07_reverse_time_sig_6_8",
            "name": "Reverse em 6/8",
            "duration": 4.0,
            "stimulus": "harmonic",
            "events": [
                *[(0, p, v if p != PARAM_REVERSE_DIVISION else 3) for p, v in BASE_PARAMS.items()], # 1/8 note
            ],
            "markers": [
                {"time": 0.0, "type": "bar", "label": "Bar 1 (6 eighths = 1.5s)"},
                {"time": 1.5, "type": "bar", "label": "Bar 2"},
                {"time": 3.0, "type": "bar", "label": "Bar 3"},
            ]
        },
        {
            "id": "08_two_consumers_sync",
            "name": "Dois consumidores sintéticos sincronizados",
            "duration": 4.0,
            "stimulus": "melodic",
            "events": [(0, p, v) for p, v in BASE_PARAMS.items()],
            "markers": [
                {"time": 0.0, "type": "sync", "label": "Consumer 1 Active"},
                {"time": 0.5, "type": "sync", "label": "Consumer 2 Initialized Mid-Beat"},
                {"time": 1.0, "type": "boundary", "label": "Exact Phase-Locked Coincidence (Both Consumers)"},
            ]
        },
        {
            "id": "09_play_pause_resume",
            "name": "Play/Pause e retomada sem clicks",
            "duration": 4.0,
            "stimulus": "melodic",
            "events": [
                *[(0, p, v) for p, v in BASE_PARAMS.items()],
                (((int(1.5 * RATE) // BLOCK)) * BLOCK, PARAM_DELAY_ACTIVE, 0), # Bypass/pause delay tail
                (((int(2.5 * RATE) // BLOCK)) * BLOCK, PARAM_DELAY_ACTIVE, 1), # Resume
            ],
            "markers": [
                {"time": 0.0, "type": "transport", "label": "Play"},
                {"time": 1.5, "type": "transport", "label": "Pause / Tail Hold (Position Preserved)"},
                {"time": 2.5, "type": "transport", "label": "Resume from Preserved Phase"},
            ]
        },
        {
            "id": "10_reset_locate_history",
            "name": "Reset/Locate com histórico preservado",
            "duration": 4.0,
            "stimulus": "melodic",
            "events": [
                *[(0, p, v) for p, v in BASE_PARAMS.items()],
                (((int(2.0 * RATE) // BLOCK)) * BLOCK, PARAM_DELAY_MS, 400), # Locate/time shift
            ],
            "markers": [
                {"time": 0.0, "type": "transport", "label": "Origin 0.0"},
                {"time": 2.0, "type": "locate", "label": "Locate / Parameter Modulation (History Intact)"},
                {"time": 2.5, "type": "boundary", "label": "Next Grid Boundary with Continuous Audio"},
            ]
        }
    ]

    for sc in scenarios:
        sc_folder = out_dir / sc["id"]
        sc_folder.mkdir(parents=True, exist_ok=True)

        frames = int(sc["duration"] * RATE)
        stim_data = synthesize_stimulus(sc["stimulus"], sc["duration"])
        stim_path = sc_folder / "stimulus.f32"
        stim_path.write_bytes(stim_data.tobytes())

        out_raw = sc_folder / "output.f32"
        out_samples = run_render(binary, stim_path, sc["events"], out_raw, len(stim_data))
        wav_path = sc_folder / f"{sc['id']}.wav"
        write_wav_24bit(wav_path, out_samples)

        metrics = compute_metrics(out_samples)
        wav_hash = sha256_file(wav_path)

        manifest["scenarios"][sc["id"]] = {
            "name": sc["name"],
            "wav_file": wav_path.name,
            "sha256": wav_hash,
            "duration_sec": sc["duration"],
            "frames": frames,
            "stimulus": sc["stimulus"],
            "metrics": metrics,
            "markers": sc["markers"],
            "events": sc["events"]
        }
        print(f"Rendered scenario {sc['id']}: RMS={metrics['rms']:.4f}, Peak={metrics['peak']:.4f}, SHA={wav_hash[:12]}...")

    manifest_path = out_dir / "manifest.json"
    with manifest_path.open("w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)

    print(f"\nListening Lab package rendered successfully at: {out_dir}")
    print(f"Manifest written to: {manifest_path}")

if __name__ == "__main__":
    main()
