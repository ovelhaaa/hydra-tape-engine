#!/usr/bin/env python3
"""
Accidental Warp V1.2 — Multi-Engine Sonic Fidelity & A/B Validation Suite.

Executes a tri-engine comparison:
  Engine A: Original JavaScript AudioWorklet extracted dynamically from
            docs/simulators/accidental_warp_simulator.html (via Node.js runner)
  Engine B: Python Reference Model (SimulatorReferenceDSP)
  Engine C: Native C++ TapeCore DSP Engine (hydra_listen_native.exe)

Runs the 12 mandatory validation scenarios, computes objective metrics
(FFT dominant frequency, RMS, peak, L/R decorrelation, cross-engine RMS error,
stability, NaN/Inf checks), generates comparative 24-bit 48kHz WAV audio files,
and records an authoritative manifest.json.
"""

import json
import math
import os
import struct
import subprocess
import sys
import wave
from datetime import datetime, timezone
from pathlib import Path

import numpy as np
from scipy.fft import rfft, rfftfreq

ROOT = Path(__file__).resolve().parents[1]
OUTPUT_DIR = ROOT / "tests" / "output" / "warp_ab_validation"
LISTEN_EXE = ROOT / "build-native" / "hydra_listen_native.exe"
NODE_RUNNER = ROOT / "scripts" / "run_simulator_worklet.js"

TWO_PI = 6.28318530717958647692

# ==============================================================================
# Engine B: Python Reference DSP Model
# ==============================================================================

class ReferenceDCBlocker:
    def __init__(self, r=0.995):
        self.r = r
        self.x1 = 0.0
        self.y1 = 0.0

    def process(self, x):
        y = x - self.x1 + self.r * self.y1
        self.x1 = x
        self.y1 = y
        return y


class ReferenceBiquad:
    def __init__(self):
        self.b0 = 1.0; self.b1 = 0.0; self.b2 = 0.0
        self.a1 = 0.0; self.a2 = 0.0
        self.z1 = 0.0; self.z2 = 0.0

    def set_lowpass(self, fs, freq, q=0.707):
        safe_freq = max(20.0, min(freq, fs * 0.45))
        w0 = TWO_PI * safe_freq / fs
        cosw0 = math.cos(w0)
        sinw0 = math.sin(w0)
        alpha = sinw0 / (2.0 * q)
        a0 = 1.0 + alpha
        self.b0 = ((1.0 - cosw0) / 2.0) / a0
        self.b1 = (1.0 - cosw0) / a0
        self.b2 = self.b0
        self.a1 = (-2.0 * cosw0) / a0
        self.a2 = (1.0 - alpha) / a0

    def set_highpass(self, fs, freq, q=0.707):
        safe_freq = max(20.0, min(freq, fs * 0.45))
        w0 = TWO_PI * safe_freq / fs
        cosw0 = math.cos(w0)
        sinw0 = math.sin(w0)
        alpha = sinw0 / (2.0 * q)
        a0 = 1.0 + alpha
        self.b0 = ((1.0 + cosw0) / 2.0) / a0
        self.b1 = -(1.0 + cosw0) / a0
        self.b2 = self.b0
        self.a1 = (-2.0 * cosw0) / a0
        self.a2 = (1.0 - alpha) / a0

    def process(self, x):
        y = self.b0 * x + self.z1
        self.z1 = self.b1 * x - self.a1 * y + self.z2
        self.z2 = self.b2 * x - self.a2 * y
        if abs(self.z1) < 1e-20: self.z1 = 0.0
        if abs(self.z2) < 1e-20: self.z2 = 0.0
        return y


class ReferenceDelayLine:
    def __init__(self, size):
        self.size = size
        self.buffer = [0.0] * size
        self.write_idx = 0

    def read_hermite(self, delay, active_size):
        max_d = active_size - 4
        if delay < 2.0: delay = 2.0
        if delay > max_d: delay = max_d

        read_pos = self.write_idx - delay
        while read_pos < 0.0: read_pos += active_size
        while read_pos >= active_size: read_pos -= active_size

        idx1 = int(math.floor(read_pos))
        idx0 = (idx1 - 1 + active_size) % active_size
        idx2 = (idx1 + 1) % active_size
        idx3 = (idx1 + 2) % active_size
        f = read_pos - idx1

        y0 = self.buffer[idx0]; y1 = self.buffer[idx1]
        y2 = self.buffer[idx2]; y3 = self.buffer[idx3]

        c0 = y1
        c1 = 0.5 * (y2 - y0)
        c2 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3
        c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2)
        return ((c3 * f + c2) * f + c1) * f + c0

    def read_int(self, delay, active_size):
        max_d = active_size - 4
        if delay < 2.0: delay = 2.0
        if delay > max_d: delay = max_d
        read_pos = self.write_idx - delay
        while read_pos < 0.0: read_pos += active_size
        while read_pos >= active_size: read_pos -= active_size
        return self.buffer[int(read_pos) % active_size]


class ReferenceAccidentalWarpDSP:
    def __init__(self, fs=48000.0, grain_ms=80.0, speed=1.0, stereo=0.5, feedback=0.0, mode=0):
        self.fs = fs
        self.max_samples = int(fs * 2.5)
        self.delay_l = ReferenceDelayLine(self.max_samples)
        self.delay_r = ReferenceDelayLine(self.max_samples)

        self.fb_lpf_l = ReferenceBiquad()
        self.fb_lpf_r = ReferenceBiquad()
        self.fb_hpf_l = ReferenceBiquad()
        self.fb_hpf_r = ReferenceBiquad()
        self.dc_l = ReferenceDCBlocker()
        self.dc_r = ReferenceDCBlocker()

        self.fb_lpf_l.set_lowpass(fs, 2200.0, 0.707)
        self.fb_lpf_r.set_lowpass(fs, 2200.0, 0.707)
        self.fb_hpf_l.set_highpass(fs, 120.0, 0.707)
        self.fb_hpf_r.set_highpass(fs, 120.0, 0.707)

        self.grain_ms = grain_ms
        self.speed = speed
        self.stereo = stereo
        self.feedback = feedback * 0.01
        self.mode = mode
        self.fb_drive = 1.60

        self.d_samples = max(64, int(grain_ms * 0.001 * fs))
        self.d_size = self.d_samples if (mode == 0 or mode == 3) else (self.d_samples + 1000)
        self.write_pos = 0.0

    def set_grain_ms(self, grain_ms):
        self.grain_ms = grain_ms
        self.d_samples = max(64, int(grain_ms * 0.001 * self.fs))
        self.d_size = self.d_samples if (self.mode == 0 or self.mode == 3) else (self.d_samples + 1000)
        if self.write_pos >= self.d_size: self.write_pos = 0.0
        if self.delay_l.write_idx >= self.d_size: self.delay_l.write_idx = 0
        if self.delay_r.write_idx >= self.d_size: self.delay_r.write_idx = 0

    def process_frame(self, dry_l, dry_r):
        d_size = self.d_size
        d_samples = self.d_samples
        speed_mult = self.speed
        fb = self.feedback
        mode = self.mode

        if mode == 1 or mode == 2:
            read_pos = self.write_pos + d_samples
            while read_pos >= d_size: read_pos -= d_size
            while read_pos < 0.0: read_pos += d_size

            rel_d = d_size - read_pos
            norm_pos = rel_d / d_size
            env = 1.0
            declick = 0.05
            if norm_pos < declick: env = norm_pos / declick
            elif norm_pos > (1.0 - declick): env = (1.0 - norm_pos) / declick

            if mode == 2:
                rel_d = math.floor(rel_d)
                wet_l = self.delay_l.read_int(rel_d, d_size) * env
                wet_r = self.delay_r.read_int(rel_d, d_size) * env
            else:
                wet_l = self.delay_l.read_hermite(rel_d, d_size) * env
                wet_r = self.delay_r.read_hermite(rel_d, d_size) * env

            proc_l = self.fb_lpf_l.process(self.fb_hpf_l.process(wet_l))
            proc_r = self.fb_lpf_r.process(self.fb_hpf_r.process(wet_r))
            fb_l = proc_l * fb
            fb_r = proc_r * fb
        else:
            base_pos = self.write_pos + d_samples
            while base_pos >= d_size: base_pos -= d_size
            while base_pos < 0.0: base_pos += d_size

            phase1 = base_pos / d_size
            phase2 = (phase1 + 0.5) if (phase1 + 0.5 < 1.0) else (phase1 - 0.5)

            gain1 = 1.0 - abs(2.0 * phase1 - 1.0)
            gain2 = 1.0 - abs(2.0 * phase2 - 1.0)

            rel_d1_l = (1.0 - phase1) * d_size
            rel_d2_l = (1.0 - phase2) * d_size

            r_offset = self.stereo * 0.25
            phase1_r = phase1 + r_offset
            if phase1_r >= 1.0: phase1_r -= 1.0
            phase2_r = (phase1_r + 0.5) if (phase1_r + 0.5 < 1.0) else (phase1_r - 0.5)
            gain1_r = 1.0 - abs(2.0 * phase1_r - 1.0)
            gain2_r = 1.0 - abs(2.0 * phase2_r - 1.0)
            rel_d1_r = (1.0 - phase1_r) * d_size
            rel_d2_r = (1.0 - phase2_r) * d_size

            if mode == 3:
                rel_d1_l = math.floor(rel_d1_l); rel_d2_l = math.floor(rel_d2_l)
                rel_d1_r = math.floor(rel_d1_r); rel_d2_r = math.floor(rel_d2_r)
                wet_l = self.delay_l.read_int(rel_d1_l, d_size) * gain1 + self.delay_l.read_int(rel_d2_l, d_size) * gain2
                wet_r = self.delay_r.read_int(rel_d1_r, d_size) * gain1_r + self.delay_r.read_int(rel_d2_r, d_size) * gain2_r
            else:
                wet_l = self.delay_l.read_hermite(rel_d1_l, d_size) * gain1 + self.delay_l.read_hermite(rel_d2_l, d_size) * gain2
                wet_r = self.delay_r.read_hermite(rel_d1_r, d_size) * gain1_r + self.delay_r.read_hermite(rel_d2_r, d_size) * gain2_r

            proc_l = self.fb_lpf_l.process(self.fb_hpf_l.process(wet_l))
            proc_r = self.fb_lpf_r.process(self.fb_hpf_r.process(wet_r))
            if self.fb_drive > 1.01:
                proc_l = math.tanh(proc_l * self.fb_drive) / self.fb_drive
                proc_r = math.tanh(proc_r * self.fb_drive) / self.fb_drive
            fb_l = proc_l * fb
            fb_r = proc_r * fb

        self.delay_l.buffer[self.delay_l.write_idx] = self.dc_l.process(dry_l + fb_l)
        self.delay_l.write_idx = (self.delay_l.write_idx + 1) % d_size
        self.delay_r.buffer[self.delay_r.write_idx] = self.dc_r.process(dry_r + fb_r)
        self.delay_r.write_idx = (self.delay_r.write_idx + 1) % d_size

        self.write_pos += speed_mult
        if self.write_pos >= d_size: self.write_pos -= d_size

        return wet_l, wet_r

# ==============================================================================
# File I/O & Runners
# ==============================================================================

def write_wav(path, signal_stereo, fs=48000):
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), 'wb') as wav:
        wav.setnchannels(2)
        wav.setsampwidth(3) # 24-bit PCM
        wav.setframerate(fs)
        frames = bytearray()
        for l, r in signal_stereo:
            clamped_l = max(-1.0, min(1.0, float(l)))
            clamped_r = max(-1.0, min(1.0, float(r)))
            val_l = int(clamped_l * 8388607.0)
            val_r = int(clamped_r * 8388607.0)
            frames += struct.pack('<i', val_l)[:3]
            frames += struct.pack('<i', val_r)[:3]
        wav.writeframes(frames)


def run_engine_a_worklet(input_stereo, params_dict, fs=48000):
    """Engine A: Node.js worker evaluating exact original AudioWorklet from HTML."""
    in_raw_path = OUTPUT_DIR / "temp_in_a.f32"
    out_raw_path = OUTPUT_DIR / "temp_out_a.f32"
    params_json_path = OUTPUT_DIR / "temp_params_a.json"
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    with open(in_raw_path, "wb") as f:
        for l, r in input_stereo:
            f.write(struct.pack("<ff", float(l), float(r)))

    p_worklet = {
        "sampleRate": fs,
        "speed": params_dict.get("warpSpeed", 1.0),
        "grainMs": params_dict.get("warpGrainMs", 80.0),
        "stereo": params_dict.get("warpStereo", 0.5),
        "feedback": params_dict.get("warpFeedback", 0.0),
        "mode": params_dict.get("warpMode", 0)
    }
    with open(params_json_path, "w") as f:
        json.dump(p_worklet, f)

    cmd = ["node", str(NODE_RUNNER), str(in_raw_path), str(params_json_path), str(out_raw_path)]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        raise RuntimeError(f"Engine A (Node AudioWorklet) failed: {res.stderr}\n{res.stdout}")

    out_stereo = []
    with open(out_raw_path, "rb") as f:
        raw = f.read()
        num_floats = len(raw) // 4
        floats = struct.unpack(f"<{num_floats}f", raw)
        for i in range(0, len(floats), 2):
            out_stereo.append((floats[i], floats[i+1]))

    for p in [in_raw_path, out_raw_path, params_json_path]:
        if p.exists(): p.unlink()

    return out_stereo


def run_engine_c_native(input_stereo, params_dict, automation_events=None, fs=48000):
    """Engine C: Compiled C++ TapeCore binary (hydra_listen_native.exe)."""
    in_raw_path = OUTPUT_DIR / "temp_in_c.f32"
    out_raw_path = OUTPUT_DIR / "temp_out_c.f32"
    auto_path = OUTPUT_DIR / "temp_auto_c.txt"
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    with open(in_raw_path, "wb") as f:
        for l, r in input_stereo:
            f.write(struct.pack("<ff", float(l), float(r)))

    # Base parameters at frame 0
    base_params = {
        3: 20.0,   # drive
        11: 1.0,   # delayActive
        12: 100.0, # delayTimeMs
        13: 0.0,   # feedback (tape)
        14: 100.0, # dryWet
        15: 100.0, # delayWet
        30: float(params_dict.get("warpActive", 1.0)),
        31: float(params_dict.get("warpSpeed", 1.0)),
        32: float(params_dict.get("warpGrainMs", 80.0)),
        33: float(params_dict.get("warpStereo", 0.5)),
        34: float(params_dict.get("warpFeedback", 0.0)),
        35: float(params_dict.get("warpMode", 0.0)),
    }

    with open(auto_path, "w") as f:
        for pid, val in sorted(base_params.items()):
            f.write(f"0 {pid} {val}\n")
        if automation_events:
            sorted_events = sorted(automation_events, key=lambda e: (int(e[0]) // 128) * 128)
            for frame, pid, val in sorted_events:
                aligned_frame = (int(frame) // 128) * 128
                if aligned_frame > 0:
                    f.write(f"{aligned_frame} {pid} {val}\n")

    res = subprocess.run([str(LISTEN_EXE), str(in_raw_path), str(auto_path), str(out_raw_path)],
                         capture_output=True, text=True)
    if res.returncode != 0:
        raise RuntimeError(f"Engine C (Native C++ TapeCore) failed: {res.stderr}")

    out_stereo = []
    with open(out_raw_path, "rb") as f:
        raw = f.read()
        num_floats = len(raw) // 4
        floats = struct.unpack(f"<{num_floats}f", raw)
        for i in range(0, len(floats), 2):
            out_stereo.append((floats[i], floats[i+1]))

    for p in [in_raw_path, out_raw_path, auto_path]:
        if p.exists(): p.unlink()

    return out_stereo

# ==============================================================================
# Objective Metrics Calculation
# ==============================================================================

def analyze_audio(signal_stereo, fs=48000, start_sec=0.2, end_sec=None):
    """Computes comprehensive audio metrics."""
    ch_l = np.array([s[0] for s in signal_stereo], dtype=np.float64)
    ch_r = np.array([s[1] for s in signal_stereo], dtype=np.float64)

    start_idx = int(start_sec * fs)
    end_idx = int(end_sec * fs) if end_sec is not None else len(ch_l)
    slice_l = ch_l[start_idx:end_idx]
    slice_r = ch_r[start_idx:end_idx]

    rms_l = float(np.sqrt(np.mean(slice_l**2))) if len(slice_l) > 0 else 0.0
    rms_r = float(np.sqrt(np.mean(slice_r**2))) if len(slice_r) > 0 else 0.0
    peak_l = float(np.max(np.abs(slice_l))) if len(slice_l) > 0 else 0.0
    peak_r = float(np.max(np.abs(slice_r))) if len(slice_r) > 0 else 0.0
    decorrelation = float(np.mean(np.abs(slice_l - slice_r))) if len(slice_l) > 0 else 0.0

    # FFT dominant peak
    if len(slice_l) >= 1024:
        fft_vals = np.abs(rfft(slice_l * np.hanning(len(slice_l))))
        fft_freqs = rfftfreq(len(slice_l), 1.0 / fs)
        # Look above 50 Hz to ignore DC
        valid_idx = np.where(fft_freqs >= 50.0)[0]
        if len(valid_idx) > 0:
            peak_idx = valid_idx[np.argmax(fft_vals[valid_idx])]
            dom_freq = float(fft_freqs[peak_idx])
        else:
            dom_freq = 0.0
    else:
        dom_freq = 0.0

    has_nan = bool(np.isnan(ch_l).any() or np.isnan(ch_r).any())
    has_inf = bool(np.isinf(ch_l).any() or np.isinf(ch_r).any())
    max_step = float(np.max(np.abs(np.diff(slice_l)))) if len(slice_l) > 1 else 0.0

    return {
        "rms_l": rms_l,
        "rms_r": rms_r,
        "peak_l": peak_l,
        "peak_r": peak_r,
        "decorrelation_lr": decorrelation,
        "dominant_freq_hz": dom_freq,
        "max_step_delta": max_step,
        "has_nan": has_nan,
        "has_inf": has_inf,
        "is_stable": not (has_nan or has_inf or peak_l > 2.0 or peak_r > 2.0)
    }


def compare_signals(sig1, sig2):
    """Computes cross-engine RMS error and correlation."""
    min_len = min(len(sig1), len(sig2))
    a = np.array([sig1[i][0] for i in range(min_len)], dtype=np.float64)
    b = np.array([sig2[i][0] for i in range(min_len)], dtype=np.float64)
    diff = a - b
    rms_err = float(np.sqrt(np.mean(diff**2)))
    return rms_err

# ==============================================================================
# 12 Validation Scenarios Execution
# ==============================================================================

def main():
    print("=" * 80)
    print("ACCIDENTAL WARP V1.2 — TRI-ENGINE MULTI-SCENARIO VALIDATION SUITE")
    print("Engines: [A] Original JS AudioWorklet  [B] Python Model  [C] C++ TapeCore")
    print("=" * 80)

    fs = 48000
    manifest = {
        "suite": "Accidental Warp V1.2 Sonic Fidelity Validation",
        "timestamp_utc": datetime.now(timezone.utc).isoformat(),
        "sample_rate_hz": fs,
        "engines": {
            "engine_a": "Original JS AudioWorklet (docs/simulators/accidental_warp_simulator.html)",
            "engine_b": "Python Reference Model (scripts/validate_warp_prototype.py)",
            "engine_c": "Native C++ TapeCore (build-native/hydra_listen_native.exe)"
        },
        "scenarios": []
    }

    # Helper stimuli
    def make_sine(freq=440.0, dur_sec=0.8, amp=0.5):
        n = int(fs * dur_sec)
        return [(amp * math.sin(TWO_PI * freq * i / fs), amp * math.sin(TWO_PI * freq * i / fs)) for i in range(n)]

    def make_impulse(dur_sec=2.0, amp=0.9):
        n = int(fs * dur_sec)
        return [(amp, amp)] + [(0.0, 0.0)] * (n - 1)

    def make_melody_burst(dur_sec=15.0):
        n = int(fs * dur_sec)
        buf = [(0.0, 0.0)] * n
        notes = [220.0, 277.18, 329.63, 440.0, 554.37]
        for idx, f in enumerate(notes):
            offset = int(idx * 0.25 * fs)
            for i in range(int(0.20 * fs)):
                if offset + i < n:
                    env = math.exp(-i / (0.08 * fs))
                    val = 0.4 * env * math.sin(TWO_PI * f * i / fs)
                    buf[offset + i] = (val, val)
        return buf

    # 1. Octave-Up Transposition (speed=1.0)
    print("\n[Scenario 1/12] Octave-Up Transposition (+12 st, speed=1.0, 440Hz -> 880Hz)")
    stim1 = make_sine(440.0, 0.8)
    p1 = {"warpSpeed": 1.0, "warpGrainMs": 80.0, "warpStereo": 0.0, "warpFeedback": 0.0, "warpMode": 0}
    out_a1 = run_engine_a_worklet(stim1, p1, fs)
    out_b1 = [ReferenceAccidentalWarpDSP(fs, 80.0, 1.0, 0.0, 0.0, 0).process_frame(l, r) for l, r in stim1]
    out_c1 = run_engine_c_native(stim1, p1, fs=fs)
    m_a1 = analyze_audio(out_a1, fs); m_b1 = analyze_audio(out_b1, fs); m_c1 = analyze_audio(out_c1, fs)
    rms_err_ac1 = compare_signals(out_a1, out_c1)
    print(f"  Dominant Frequency: A={m_a1['dominant_freq_hz']:.1f}Hz | B={m_b1['dominant_freq_hz']:.1f}Hz | C={m_c1['dominant_freq_hz']:.1f}Hz")
    print(f"  Cross-Engine RMS Error (A vs C): {rms_err_ac1:.5f}")
    write_wav(OUTPUT_DIR / "warp_octave_up.wav", out_c1, fs)
    manifest["scenarios"].append({
        "id": "scenario_01_octave_up", "name": "Octave-Up Transposition",
        "target_hz": 880.0, "measured_hz_native": m_c1['dominant_freq_hz'],
        "rms_error_worklet_vs_native": rms_err_ac1,
        "pass": abs(m_c1['dominant_freq_hz'] - 880.0) < 25.0 and m_c1['is_stable']
    })

    # 2. Perfect Fifth Transposition (speed=0.5)
    print("\n[Scenario 2/12] Perfect Fifth Transposition (+7 st, speed=0.5, 440Hz -> 660Hz)")
    stim2 = make_sine(440.0, 0.8)
    p2 = {"warpSpeed": 0.5, "warpGrainMs": 80.0, "warpStereo": 0.0, "warpFeedback": 0.0, "warpMode": 0}
    out_a2 = run_engine_a_worklet(stim2, p2, fs)
    out_b2 = [ReferenceAccidentalWarpDSP(fs, 80.0, 0.5, 0.0, 0.0, 0).process_frame(l, r) for l, r in stim2]
    out_c2 = run_engine_c_native(stim2, p2, fs=fs)
    m_c2 = analyze_audio(out_c2, fs)
    print(f"  Dominant Frequency C: {m_c2['dominant_freq_hz']:.1f}Hz (Target ~660Hz)")
    write_wav(OUTPUT_DIR / "warp_fifth_up.wav", out_c2, fs)
    manifest["scenarios"].append({
        "id": "scenario_02_fifth_up", "name": "Perfect Fifth Transposition",
        "target_hz": 660.0, "measured_hz_native": m_c2['dominant_freq_hz'],
        "pass": abs(m_c2['dominant_freq_hz'] - 660.0) < 25.0 and m_c2['is_stable']
    })

    # 3. Mode 0: 2-Tap Musical Hermite
    print("\n[Scenario 3/12] Mode 0 (2-Tap Musical Hermite)")
    stim3 = make_sine(330.0, 0.8)
    p3 = {"warpSpeed": 1.0, "warpGrainMs": 80.0, "warpStereo": 0.2, "warpFeedback": 0.0, "warpMode": 0}
    out_c3 = run_engine_c_native(stim3, p3, fs=fs)
    m_c3 = analyze_audio(out_c3, fs)
    write_wav(OUTPUT_DIR / "warp_mode0_musical.wav", out_c3, fs)
    manifest["scenarios"].append({"id": "scenario_03_mode0_hermite", "name": "Mode 0 2-Tap Hermite", "metrics": m_c3, "pass": m_c3['is_stable']})

    # 4. Mode 1: 1-Tap Solo Hermite
    print("\n[Scenario 4/12] Mode 1 (1-Tap Solo Hermite)")
    p4 = {"warpSpeed": 1.0, "warpGrainMs": 80.0, "warpStereo": 0.0, "warpFeedback": 0.0, "warpMode": 1}
    out_c4 = run_engine_c_native(stim3, p4, fs=fs)
    m_c4 = analyze_audio(out_c4, fs)
    write_wav(OUTPUT_DIR / "warp_mode1_solo_hermite.wav", out_c4, fs)
    manifest["scenarios"].append({"id": "scenario_04_mode1_solo_hermite", "name": "Mode 1 Solo Hermite", "metrics": m_c4, "pass": m_c4['is_stable']})

    # 5. Mode 2: 1-Tap Raw Int
    print("\n[Scenario 5/12] Mode 2 (1-Tap Raw Int - Experimental Crunch)")
    p5 = {"warpSpeed": 1.0, "warpGrainMs": 80.0, "warpStereo": 0.0, "warpFeedback": 0.0, "warpMode": 2}
    out_c5 = run_engine_c_native(stim3, p5, fs=fs)
    m_c5 = analyze_audio(out_c5, fs)
    write_wav(OUTPUT_DIR / "warp_mode2_raw_int.wav", out_c5, fs)
    manifest["scenarios"].append({"id": "scenario_05_mode2_raw_int", "name": "Mode 2 Raw Int", "metrics": m_c5, "pass": m_c5['is_stable']})

    # 6. Mode 3: 2-Tap Raw Int
    print("\n[Scenario 6/12] Mode 3 (2-Tap Raw Int - Crossfaded Crunch)")
    p6 = {"warpSpeed": 1.0, "warpGrainMs": 80.0, "warpStereo": 0.4, "warpFeedback": 0.0, "warpMode": 3}
    out_c6 = run_engine_c_native(stim3, p6, fs=fs)
    m_c6 = analyze_audio(out_c6, fs)
    write_wav(OUTPUT_DIR / "warp_mode3_raw_int_crossfade.wav", out_c6, fs)
    manifest["scenarios"].append({"id": "scenario_06_mode3_raw_int", "name": "Mode 3 Raw Int Crossfade", "metrics": m_c6, "pass": m_c6['is_stable']})

    # 7. Stereo Warp Variations (0.0, 0.5, 1.0)
    print("\n[Scenario 7/12] Stereo Phase Offset (0.0 vs 0.5 vs 1.0)")
    stim7 = make_sine(440.0, 0.6)
    out_s0 = run_engine_c_native(stim7, {"warpSpeed": 1.0, "warpGrainMs": 80.0, "warpStereo": 0.0, "warpMode": 0}, fs=fs)
    out_s5 = run_engine_c_native(stim7, {"warpSpeed": 1.0, "warpGrainMs": 80.0, "warpStereo": 0.5, "warpMode": 0}, fs=fs)
    out_s1 = run_engine_c_native(stim7, {"warpSpeed": 1.0, "warpGrainMs": 80.0, "warpStereo": 1.0, "warpMode": 0}, fs=fs)
    m_s0 = analyze_audio(out_s0, fs); m_s5 = analyze_audio(out_s5, fs); m_s1 = analyze_audio(out_s1, fs)
    print(f"  Stereo Decorrelation: s=0.0 -> {m_s0['decorrelation_lr']:.5f} | s=0.5 -> {m_s5['decorrelation_lr']:.5f} | s=1.0 -> {m_s1['decorrelation_lr']:.5f}")
    write_wav(OUTPUT_DIR / "warp_stereo_wide.wav", out_s1, fs)
    manifest["scenarios"].append({
        "id": "scenario_07_stereo_spread", "name": "Stereo Spread Range",
        "decorr_0_0": m_s0['decorrelation_lr'], "decorr_0_5": m_s5['decorrelation_lr'], "decorr_1_0": m_s1['decorrelation_lr'],
        "pass": (m_s0['decorrelation_lr'] < 0.005) and (m_s5['decorrelation_lr'] > 0.05) and (m_s1['decorrelation_lr'] > 0.05)
    })

    # 8. Feedback Range & Gain Stability (0%, 50%, 80%, 95%)
    print("\n[Scenario 8/12] Feedback Levels (0%, 50%, 80%, 95%) Stability & Tail")
    impulse = make_impulse(dur_sec=1.5, amp=0.8)
    fb_levels = [0.0, 50.0, 80.0, 95.0]
    fb_results = {}
    for fb in fb_levels:
        out_fb = run_engine_c_native(impulse, {"warpSpeed": 0.5, "warpGrainMs": 80.0, "warpFeedback": fb, "warpMode": 0}, fs=fs)
        m_fb = analyze_audio(out_fb, fs, start_sec=0.5)
        fb_results[fb] = m_fb
        print(f"  FB {fb:4.1f}% -> Tail RMS: {m_fb['rms_l']:.5f} | Stable: {m_fb['is_stable']}")
    write_wav(OUTPUT_DIR / "warp_high_feedback_95.wav", out_fb, fs)
    manifest["scenarios"].append({
        "id": "scenario_08_feedback_stability", "name": "Feedback Stability Range",
        "results": {f"{k}%": v['rms_l'] for k, v in fb_results.items()},
        "pass": all(v['is_stable'] for v in fb_results.values()) and (fb_results[80.0]['rms_l'] > fb_results[0.0]['rms_l'])
    })

    # 9. Dynamic Grain Window Automation
    print("\n[Scenario 9/12] Dynamic Grain Window Automation (40ms -> 240ms)")
    stim9 = make_sine(440.0, 1.2)
    events9 = [(int(0.3 * fs), 32, 120.0), (int(0.6 * fs), 32, 180.0), (int(0.9 * fs), 32, 240.0)]
    out_c9 = run_engine_c_native(stim9, {"warpSpeed": 1.0, "warpGrainMs": 40.0, "warpMode": 0}, automation_events=events9, fs=fs)
    m_c9 = analyze_audio(out_c9, fs)
    write_wav(OUTPUT_DIR / "warp_grain_automation.wav", out_c9, fs)
    manifest["scenarios"].append({"id": "scenario_09_grain_automation", "name": "Grain Automation", "metrics": m_c9, "pass": m_c9['is_stable'] and m_c9['max_step_delta'] < 1.0})

    # 10. Dynamic Speed Multiplier Automation
    print("\n[Scenario 10/12] Dynamic Speed Multiplier Automation (0.0 -> 0.5 -> 1.0 -> 1.5)")
    events10 = [(int(0.3 * fs), 31, 0.5), (int(0.6 * fs), 31, 1.0), (int(0.9 * fs), 31, 1.5)]
    out_c10 = run_engine_c_native(stim9, {"warpSpeed": 0.0, "warpGrainMs": 80.0, "warpMode": 0}, automation_events=events10, fs=fs)
    m_c10 = analyze_audio(out_c10, fs)
    write_wav(OUTPUT_DIR / "warp_speed_automation.wav", out_c10, fs)
    manifest["scenarios"].append({"id": "scenario_10_speed_automation", "name": "Speed Automation", "metrics": m_c10, "pass": m_c10['is_stable'] and m_c10['max_step_delta'] < 1.0})

    # 11. Live Audio Buffer Toggle
    print("\n[Scenario 11/12] Live Audio Buffer Toggle (Warp Active 1 -> 0 -> 1)")
    events11 = [(int(0.4 * fs), 30, 0.0), (int(0.8 * fs), 30, 1.0)]
    out_c11 = run_engine_c_native(stim9, {"warpActive": 1.0, "warpSpeed": 1.0, "warpGrainMs": 80.0, "warpMode": 0}, automation_events=events11, fs=fs)
    m_c11 = analyze_audio(out_c11, fs)
    write_wav(OUTPUT_DIR / "warp_toggle_transition.wav", out_c11, fs)
    manifest["scenarios"].append({"id": "scenario_11_live_toggle", "name": "Live Buffer Toggle", "metrics": m_c11, "pass": m_c11['is_stable'] and m_c11['max_step_delta'] < 1.0})

    # 12. Extended Feedback Cascade (15 seconds)
    print("\n[Scenario 12/12] Extended Upward Feedback Cascade (15.0 seconds, feedback=65%)")
    stim12 = make_melody_burst(dur_sec=15.0)
    p12 = {"warpSpeed": 1.0, "warpGrainMs": 90.0, "warpStereo": 0.4, "warpFeedback": 65.0, "warpMode": 0}
    out_c12 = run_engine_c_native(stim12, p12, fs=fs)
    m_c12 = analyze_audio(out_c12, fs, start_sec=2.0)
    print(f"  15s Cascade Peak: {m_c12['peak_l']:.4f} | Late Tail RMS (2s-15s): {m_c12['rms_l']:.5f} | Stable: {m_c12['is_stable']}")
    write_wav(OUTPUT_DIR / "warp_feedback_cascade_15s.wav", out_c12, fs)
    manifest["scenarios"].append({
        "id": "scenario_12_extended_cascade_15s", "name": "15s Feedback Cascade",
        "duration_sec": 15.0, "metrics": m_c12,
        "pass": m_c12['is_stable'] and (m_c12['peak_l'] < 1.8) and (m_c12['rms_l'] > 0.001)
    })

    # Save Authoritative Manifest
    all_passed = all(s.get("pass", False) for s in manifest["scenarios"])
    manifest["all_passed"] = all_passed
    manifest_path = OUTPUT_DIR / "manifest.json"
    with open(manifest_path, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)

    print("\n" + "=" * 80)
    print(f"VALIDATION FINISHED: {'ALL 12 SCENARIOS PASSED' if all_passed else 'SOME SCENARIOS FAILED'}")
    print(f"Manifest written to: {manifest_path}")
    print("=" * 80)

    if not all_passed:
        sys.exit(1)


if __name__ == "__main__":
    main()
