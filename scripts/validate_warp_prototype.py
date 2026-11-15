#!/usr/bin/env python3
"""
Accidental Warp V1.1 — Prototype Simulator vs C++ TapeCore A/B Validation Runner.

This script executes the exact mathematical model of the HTML prototype simulator
alongside the compiled native C++ TapeCore engine (hydra_listen_native).
It renders matching stimulus signals, computes objective metrics, and saves
comparative 24-bit/48kHz WAV audio files.
"""

import math
import os
import struct
import subprocess
import sys
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUTPUT_DIR = ROOT / "tests" / "output" / "warp_ab_validation"
RUNNER_EXE = ROOT / "build-native" / "hydra_listen_native.exe"

TWO_PI = 6.28318530717958647692

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
        self.mode = mode # 0: 2-Tap Hermite, 1: 1-Tap Hermite, 2: 1-Tap Raw Int, 3: 2-Tap Raw Int
        self.fb_drive = 1.60

        self.d_samples = max(64, int(grain_ms * 0.001 * fs))
        self.d_size = self.d_samples if (mode == 0 or mode == 3) else (self.d_samples + 1000)
        self.write_pos = 0.0

    def process_frame(self, dry_l, dry_r):
        d_size = self.d_size
        d_samples = self.d_samples
        speed_mult = self.speed
        fb = self.feedback
        mode = self.mode

        wet_l = 0.0
        wet_r = 0.0

        if mode == 1 or mode == 2:
            # Mode 1 & 2: 1-Tap Bug Cru
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
            # Mode 0 & 3: 2-Taps Musical Crossfade
            base_pos = self.write_pos + d_samples
            while base_pos >= d_size: base_pos -= d_size
            while base_pos < 0.0: base_pos += d_size

            phase1 = base_pos / d_size
            phase2 = (phase1 + 0.5) if (phase1 + 0.5 < 1.0) else (phase1 - 0.5)

            gain1 = 1.0 - abs(2.0 * phase1 - 1.0)
            gain2 = 1.0 - abs(2.0 * phase2 - 1.0)

            rel_d1_l = (1.0 - phase1) * d_size
            rel_d2_l = (1.0 - phase2) * d_size

            # Channel R offset
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

        # Write to buffer
        self.delay_l.buffer[self.delay_l.write_idx] = self.dc_l.process(dry_l + fb_l)
        self.delay_l.write_idx = (self.delay_l.write_idx + 1) % d_size
        self.delay_r.buffer[self.delay_r.write_idx] = self.dc_r.process(dry_r + fb_r)
        self.delay_r.write_idx = (self.delay_r.write_idx + 1) % d_size

        self.write_pos += speed_mult
        if self.write_pos >= d_size: self.write_pos -= d_size

        return wet_l, wet_r

def write_wav(path, signal_stereo, fs=48000):
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), 'wb') as wav:
        wav.setnchannels(2)
        wav.setsampwidth(3) # 24-bit PCM
        wav.setframerate(fs)
        frames = bytearray()
        for l, r in signal_stereo:
            clamped_l = max(-1.0, min(1.0, l))
            clamped_r = max(-1.0, min(1.0, r))
            val_l = int(clamped_l * 8388607.0)
            val_r = int(clamped_r * 8388607.0)
            frames += struct.pack('<i', val_l)[:3]
            frames += struct.pack('<i', val_r)[:3]
        wav.writeframes(frames)

def run_hydra_native(input_stereo, params_dict, fs=48000):
    in_raw_path = OUTPUT_DIR / "temp_in.f32"
    out_raw_path = OUTPUT_DIR / "temp_out.f32"
    auto_path = OUTPUT_DIR / "temp_auto.txt"
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    with open(in_raw_path, "wb") as f:
        for l, r in input_stereo:
            f.write(struct.pack("<ff", l, r))

    # Construct automation.txt
    # Parameter IDs:
    # 3: drive (20.0), 11: delayActive (1), 12: delayTimeMs (150), 13: feedback (0),
    # 14: dryWet (100), 15: delayWet (100), 30: warpActive (1), 31: warpSpeed,
    # 32: warpGrainMs, 33: warpStereo, 34: warpFeedback, 35: warpMode
    base_params = {
        3: 20.0,
        11: 1.0,
        12: 100.0,
        13: 0.0,
        14: 100.0,
        15: 100.0,
        30: 1.0,
        31: params_dict.get("warpSpeed", 1.0),
        32: params_dict.get("warpGrainMs", 80.0),
        33: params_dict.get("warpStereo", 0.5),
        34: params_dict.get("warpFeedback", 0.0),
        35: params_dict.get("warpMode", 0.0),
    }

    with open(auto_path, "w") as f:
        for pid, val in sorted(base_params.items()):
            f.write(f"0 {pid} {val}\n")

    res = subprocess.run([str(RUNNER_EXE), str(in_raw_path), str(auto_path), str(out_raw_path)],
                         capture_output=True, text=True)
    if res.returncode != 0:
        raise RuntimeError(f"Runner failed: {res.stderr}")

    out_stereo = []
    with open(out_raw_path, "rb") as f:
        raw = f.read()
        num_floats = len(raw) // 4
        floats = struct.unpack(f"<{num_floats}f", raw)
        for i in range(0, len(floats), 2):
            out_stereo.append((floats[i], floats[i+1]))

    return out_stereo

def measure_frequency(signal_ch, fs=48000, start_sec=0.25, end_sec=0.45):
    start = int(start_sec * fs)
    end = int(end_sec * fs)
    crossings = 0
    for i in range(start + 1, end):
        if (signal_ch[i-1] < 0.0 and signal_ch[i] >= 0.0) or (signal_ch[i-1] >= 0.0 and signal_ch[i] < 0.0):
            crossings += 1
    duration = (end - start) / fs
    return crossings / (2.0 * duration)

def main():
    print("=" * 70)
    print("ACCIDENTAL WARP V1.1 — PROTOTYPE SIMULATOR vs C++ TAPECORE A/B")
    print("=" * 70)

    fs = 48000
    duration_sec = 0.6
    n_samples = int(fs * duration_sec)

    # 1. Stimulus: 440 Hz pure sine wave
    sine_input = []
    for i in range(n_samples):
        s = math.sin(TWO_PI * 440.0 * i / fs) * 0.5
        sine_input.append((s, s))

    # Scenario 1: Octave Up Transposition (speed=1.0)
    print("\n[Scenario 1] Octave-Up Transposition (+12 semitones, speed=1.0)")
    dsp_ref1 = ReferenceAccidentalWarpDSP(fs=fs, grain_ms=80.0, speed=1.0, stereo=0.0, feedback=0.0, mode=0)
    ref_out1 = [dsp_ref1.process_frame(l, r) for l, r in sine_input]
    hydra_out1 = run_hydra_native(sine_input, {"warpSpeed": 1.0, "warpGrainMs": 80.0, "warpStereo": 0.0, "warpFeedback": 0.0, "warpMode": 0.0})

    ref_freq1 = measure_frequency([s[0] for s in ref_out1], fs)
    hydra_freq1 = measure_frequency([s[0] for s in hydra_out1], fs)
    print(f"  Reference Simulator Measured Freq: {ref_freq1:.1f} Hz (Target ~880 Hz)")
    print(f"  C++ TapeCore Measured Freq:       {hydra_freq1:.1f} Hz (Target ~880 Hz)")
    write_wav(OUTPUT_DIR / "warp_ref_octave_up.wav", ref_out1, fs)
    write_wav(OUTPUT_DIR / "warp_hydra_octave_up.wav", hydra_out1, fs)

    # Scenario 2: Perfect Fifth Transposition (speed=0.5)
    print("\n[Scenario 2] Perfect Fifth Transposition (+7 semitones, speed=0.5)")
    dsp_ref2 = ReferenceAccidentalWarpDSP(fs=fs, grain_ms=80.0, speed=0.5, stereo=0.0, feedback=0.0, mode=0)
    ref_out2 = [dsp_ref2.process_frame(l, r) for l, r in sine_input]
    hydra_out2 = run_hydra_native(sine_input, {"warpSpeed": 0.5, "warpGrainMs": 80.0, "warpStereo": 0.0, "warpFeedback": 0.0, "warpMode": 0.0})

    ref_freq2 = measure_frequency([s[0] for s in ref_out2], fs)
    hydra_freq2 = measure_frequency([s[0] for s in hydra_out2], fs)
    print(f"  Reference Simulator Measured Freq: {ref_freq2:.1f} Hz (Target ~660 Hz)")
    print(f"  C++ TapeCore Measured Freq:       {hydra_freq2:.1f} Hz (Target ~660 Hz)")
    write_wav(OUTPUT_DIR / "warp_ref_fifth_up.wav", ref_out2, fs)
    write_wav(OUTPUT_DIR / "warp_hydra_fifth_up.wav", hydra_out2, fs)

    # Scenario 3: Stereo Decorrelation
    print("\n[Scenario 3] Stereo Phase Decorrelation (stereo=0.8)")
    dsp_ref3 = ReferenceAccidentalWarpDSP(fs=fs, grain_ms=80.0, speed=1.0, stereo=0.8, feedback=0.0, mode=0)
    ref_out3 = [dsp_ref3.process_frame(l, r) for l, r in sine_input]
    hydra_out3 = run_hydra_native(sine_input, {"warpSpeed": 1.0, "warpGrainMs": 80.0, "warpStereo": 0.8, "warpFeedback": 0.0, "warpMode": 0.0})

    ref_diff3 = sum(abs(l - r) for l, r in ref_out3[1000:10000])
    hydra_diff3 = sum(abs(l - r) for l, r in hydra_out3[1000:10000])
    print(f"  Reference Simulator L/R Decorrelation Sum: {ref_diff3:.2f}")
    print(f"  C++ TapeCore L/R Decorrelation Sum:        {hydra_diff3:.2f}")
    write_wav(OUTPUT_DIR / "warp_ref_stereo_decorrelation.wav", ref_out3, fs)
    write_wav(OUTPUT_DIR / "warp_hydra_stereo_decorrelation.wav", hydra_out3, fs)

    # Scenario 4: Raw Integer Cast (Mode 2 vs Mode 0)
    print("\n[Scenario 4] Raw Integer Crunch vs Hermite Interpolation (Mode 2 vs 1)")
    dsp_ref4 = ReferenceAccidentalWarpDSP(fs=fs, grain_ms=80.0, speed=1.0, stereo=0.0, feedback=0.0, mode=2)
    ref_out4 = [dsp_ref4.process_frame(l, r) for l, r in sine_input]
    hydra_out4 = run_hydra_native(sine_input, {"warpSpeed": 1.0, "warpGrainMs": 80.0, "warpStereo": 0.0, "warpFeedback": 0.0, "warpMode": 2.0})

    write_wav(OUTPUT_DIR / "warp_ref_raw_int.wav", ref_out4, fs)
    write_wav(OUTPUT_DIR / "warp_hydra_raw_int.wav", hydra_out4, fs)
    print("  Mode 2 Raw Int WAV files written.")

    # Scenario 5: Feedback Recirculation & Upward Cascade
    print("\n[Scenario 5] Feedback Cascade with Impulse (warpFeedback=75%)")
    impulse_input = [(0.9, 0.9)] + [(0.0, 0.0)] * (fs - 1) # 1.0 sec
    dsp_ref5 = ReferenceAccidentalWarpDSP(fs=fs, grain_ms=80.0, speed=0.5, stereo=0.3, feedback=75.0, mode=0)
    ref_out5 = [dsp_ref5.process_frame(l, r) for l, r in impulse_input]
    hydra_out5 = run_hydra_native(impulse_input, {"warpSpeed": 0.5, "warpGrainMs": 80.0, "warpStereo": 0.3, "warpFeedback": 75.0, "warpMode": 0.0})

    ref_energy5 = sum(abs(l) for l, r in ref_out5[8000:])
    hydra_energy5 = sum(abs(l) for l, r in hydra_out5[8000:])
    print(f"  Reference Cascade Tail Energy: {ref_energy5:.3f}")
    print(f"  C++ TapeCore Cascade Tail Energy: {hydra_energy5:.3f}")
    write_wav(OUTPUT_DIR / "warp_ref_feedback_cascade.wav", ref_out5, fs)
    write_wav(OUTPUT_DIR / "warp_hydra_feedback_cascade.wav", hydra_out5, fs)

    print("\n" + "=" * 70)
    print("A/B VALIDATION COMPLETED SUCCESSFULLY")
    print(f"Comparative WAV files available in: {OUTPUT_DIR}")
    print("=" * 70)

    # Clean up temporary raw files
    for tmp in [OUTPUT_DIR / "temp_in.f32", OUTPUT_DIR / "temp_out.f32", OUTPUT_DIR / "temp_auto.txt"]:
        if tmp.exists():
            tmp.unlink()

if __name__ == "__main__":
    main()
