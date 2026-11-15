#!/usr/bin/env python3
"""Render synchronized Accidental Warp stimuli and report independent validation results."""

import argparse
import hashlib
import itertools
import json
import math
import os
import shutil
import subprocess
import sys
import tempfile
import wave
from array import array
from datetime import datetime, timezone
from pathlib import Path

dependency_error = None
try:
    import numpy as np
    from scipy.signal import correlate, correlation_lags
except ImportError as error:
    dependency_error = str(error)

ROOT = Path(__file__).resolve().parents[1]
HTML = ROOT / "docs/simulators/accidental_warp_simulator.html"
REFERENCE_SHA256 = "0e448e03bce714f92c72941208252acf7aa403d1bf3d4aa7d15a0a3aa0c829c9"
NODE_RUNNER = ROOT / "scripts/run_simulator_worklet.js"
FS = 48000  # Fixed by hydra_listen.
BLOCK = 128
WARP_IDS = {"active": 30, "speed": 31, "grainMs": 32, "stereo": 33, "feedback": 34, "mode": 35}
WARP_DEFAULTS = dict(active=1, speed=1.0, grainMs=80.0, stereo=0.0, feedback=0.0, mode=0)
# Disable stochastic tape effects when measuring stereo spread and impulse decay.
# The record/playback chain and its magnetic coloration remain enabled.
NATIVE_SETTINGS = {0: 0, 1: 0, 2: 0, 3: 20, 4: 0, 8: 0, 11: 1, 12: 100, 13: 0, 14: 100, 15: 100}

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
        self.buffer = array("f", [0.0]) * size
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

        self.d_samples = max(64, int(fs * (grain_ms * 0.001)))
        self.d_size = self.d_samples if (mode == 0 or mode == 3) else (self.d_samples + 1000)
        self.write_pos = 0.0

    def set_grain_ms(self, grain_ms):
        self.grain_ms = grain_ms
        self.d_samples = max(64, int(self.fs * (grain_ms * 0.001)))
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

            proc_l = self.fb_lpf_l.process(wet_l)
            proc_r = self.fb_lpf_r.process(wet_r)
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


def run_python(stimulus, params, events):
    model = ReferenceAccidentalWarpDSP(
        FS, params["grainMs"], params["speed"], params["stereo"], params["feedback"], params["mode"]
    )
    changes = {event["frame"]: event for event in events}
    output = np.empty_like(stimulus)
    for frame, (left, right) in enumerate(stimulus):
        if frame in changes:
            change = changes[frame]
            if change["parameter"] == "grainMs":
                model.set_grain_ms(change["value"])
            elif change["parameter"] == "speed":
                model.speed = change["value"]
            else:
                raise ValueError(f"Unsupported Python automation: {change}")
        output[frame] = model.process_frame(float(left), float(right))
    return output


def run_external(engine, executable, input_path, params, events, scratch, shape):
    output_path = scratch / "output.f32"
    output_path.unlink(missing_ok=True)
    if engine == "worklet":
        settings = scratch / "params.json"
        settings.write_text(json.dumps(dict(params, sampleRate=FS, events=events)), encoding="utf-8")
        command = [executable, NODE_RUNNER, input_path, settings, output_path]
    else:
        settings = scratch / "automation.txt"
        initial = NATIVE_SETTINGS | {WARP_IDS[key]: value for key, value in params.items()}
        rows = [(0, key, value) for key, value in initial.items()]
        rows += [(e["frame"], WARP_IDS[e["parameter"]], e["value"]) for e in events]
        settings.write_text("".join(f"{frame} {key} {value}\n" for frame, key, value in rows))
        command = [executable, input_path, settings, output_path]
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=120)
    if result.returncode:
        raise RuntimeError(f"Exit {result.returncode}: {result.stderr.strip()} {result.stdout.strip()}")
    return np.fromfile(output_path, dtype="<f4").reshape(shape)


def write_wav(path, audio):
    packed = (np.clip(audio, -1.0, 1.0) * 8388607).astype("<i4")
    with wave.open(str(path), "wb") as wav:
        wav.setparams((2, 3, FS, 0, "NONE", "not compressed"))
        wav.writeframes(packed.view(np.uint8).reshape(-1, 4)[:, :3].tobytes())


def rms(audio, axis=None):
    return np.sqrt(np.mean(np.square(audio, dtype=np.float64), axis=axis))


def dominant_hz(audio):
    spectrum = np.abs(np.fft.rfft(audio * np.hanning(len(audio))))
    freqs = np.fft.rfftfreq(len(audio), 1 / FS)
    lo = np.searchsorted(freqs, 50)
    return float(freqs[lo + np.argmax(spectrum[lo:])])


def window_energy(audio):
    frames = FS // 10
    return [rms(audio[start:start + frames], axis=0).tolist() for start in range(0, len(audio), frames)]


def analyze(audio, target, tail_start, events):
    # 6 dB of numerical headroom above normalized PCM is a runaway guard.
    # For stationary tones allow one grain modulation sideband plus one FFT bin.
    peak_limit = 2.0
    metrics = {
        "nan_count": int(np.isnan(audio).sum()),
        "inf_count": int(np.isinf(audio).sum()),
    }
    checks = {"finite": not (metrics["nan_count"] or metrics["inf_count"])}
    criteria = {"finite": "Zero NaN/Inf", "peak": "Non-silent, at most 2.0 FS over the full render"}
    if checks["finite"]:
        peak = float(np.max(np.abs(audio)))
        steady = audio[FS // 5:]
        metrics.update(
            peak=peak,
            rms_by_channel=rms(audio, axis=0).tolist(),
            peak_step_by_channel=np.max(np.abs(np.diff(audio, axis=0)), axis=0).tolist(),
            lr_rms=float(rms(steady[:, 0] - steady[:, 1])),
            dominant_hz=dominant_hz(steady[:, 0]),
        )
        checks["peak"] = 0 < peak <= peak_limit
        if target:
            target_hz, grain_ms = target
            tolerance = 1000 / grain_ms + FS / len(steady)
            criteria["pitch"] = {"target_hz": target_hz, "max_error_hz": tolerance,
                                 "reason": "One grain-rate sideband plus one FFT bin"}
            metrics["pitch_ratio"] = metrics["dominant_hz"] / 440
            checks["pitch"] = abs(metrics["dominant_hz"] - target_hz) <= tolerance
        if tail_start:
            start = round(tail_start * FS)
            early = rms(audio[:start])
            tail = rms(audio[start:])
            metrics.update(tail_rms=float(tail), tail_to_early_rms=float(tail / early) if early else None)
            criteria["tail_decay"] = "RMS after the settling interval is below the early RMS"
            checks["tail_decay"] = bool(tail < early)
    if checks["finite"] and events and events[0]["parameter"] in ("speed", "active"):
        frames = [0] + [e["frame"] for e in events] + [len(audio)]
        targets = ([440] + [440 * (1 + e["value"]) for e in events]
                   if events[0]["parameter"] == "speed" else [880, 440, 880])
        # Discard 1.5 grains after every event for startup and native settling.
        sections = [audio[start + round(.12 * FS):end, 0] for start, end in zip(frames, frames[1:])]
        freqs = [dominant_hz(section) for section in sections]
        errors = [1000 / WARP_DEFAULTS["grainMs"] + FS / len(section) for section in sections]
        metrics["automation_pitch_hz"] = freqs
        criteria["automation_pitch"] = {"targets_hz": targets, "max_error_hz": errors,
                                        "reason": "One grain-rate sideband plus one FFT bin per segment"}
        checks["automation_pitch"] = all(abs(f - target) <= error for f, target, error in zip(freqs, targets, errors))
    return {"status": "PASS" if all(checks.values()) else "FAIL",
            "checks": checks, "criteria": criteria, "metrics": metrics}


def compare(a, b, reference_pair):
    a, b = a.astype(np.float64), b.astype(np.float64)
    error = a - b
    # Estimate relative lag from the waveform and report the raw zero-lag errors.
    # Bounded search avoids matching a different repetition in a long feedback tail.
    def lag(x, y):
        lags = correlation_lags(len(x), len(y))
        valid = np.abs(lags) <= FS // 4
        return int(lags[valid][np.argmax(correlate(y, x, method="fft")[valid])])

    window = np.hanning(len(a))[:, None]
    spectrum_a = np.abs(np.fft.rfft(a * window, axis=0))
    spectrum_b = np.abs(np.fft.rfft(b * window, axis=0))
    metrics = {
        "rms_error_by_channel": rms(error, axis=0).tolist(),
        "max_abs_error_by_channel": np.max(np.abs(error), axis=0).tolist(),
        "best_lag_frames_by_channel": [lag(a[:, ch], b[:, ch]) for ch in range(2)],
        "zero_lag_correlation_by_channel": [
            float(np.corrcoef(x, y)[0, 1]) if np.std(x) and np.std(y) else None for x, y in zip(a.T, b.T)
        ],
        "relative_magnitude_spectrum_l2_by_channel": (
            np.linalg.norm(spectrum_a - spectrum_b, axis=0) /
            np.maximum(np.linalg.norm(spectrum_a, axis=0), np.finfo(float).tiny)
        ).tolist(),
        "rms_windows_100ms": {"first": window_energy(a), "second": window_energy(b)},
        "lr_difference_rms_error": float(rms(error[:, 0] - error[:, 1])),
    }
    if reference_pair:
        # Float32 PCM should agree to near its rounding precision for two ports of
        # the same signal path. Allow small accumulated biquad/libm rounding error.
        criteria = {"rms_error_max": 1e-6, "max_abs_error_max": 1e-5, "lag_frames": 0}
        passed = (max(metrics["rms_error_by_channel"]) <= criteria["rms_error_max"] and
                  max(metrics["max_abs_error_by_channel"]) <= criteria["max_abs_error_max"] and
                  metrics["best_lag_frames_by_channel"] == [0, 0])
        return {"status": "PASS" if passed else "FAIL", "criteria": criteria, "metrics": metrics}
    return {
        "status": "NOT COMPARABLE", "metrics": metrics,
        "criteria": "Descriptive full-chain A/B; waveform equivalence requires matched signal paths",
        "reason": "TapeCore uses magnetic record/playback coloration and a separate phase/read implementation, "
                  "including grain smoothing and different single-tap timing. These measurements include all stages.",
    }


def scenarios():
    def sine(hz, duration):
        return 0.5 * np.sin(TWO_PI * hz * np.arange(round(duration * FS)) / FS)

    impulse = np.zeros(round(1.5 * FS))
    impulse[0] = 0.8
    melody = np.zeros(15 * FS)
    note_frames = np.arange(FS // 5)
    for index, hz in enumerate((220, 277.18, 329.63, 440, 554.37)):
        start = index * FS // 4
        melody[start:start + len(note_frames)] = (
            0.4 * np.exp(-note_frames / (0.08 * FS)) * np.sin(TWO_PI * hz * note_frames / FS)
        )
    controls = sine(440, 1.2)
    tone = sine(330, 0.8)
    return [
        ("01_octave_up", sine(440, 0.8), [{}], [], (880, 80), None),
        ("02_fifth_up", sine(440, 0.8), [dict(speed=0.5)], [], (660, 80), None),
        ("03_dual_tap_hermite", tone, [dict(mode=0, stereo=0.2)], [], None, None),
        ("04_single_tap_hermite", tone, [dict(mode=1)], [], None, None),
        ("05_single_tap_raw", tone, [dict(mode=2)], [], None, None),
        ("06_dual_tap_raw", tone, [dict(mode=3, stereo=0.4)], [], None, None),
        ("07_stereo", tone, [dict(stereo=s) for s in (0, 0.5, 1)], [], None, None),
        ("08_feedback", impulse, [dict(speed=0.5, feedback=f) for f in (0, 50, 80, 95)], [], None, 0.5),
        ("09_grain", controls, [dict(grainMs=40)], [(.3, "grainMs", 120), (.6, "grainMs", 180),
                                                    (.9, "grainMs", 240)], None, None),
        ("10_speed", controls, [dict(speed=0)], [(.3, "speed", .5), (.6, "speed", 1),
                                                  (.9, "speed", 1.5)], None, None),
        ("11_toggle", controls, [{}], [(.4, "active", 0), (.8, "active", 1)], None, None),
        ("12_cascade", melody, [dict(grainMs=90, stereo=.4, feedback=65)], [], None, 2),
    ]


def render_scenario(scenario, args, provenance, scratch):
    name, mono, variants, changes, pitch, tail_start = scenario
    folder = args.output_dir / name
    folder.mkdir(exist_ok=True)
    stimulus = np.repeat(mono[:, None], 2, axis=1).astype("<f4")
    input_path = folder / "input.f32"
    stimulus.tofile(input_path)
    write_wav(folder / "input.wav", stimulus)
    events = [
        {"requested_seconds": time, "frame": round(time * FS) // BLOCK * BLOCK,
         "parameter": key, "value": value}
        for time, key, value in changes
    ]
    record = dict(provenance, id=name, input_pcm=f"{name}/input.f32", input_wav=f"{name}/input.wav",
                  sample_rate_hz=FS, frames=len(stimulus), duration_sec=len(stimulus) / FS,
                  event_quantum_frames=BLOCK, automation=events, variants=[],
                  perceptual={"status": "NOT RUN", "criteria": [
                      "Level-matched A/B", "Rhythmic artifacts", "Granular texture",
                      "Brightness", "Saturation", "Repeat decay", "Cascade musicality"
                  ]})
    for index, overrides in enumerate(variants):
        params = WARP_DEFAULTS | overrides
        variant = {"warp_parameters": params, "tape_core_initial_parameters_by_id": NATIVE_SETTINGS,
                   "engines": {}, "similarity": {}}
        record["variants"].append(variant)
        signals = {}
        for engine in ("worklet", "python", "native"):
            run = dict(provenance["engine_versions"][engine])
            variant["engines"][engine] = run
            if name == "11_toggle" and engine != "native":
                run.update(status="NOT COMPARABLE", reason="The prototype has no equivalent Warp enable/disable transition.")
                continue
            try:
                audio = (run_python(stimulus, params, events) if engine == "python" else
                         run_external(engine, args.node if engine == "worklet" else args.listen,
                                      input_path, params, events, scratch, stimulus.shape))
                run["dsp"] = analyze(audio, pitch, tail_start, events)
                if name == "09_grain" and run["dsp"]["status"] == "PASS":
                    baseline = (run_python(stimulus, params, []) if engine == "python" else
                                run_external(engine, args.node if engine == "worklet" else args.listen,
                                             input_path, params, [], scratch, stimulus.shape))
                    delta = audio.astype(np.float64) - baseline
                    edge = events[0]["frame"]
                    before, after = float(rms(delta[:edge])), float(rms(delta[edge:]))
                    run["dsp"]["metrics"].update(grain_delta_before_first_event=before,
                                                 grain_delta_after_first_event=after)
                    run["dsp"]["criteria"]["grain_response"] = (
                        "Identical to the static control before the first event; difference above -60 dBFS afterward"
                    )
                    passed = before == 0 and after > 1e-3
                    run["dsp"]["checks"]["grain_response"] = passed
                    if not passed:
                        run["dsp"]["status"] = "FAIL"
                run["status"] = run["dsp"]["status"]
                if np.isfinite(audio).all():
                    signals[engine] = audio
                    wav = folder / f"{index + 1:02d}_{engine}.wav"
                    write_wav(wav, audio)
                    run["output_wav"] = wav.relative_to(args.output_dir).as_posix()
            except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired) as error:
                run.update(status="FAIL", reason=str(error))
        for first, second in itertools.combinations(variant["engines"], 2):
            pair = f"{first}_vs_{second}"
            variant["similarity"][pair] = (
                compare(signals[first], signals[second], second == "python")
                if first in signals and second in signals else
                {"status": "NOT COMPARABLE", "reason": "At least one render is unavailable or non-finite."}
            )
    record["execution_scope"] = "NOT COMPARABLE" if name == "11_toggle" else "three engines"
    if name in ("07_stereo", "08_feedback"):
        record["cross_variant_dsp"] = {}
        for engine in ("worklet", "python", "native"):
            runs = [v["engines"][engine] for v in record["variants"]]
            if any("dsp" not in r or "rms_by_channel" not in r["dsp"]["metrics"] for r in runs):
                continue
            metrics = [r["dsp"]["metrics"] for r in runs]
            if name == "07_stereo":
                values = [m["lr_rms"] for m in metrics]
                passed = values[0] < 1e-5 and min(values[1:]) > 1e-3
                criterion = "Mono baseline within float32 tolerance; stereo difference over -60 dBFS"
            else:
                values = [m["tail_rms"] for m in metrics]
                passed = values[0] < min(values[1:])
                criterion = "Every feedback setting sustains more tail energy than the zero-feedback impulse"
            record["cross_variant_dsp"][engine] = {
                "status": "PASS" if passed else "FAIL", "criteria": criterion, "values": values
            }
    return record


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--listen", type=Path, default=ROOT / "build-native" /
                        ("hydra_listen_native.exe" if os.name == "nt" else "hydra_listen_native"))
    parser.add_argument("--node", default="node")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "artifacts/warp-ab")
    args = parser.parse_args()
    args.output_dir = args.output_dir.resolve()
    args.listen = args.listen.resolve()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    manifest_path = args.output_dir / "manifest.json"
    manifest = {"suite": "Accidental Warp V1.3", "utc": datetime.now(timezone.utc).isoformat(),
                "complete": False, "scenarios": []}

    def save():
        manifest_path.write_text(json.dumps(manifest, indent=2, allow_nan=False) + "\n", encoding="utf-8")

    save()
    try:
        if dependency_error:
            raise ValueError(f"Install validation dependencies with python -m pip install numpy scipy: {dependency_error}")
        normalized = hashlib.sha256(HTML.read_bytes().replace(b"\r\n", b"\n")).hexdigest()
        if normalized != REFERENCE_SHA256:
            raise ValueError(f"Simulator HTML SHA-256 mismatch (LF canonical): {normalized}")
        if not args.listen.is_file():
            raise ValueError(f"Native runner missing at {args.listen}; build hydra_listen_native or pass --listen.")
        node = shutil.which(args.node)
        if node is None:
            raise ValueError(f"Node.js missing: {args.node}; install Node.js or pass --node.")
        args.node = node

        def command_output(command):
            return subprocess.check_output(command, cwd=ROOT, text=True).strip()

        provenance = {
            "git_commit": command_output(["git", "rev-parse", "HEAD"]),
            "tracked_worktree_dirty": bool(command_output(["git", "status", "--porcelain", "--untracked-files=no"])),
            "reference_sha256_lf": normalized, "reference_sha256_file": digest(HTML),
            "worklet_runner_sha256": digest(NODE_RUNNER),
            "engine_versions": {
                "worklet": {"version": normalized, "node_version": command_output([node, "--version"])},
                "python": {"version": digest(Path(__file__)), "python_version": sys.version.split()[0]},
                "native": {"version": command_output([args.listen, "--version"]), "binary_sha256": digest(args.listen)},
            },
        }
        with tempfile.TemporaryDirectory(prefix="hydra-warp-") as scratch:
            for scenario in scenarios():
                record = render_scenario(scenario, args, provenance, Path(scratch))
                manifest["scenarios"].append(record)
                print(f"Rendered {record['id']}", flush=True)
                save()
        manifest["complete"] = True
        save()
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        manifest["status"] = "NOT RUN" if not manifest["scenarios"] else "FAIL"
        manifest["reason"] = str(error)
        save()
        parser.exit(2, f"{error}\n")

    print(manifest_path)
    statuses = [
        result["status"]
        for scenario in manifest["scenarios"]
        for variant in scenario["variants"]
        for group in ("engines", "similarity")
        for result in variant[group].values()
    ] + [
        result["status"] for scenario in manifest["scenarios"]
        for result in scenario.get("cross_variant_dsp", {}).values()
    ]
    return int("FAIL" in statuses)


if __name__ == "__main__":
    sys.exit(main())
