"""Render the Hydra Reverse V2 — Rhythmic Evaluation A/B package for Listening Lab."""

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

# Parameter IDs from include/hydra_dsp.h
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
    PARAM_REVERSE_MODE: 0,
    PARAM_REVERSE_DIVISION: 0,
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

    if kind == "impulse":
        # Clear impulse at 0.1s
        idx = int(0.1 * RATE) * 2
        data[idx] = 0.95
        data[idx + 1] = 0.95
    elif kind == "percussive":
        # Simulated kick, snare, and hihat transient pattern
        pattern = [
            (0.0, 60.0, 20.0, 0.9),    # Kick
            (0.25, 8000.0, 60.0, 0.4), # Hihat
            (0.5, 200.0, 15.0, 0.8),   # Snare
            (0.75, 8000.0, 60.0, 0.4), # Hihat
        ]
        beats_total = int(duration_sec * 2) # 120 bpm = 2 beats per sec
        for b in range(beats_total):
            b_time = b * 0.5
            for off, freq, decay, amp in pattern:
                t_start = b_time + off
                if t_start + 0.3 >= duration_sec:
                    continue
                start_frame = int(t_start * RATE)
                env_len = int(0.25 * RATE)
                for f in range(env_len):
                    if start_frame + f >= frames:
                        break
                    t = f / RATE
                    env = amp * math.exp(-decay * t)
                    val = env * math.sin(tau * freq * t)
                    data[2 * (start_frame + f)] += val
                    data[2 * (start_frame + f) + 1] += val
    elif kind == "harmonic":
        # Rich sustained chords (Am7 - Fmaj7)
        chords = [
            [220.0, 261.63, 329.63, 392.0], # Am7
            [174.61, 220.0, 261.63, 329.63],# Fmaj7
        ]
        c_dur = 2.0
        n_chords = int(math.ceil(duration_sec / c_dur))
        for c_idx in range(n_chords):
            c_start = c_idx * c_dur
            c_freqs = chords[c_idx % len(chords)]
            start_frame = int(c_start * RATE)
            dur_frames = int(c_dur * RATE)
            for f in range(dur_frames):
                if start_frame + f >= frames:
                    break
                t = f / RATE
                # envelope with attack and release
                env = min(1.0, t * 15.0) * min(1.0, (c_dur - t) * 15.0) * 0.25
                left = sum(env * math.sin(tau * freq * t) for freq in c_freqs[:3])
                right = sum(env * math.sin(tau * freq * t) for freq in c_freqs[1:])
                data[2 * (start_frame + f)] += left
                data[2 * (start_frame + f) + 1] += right
    elif kind == "melodic":
        # Short melodic phrase (E minor pentatonic)
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
    elif kind == "stereo_melody":
        # Panned melody
        melody = [220.0, 261.63, 329.63, 392.0, 440.0, 392.0, 329.63, 261.63]
        step = 0.25
        n_notes = int(duration_sec / step)
        for i in range(n_notes):
            t_start = i * step
            freq = melody[i % len(melody)]
            pan = 0.5 + 0.4 * math.sin(i * 1.2) # alternating pan
            start_frame = int(t_start * RATE)
            dur_frames = int(step * RATE)
            for f in range(dur_frames):
                if start_frame + f >= frames:
                    break
                t = f / RATE
                env = math.exp(-10.0 * t) * 0.4
                val = env * math.sin(tau * freq * t)
                data[2 * (start_frame + f)] += val * (1.0 - pan)
                data[2 * (start_frame + f) + 1] += val * pan
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

def compute_metrics(a: array.array, b: array.array):
    d = [y - x for x, y in zip(a, b)]
    max_abs = max(abs(x) for x in d)
    peak_frame = max(range(len(d) // 2), key=lambda i: max(abs(d[2*i]), abs(d[2*i+1])))
    rmse = math.sqrt(sum(x * x for x in d) / len(d))
    rms_a = math.sqrt(sum(x * x for x in a) / len(a))
    rms_b = math.sqrt(sum(x * x for x in b) / len(b))
    peak_a = max(abs(x) for x in a)
    peak_b = max(abs(x) for x in b)
    max_step_a = max(abs(a[i] - a[i-1]) for i in range(1, len(a)))
    max_step_b = max(abs(b[i] - b[i-1]) for i in range(1, len(b)))
    return {
        "max_abs": float(max_abs),
        "peak_frame": int(peak_frame),
        "rmse": float(rmse),
        "rms": [float(rms_a), float(rms_b)],
        "peak": [float(peak_a), float(peak_b)],
        "max_step": [float(max_step_a), float(max_step_b)],
    }

def main():
    root = Path(__file__).resolve().parents[1]
    binary = root / "build-native" / "hydra_listen_native.exe"
    if not binary.exists():
        print(f"Error: {binary} does not exist. Build native target first.")
        sys.exit(1)

    out_dir = root / "artifacts" / "reverse-v2-ab"
    out_dir.mkdir(parents=True, exist_ok=True)

    git_commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()

    scenarios_def = [
        {
            "id": "01_reverse_v1_vs_v2",
            "name": "Reverse V1 × Reverse V2",
            "stimulus": "melodic",
            "duration": 5.0,
            "comp_key": "reverse_v1_vs_reverse_v2",
            "name_a": "reverse_v1",
            "name_b": "reverse_v2",
            "label_a": "Reverse V1 (Legacy)",
            "label_b": "Reverse V2 (Rhythmic 1/4)",
            "params_a": {PARAM_REVERSE_MODE: 2, PARAM_DELAY_MS: 500, PARAM_FEEDBACK: 40},
            "params_b": {PARAM_REVERSE_MODE: 0, PARAM_REVERSE_DIVISION: 0, PARAM_BPM: 120, PARAM_FEEDBACK: 40},
            "events_a": [],
            "events_b": [],
            "markers": [{"time": 0.5, "label": "V2 window 1"}, {"time": 1.0, "label": "V2 window 2"}],
        },
        {
            "id": "02_quarter_vs_eighth",
            "name": "Reverse V2 1/4 × 1/8",
            "stimulus": "melodic",
            "duration": 5.0,
            "comp_key": "quarter_vs_eighth",
            "name_a": "quarter",
            "name_b": "eighth",
            "label_a": "Reverse V2 1/4 (500 ms)",
            "label_b": "Reverse V2 1/8 (250 ms)",
            "params_a": {PARAM_REVERSE_MODE: 0, PARAM_REVERSE_DIVISION: 0, PARAM_BPM: 120, PARAM_FEEDBACK: 35},
            "params_b": {PARAM_REVERSE_MODE: 0, PARAM_REVERSE_DIVISION: 3, PARAM_BPM: 120, PARAM_FEEDBACK: 35},
            "events_a": [],
            "events_b": [],
            "markers": [{"time": 0.5, "label": "Quarter boundary"}, {"time": 0.25, "label": "Eighth boundary"}],
        },
        {
            "id": "03_straight_vs_dotted",
            "name": "Reverse V2 Straight × Dotted",
            "stimulus": "percussive",
            "duration": 5.0,
            "comp_key": "straight_vs_dotted",
            "name_a": "straight",
            "name_b": "dotted",
            "label_a": "Reverse V2 Straight 1/8 (250 ms)",
            "label_b": "Reverse V2 Dotted 1/8 (375 ms)",
            "params_a": {PARAM_REVERSE_MODE: 0, PARAM_REVERSE_DIVISION: 3, PARAM_BPM: 120, PARAM_FEEDBACK: 45},
            "params_b": {PARAM_REVERSE_MODE: 0, PARAM_REVERSE_DIVISION: 6, PARAM_BPM: 120, PARAM_FEEDBACK: 45},
            "events_a": [],
            "events_b": [],
            "markers": [{"time": 0.25, "label": "Straight 1/8"}, {"time": 0.375, "label": "Dotted 1/8"}],
        },
        {
            "id": "04_straight_vs_triplet",
            "name": "Reverse V2 Straight × Triplet",
            "stimulus": "percussive",
            "duration": 5.0,
            "comp_key": "straight_vs_triplet",
            "name_a": "straight",
            "name_b": "triplet",
            "label_a": "Reverse V2 Straight 1/4 (500 ms)",
            "label_b": "Reverse V2 Triplet 1/4 (333 ms)",
            "params_a": {PARAM_REVERSE_MODE: 0, PARAM_REVERSE_DIVISION: 0, PARAM_BPM: 120, PARAM_FEEDBACK: 40},
            "params_b": {PARAM_REVERSE_MODE: 0, PARAM_REVERSE_DIVISION: 7, PARAM_BPM: 120, PARAM_FEEDBACK: 40},
            "events_a": [],
            "events_b": [],
            "markers": [{"time": 0.5, "label": "Straight 1/4"}, {"time": 0.333, "label": "Triplet 1/4"}],
        },
        {
            "id": "05_feedback_50_vs_95",
            "name": "Reverse Feedback 50% × 95%",
            "stimulus": "impulse",
            "duration": 6.0,
            "comp_key": "feedback_50_vs_95",
            "name_a": "feedback_50",
            "name_b": "feedback_95",
            "label_a": "Feedback 50%",
            "label_b": "Feedback 95%",
            "params_a": {PARAM_REVERSE_MODE: 0, PARAM_REVERSE_DIVISION: 0, PARAM_BPM: 120, PARAM_FEEDBACK: 50},
            "params_b": {PARAM_REVERSE_MODE: 0, PARAM_REVERSE_DIVISION: 0, PARAM_BPM: 120, PARAM_FEEDBACK: 95},
            "events_a": [],
            "events_b": [],
            "markers": [{"time": 1.0, "label": "Repeat 1"}, {"time": 2.0, "label": "Repeat 2"}, {"time": 3.0, "label": "Repeat 3"}],
        },
        {
            "id": "06_free_vs_rhythmic",
            "name": "Reverse Free × Rhythmic",
            "stimulus": "harmonic",
            "duration": 6.0,
            "comp_key": "free_vs_rhythmic",
            "name_a": "free",
            "name_b": "rhythmic",
            "label_a": "Reverse Free (370 ms)",
            "label_b": "Reverse Rhythmic 1/4 (500 ms)",
            "params_a": {PARAM_REVERSE_MODE: 1, PARAM_DELAY_MS: 370, PARAM_FEEDBACK: 45},
            "params_b": {PARAM_REVERSE_MODE: 0, PARAM_REVERSE_DIVISION: 0, PARAM_BPM: 120, PARAM_FEEDBACK: 45},
            "events_a": [],
            "events_b": [],
            "markers": [{"time": 0.37, "label": "Free hop"}, {"time": 0.50, "label": "Rhythmic hop"}],
        },
        {
            "id": "07_fixed_vs_automated_bpm",
            "name": "BPM Fixo × BPM Automatizado",
            "stimulus": "melodic",
            "duration": 7.0,
            "comp_key": "fixed_vs_automated",
            "name_a": "fixed_bpm",
            "name_b": "automated_bpm",
            "label_a": "BPM Fixo (120 BPM)",
            "label_b": "BPM Automatizado (120 -> 80 -> 140)",
            "params_a": {PARAM_REVERSE_MODE: 0, PARAM_REVERSE_DIVISION: 0, PARAM_BPM: 120, PARAM_FEEDBACK: 40},
            "params_b": {PARAM_REVERSE_MODE: 0, PARAM_REVERSE_DIVISION: 0, PARAM_BPM: 120, PARAM_FEEDBACK: 40},
            "events_a": [],
            "events_b": [
                (int(2.5 * RATE / BLOCK) * BLOCK, PARAM_BPM, 80.0),
                (int(5.0 * RATE / BLOCK) * BLOCK, PARAM_BPM, 140.0),
            ],
            "markers": [{"time": 2.5, "label": "BPM → 80"}, {"time": 5.0, "label": "BPM → 140"}],
        },
        {
            "id": "08_stereo_imaging",
            "name": "Reverse V2 Mono × Estéreo Ping-Pong",
            "stimulus": "stereo_melody",
            "duration": 5.0,
            "comp_key": "mono_vs_stereo",
            "name_a": "mono",
            "name_b": "stereo",
            "label_a": "Reverse V2 Mono (Ping-Pong OFF)",
            "label_b": "Reverse V2 Ping-Pong ON",
            "params_a": {PARAM_REVERSE_MODE: 0, PARAM_REVERSE_DIVISION: 0, PARAM_BPM: 120, PARAM_FEEDBACK: 50, PARAM_PING_PONG: 0},
            "params_b": {PARAM_REVERSE_MODE: 0, PARAM_REVERSE_DIVISION: 0, PARAM_BPM: 120, PARAM_FEEDBACK: 50, PARAM_PING_PONG: 1},
            "events_a": [],
            "events_b": [],
            "markers": [{"time": 0.5, "label": "Bounce L"}, {"time": 1.0, "label": "Bounce R"}],
        },
        {
            "id": "09_percussive_material",
            "name": "Material Percussivo Dry × Reverse V2",
            "stimulus": "percussive",
            "duration": 5.0,
            "comp_key": "percussive_dry_vs_reverse",
            "name_a": "dry",
            "name_b": "reverse",
            "label_a": "Sinal Seco (Percussivo)",
            "label_b": "Reverse V2 1/8 Dotted",
            "params_a": {PARAM_DELAY_ACTIVE: 0, PARAM_DRY_WET: 100, PARAM_DELAY_WET: 0},
            "params_b": {PARAM_REVERSE_MODE: 0, PARAM_REVERSE_DIVISION: 6, PARAM_BPM: 120, PARAM_FEEDBACK: 35, PARAM_DRY_WET: 100, PARAM_DELAY_WET: 100},
            "events_a": [],
            "events_b": [],
            "markers": [{"time": 0.375, "label": "First dotted echo"}],
        },
        {
            "id": "10_harmonic_material",
            "name": "Material Harmônico Dry × Reverse V2",
            "stimulus": "harmonic",
            "duration": 6.0,
            "comp_key": "harmonic_dry_vs_reverse",
            "name_a": "dry",
            "name_b": "reverse",
            "label_a": "Sinal Seco (Acordes)",
            "label_b": "Reverse V2 Rhythmic 1/4",
            "params_a": {PARAM_DELAY_ACTIVE: 0, PARAM_DRY_WET: 100, PARAM_DELAY_WET: 0},
            "params_b": {PARAM_REVERSE_MODE: 0, PARAM_REVERSE_DIVISION: 0, PARAM_BPM: 120, PARAM_FEEDBACK: 50, PARAM_DRY_WET: 100, PARAM_DELAY_WET: 100},
            "events_a": [],
            "events_b": [],
            "markers": [{"time": 0.5, "label": "First chord swell"}],
        },
    ]

    manifest = {
        "suite": "Hydra Reverse V2 — Rhythmic Evaluation",
        "git_commit": git_commit,
        "sample_rate_hz": RATE,
        "sign_off": "PENDING HUMAN REVIEW",
    }
    manifest_scenarios = {}
    provenance_hashes = {}

    for sc in scenarios_def:
        sc_id = sc["id"]
        sc_folder = out_dir / sc_id
        sc_folder.mkdir(parents=True, exist_ok=True)

        # Generate stimulus
        stim_data = synthesize_stimulus(sc["stimulus"], sc["duration"])
        stim_path = sc_folder / "input.f32"
        with stim_path.open("wb") as f:
            stim_data.tofile(f)

        # Write input.wav
        write_wav_24bit(sc_folder / "input.wav", stim_data)
        provenance_hashes[f"{sc_id}/input.wav"] = sha256_file(sc_folder / "input.wav")

        total_samples = len(stim_data)

        # Render A
        events_a = [(0, k, v) for k, v in (BASE_PARAMS | sc["params_a"]).items()] + sc["events_a"]
        out_raw_a = sc_folder / f"{sc['name_a']}.f32"
        samples_a = run_render(binary, stim_path, events_a, out_raw_a, total_samples)
        wav_path_a = sc_folder / f"{sc['name_a']}.wav"
        write_wav_24bit(wav_path_a, samples_a)
        provenance_hashes[f"{sc_id}/{sc['name_a']}.wav"] = sha256_file(wav_path_a)

        # Render B
        events_b = [(0, k, v) for k, v in (BASE_PARAMS | sc["params_b"]).items()] + sc["events_b"]
        out_raw_b = sc_folder / f"{sc['name_b']}.f32"
        samples_b = run_render(binary, stim_path, events_b, out_raw_b, total_samples)
        wav_path_b = sc_folder / f"{sc['name_b']}.wav"
        write_wav_24bit(wav_path_b, samples_b)
        provenance_hashes[f"{sc_id}/{sc['name_b']}.wav"] = sha256_file(wav_path_b)

        # Compute comparison metrics
        metrics = compute_metrics(samples_a, samples_b)

        manifest_scenarios[sc_id] = {
            "name": sc["name"],
            "rate": RATE,
            "duration_sec": sc["duration"],
            "frames": total_samples // 2,
            "stimulus": sc["stimulus"],
            "listening_markers": sc["markers"],
            "comparisons": {
                sc["comp_key"]: {
                    "pair": [sc["name_a"], sc["name_b"], f"{sc['label_a']} × {sc['label_b']}"],
                    "peak_frame": metrics["peak_frame"],
                    "max_abs": metrics["max_abs"],
                    "rmse": metrics["rmse"],
                    "rms": metrics["rms"],
                    "peak": metrics["peak"],
                    "max_step": metrics["max_step"],
                }
            }
        }
        print(f"Rendered {sc_id} -> RMSE={metrics['rmse']:.6f}, MaxAbs={metrics['max_abs']:.4f}")

    manifest.update(manifest_scenarios)
    manifest_path = out_dir / "manifest.json"
    with manifest_path.open("w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)

    print(f"\nSuccessfully generated Listening Lab package at: {out_dir}")
    print(f"Manifest written to: {manifest_path}")

if __name__ == "__main__":
    main()
