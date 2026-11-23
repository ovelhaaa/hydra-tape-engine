"""Hydra Tape Engine 2.0 — Phase 6 Musical Validation & Listening Lab Review Package.

Renders 20 musical scenarios across 4 groups:
  Group A — Tape Echo (1-4)
  Group B — Reverse V2 (5-9)
  Group C — Clock & Transport (10-15)
  Group D — Performance Readiness (16-20)

Produces for each scenario:
  - Original input WAV
  - Processed output WAV
  - Block-by-block telemetry CSV
  - Full acoustic, temporal, and stability analysis metrics
  - Musical markers and DSP configuration
  - SHA-256 checksums and Listening Lab manifest.json
"""

import array
import argparse
import platform
import subprocess
import ctypes
from ctypes import (
    c_float, c_uint32, c_int, c_void_p, c_uint64, c_double, c_int64,
    c_uint8, c_char_p, POINTER, Structure, byref
)
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import sys
import wave
import numpy as np

import musical_sources
import audio_analysis

RATE = 48000
BLOCK = 128

class HydraDspTransportState(Structure):
    _fields_ = [
        ("bpm", c_float),
        ("beat_phase", c_float),
        ("bar_phase", c_float),
        ("musical_position", c_double),
        ("absolute_sample_position", c_uint64),
        ("transport_sample_position", c_uint64),
        ("beat_index", c_int64),
        ("bar_index", c_int64),
        ("time_signature_numerator", c_uint32),
        ("time_signature_denominator", c_uint32),
        ("playing", c_int),
        ("generation", c_uint32),
    ]

class HydraDspTapTempoState(Structure):
    _fields_ = [
        ("estimated_bpm", c_float),
        ("applied_bpm", c_float),
        ("tap_count", c_uint32),
        ("confidence", c_float),
        ("time_since_last_tap_s", c_float),
        ("is_timed_out", c_int),
        ("status", c_int),
        ("last_rejection", c_int),
    ]

class HydraDspMidiClockState(Structure):
    _fields_ = [
        ("tempo_source", c_uint32),
        ("sync_status", c_int),
        ("estimated_bpm", c_float),
        ("applied_bpm", c_float),
        ("phase_error_beats", c_float),
        ("estimated_jitter_ms", c_float),
        ("pulse_count", c_uint64),
        ("last_pulse_frame", c_uint64),
        ("is_transport_running", c_int),
        ("missed_pulses", c_uint32),
    ]

class HydraDspParamSpec(Structure):
    _fields_ = [
        ("id", c_int),
        ("name", c_char_p),
        ("min_value", c_float),
        ("max_value", c_float),
        ("default_value", c_float),
        ("smoothing", c_int),
        ("flags", c_uint32),
    ]


MIN_COMPATIBLE_API_VERSION = (1, 7, 0)


def decode_api_version(version: int) -> tuple[int, int, int]:
    return ((version >> 16) & 0xFF, (version >> 8) & 0xFF, version & 0xFF)


# Parameter IDs
PARAM_FLUTTER_DEPTH = 0
PARAM_WOW_DEPTH = 1
PARAM_DROPOUT = 2
PARAM_DRIVE = 3
PARAM_NOISE = 4
PARAM_TAPE_SPEED = 5
PARAM_TAPE_AGE = 6
PARAM_HEAD_BUMP = 7
PARAM_AZIMUTH = 8
PARAM_FLUTTER_RATE = 9
PARAM_WOW_RATE = 10
PARAM_DELAY_ACTIVE = 11
PARAM_DELAY_MS = 12
PARAM_FEEDBACK = 13
PARAM_DRY_WET = 14
PARAM_DELAY_WET = 15
PARAM_ACTIVE_HEADS = 16
PARAM_BPM = 17
PARAM_HEADS_MUSICAL = 18
PARAM_GUITAR_FOCUS = 19
PARAM_TONE = 20
PARAM_PING_PONG = 21
PARAM_FREEZE = 22
PARAM_REVERSE = 23
PARAM_REVERSE_SMEAR = 24
PARAM_SPRING = 25
PARAM_SPRING_DECAY = 26
PARAM_SPRING_DAMPING = 27
PARAM_SPRING_MIX = 28
PARAM_WARP_ACTIVE = 30
PARAM_WARP_SPEED = 31
PARAM_WARP_GRAIN_MS = 32
PARAM_WARP_STEREO = 33
PARAM_WARP_FEEDBACK = 34
PARAM_WARP_MODE = 35
PARAM_REVERSE_MODE = 36
PARAM_REVERSE_DIVISION = 37

# Reverse modes & divisions
REV_MODE_RHYTHMIC = 0
REV_MODE_FREE = 1
REV_MODE_LEGACY = 2

REV_DIV_QUARTER = 0
REV_DIV_WHOLE = 1
REV_DIV_HALF = 2
REV_DIV_EIGHTH = 3
REV_DIV_SIXTEENTH = 4
REV_DIV_DOTTED_QUARTER = 5
REV_DIV_DOTTED_EIGHTH = 6
REV_DIV_TRIPLET_QUARTER = 7
REV_DIV_TRIPLET_EIGHTH = 8

# Tempo Sources
TEMPO_SOURCE_INTERNAL = 0
TEMPO_SOURCE_TAP = 1
TEMPO_SOURCE_MIDI = 2

def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest()

def write_wav_24bit(path: Path, data: np.ndarray, sample_rate: int = RATE):
    # data: (frames, 2) float32
    clipped = np.clip(data, -1.0, 1.0)
    int_data = np.round(clipped * 8388607.0).astype(np.int32)
    # Convert to 24-bit PCM bytes
    pcm = bytearray()
    for f in range(len(int_data)):
        for ch in range(2):
            val = int(int_data[f, ch])
            pcm.extend(val.to_bytes(3, "little", signed=True))
    with wave.open(str(path), "wb") as w:
        w.setparams((2, 3, sample_rate, 0, "NONE", "not compressed"))
        w.writeframes(pcm)

class HydraEngineRunner:
    def __init__(self, lib_path: Path):
        lib_path = Path(lib_path).resolve()
        self.dll_directories = []
        if sys.platform == "win32":
            gcc_path = shutil.which("gcc")
            for directory in [lib_path.parent] + ([Path(gcc_path).resolve().parent] if gcc_path else []):
                self.dll_directories.append(os.add_dll_directory(str(directory)))
        self.lib = ctypes.CDLL(str(lib_path))
        self.library_sha256 = sha256_file(lib_path)
        self.lib.hydra_dsp_get_api_version.restype = c_uint32
        self.api_version = int(self.lib.hydra_dsp_get_api_version())

        self.lib.hydra_dsp_get_param_count.restype = c_uint32
        self.param_count = int(self.lib.hydra_dsp_get_param_count())

        self.lib.hydra_dsp_get_param_spec.argtypes = [c_int, POINTER(HydraDspParamSpec)]
        self.lib.hydra_dsp_get_param_spec.restype = c_int

        # Signatures
        self.lib.hydra_dsp_create_v1_4.argtypes = [c_float, c_float, c_float, POINTER(c_void_p)]
        self.lib.hydra_dsp_create_v1_4.restype = c_int

        self.lib.hydra_dsp_destroy.argtypes = [c_void_p]
        self.lib.hydra_dsp_destroy.restype = None

        self.lib.hydra_dsp_prepare.argtypes = [c_void_p, c_uint32, c_uint32]
        self.lib.hydra_dsp_prepare.restype = c_int

        self.lib.hydra_dsp_reset.argtypes = [c_void_p]
        self.lib.hydra_dsp_reset.restype = None

        self.lib.hydra_dsp_set_parameter.argtypes = [c_void_p, c_int, c_float]
        self.lib.hydra_dsp_set_parameter.restype = c_int

        self.lib.hydra_dsp_get_parameter.argtypes = [c_void_p, c_int, POINTER(c_float)]
        self.lib.hydra_dsp_get_parameter.restype = c_int

        self.lib.hydra_dsp_set_tempo_source.argtypes = [c_void_p, c_uint32]
        self.lib.hydra_dsp_set_tempo_source.restype = c_int

        self.lib.hydra_dsp_transport_play.argtypes = [c_void_p]
        self.lib.hydra_dsp_transport_play.restype = c_int

        self.lib.hydra_dsp_transport_pause.argtypes = [c_void_p]
        self.lib.hydra_dsp_transport_pause.restype = c_int

        self.lib.hydra_dsp_transport_stop.argtypes = [c_void_p]
        self.lib.hydra_dsp_transport_stop.restype = c_int

        self.lib.hydra_dsp_transport_set_bpm.argtypes = [c_void_p, c_float]
        self.lib.hydra_dsp_transport_set_bpm.restype = c_int

        self.lib.hydra_dsp_transport_get_state.argtypes = [c_void_p, POINTER(HydraDspTransportState)]
        self.lib.hydra_dsp_transport_get_state.restype = c_int

        self.lib.hydra_dsp_tap_tempo_event.argtypes = [c_void_p, c_uint64]
        self.lib.hydra_dsp_tap_tempo_event.restype = c_int

        self.lib.hydra_dsp_tap_tempo_get_state.argtypes = [c_void_p, POINTER(HydraDspTapTempoState)]
        self.lib.hydra_dsp_tap_tempo_get_state.restype = c_int

        self.lib.hydra_dsp_midi_clock_event.argtypes = [c_void_p, c_uint8, c_uint64]
        self.lib.hydra_dsp_midi_clock_event.restype = c_int

        self.lib.hydra_dsp_midi_clock_get_state.argtypes = [c_void_p, POINTER(HydraDspMidiClockState)]
        self.lib.hydra_dsp_midi_clock_get_state.restype = c_int

        self.lib.hydra_dsp_get_reverse_diagnostics.argtypes = [
            c_void_p, POINTER(c_float), POINTER(c_float), POINTER(c_int), POINTER(c_float)
        ]
        self.lib.hydra_dsp_get_reverse_diagnostics.restype = c_int

        self.lib.hydra_dsp_process.argtypes = [
            c_void_p, POINTER(c_float), POINTER(c_float),
            POINTER(c_float), POINTER(c_float), c_uint32
        ]
        self.lib.hydra_dsp_process.restype = c_int

    def create_instance(self, sample_rate: float = 48000.0, max_delay_ms: float = 2000.0, history_ms: float = 8000.0):
        handle = c_void_p()
        ret = self.lib.hydra_dsp_create_v1_4(sample_rate, max_delay_ms, history_ms, byref(handle))
        if ret != 0 or not handle:
            raise RuntimeError(f"hydra_dsp_create_v1_4 failed with code {ret}")
        self.lib.hydra_dsp_prepare(handle, BLOCK, 2)
        return handle

    def destroy_instance(self, handle):
        if handle:
            self.lib.hydra_dsp_destroy(handle)


def verify_runner_abi_compatibility(
    before: HydraEngineRunner,
    after: HydraEngineRunner,
    required_param_ids=range(38),
    min_version: tuple[int, int, int] = MIN_COMPATIBLE_API_VERSION,
) -> dict:
    """Verify two HydraEngineRunner libraries share a compatible Phase 6 C ABI."""
    before_ver = decode_api_version(before.api_version)
    after_ver = decode_api_version(after.api_version)
    if before_ver[0] != min_version[0] or after_ver[0] != min_version[0]:
        raise RuntimeError(
            f"Incompatible major API version: before={before_ver}, after={after_ver}, expected major={min_version[0]}"
        )
    if before_ver < min_version or after_ver < min_version:
        raise RuntimeError(
            f"API version below minimum {min_version}: before={before_ver}, after={after_ver}"
        )
    req_ids = list(required_param_ids)
    min_params = (max(req_ids) + 1) if req_ids else 0
    if before.param_count < min_params or after.param_count < min_params:
        raise RuntimeError(
            f"Insufficient parameter count (need >= {min_params}): before={before.param_count}, after={after.param_count}"
        )
    for pid in req_ids:
        b_spec = HydraDspParamSpec()
        a_spec = HydraDspParamSpec()
        if before.lib.hydra_dsp_get_param_spec(pid, byref(b_spec)) != 0:
            raise RuntimeError(f"before library missing param spec for id {pid}")
        if after.lib.hydra_dsp_get_param_spec(pid, byref(a_spec)) != 0:
            raise RuntimeError(f"after library missing param spec for id {pid}")
        if (b_spec.id, b_spec.min_value, b_spec.max_value, b_spec.default_value, b_spec.smoothing, b_spec.flags) != (
            a_spec.id, a_spec.min_value, a_spec.max_value, a_spec.default_value, a_spec.smoothing, a_spec.flags
        ):
            raise RuntimeError(f"Parameter ABI mismatch on param id {pid}")
    return {
        "before_api_version": before.api_version,
        "after_api_version": after.api_version,
        "before_api_semver": list(before_ver),
        "after_api_semver": list(after_ver),
        "before_param_count": before.param_count,
        "after_param_count": after.param_count,
        "verified_param_count": len(req_ids),
    }

def run_simulation(runner: HydraEngineRunner, config: dict) -> dict:
    handle = runner.create_instance(sample_rate=config.get("sample_rate", RATE))
    try:
        # Default parameters
        defaults = {
            PARAM_FLUTTER_DEPTH: 8.0,
            PARAM_WOW_DEPTH: 6.0,
            PARAM_DRIVE: 25.0,
            PARAM_NOISE: 10.0,
            PARAM_DELAY_ACTIVE: 1.0,
            PARAM_DELAY_MS: 500.0,
            PARAM_FEEDBACK: 35.0,
            PARAM_DRY_WET: 50.0,
            PARAM_DELAY_WET: 50.0,
            PARAM_ACTIVE_HEADS: 4.0, # Head 3
            PARAM_BPM: config.get("initial_bpm", 120.0),
            PARAM_HEADS_MUSICAL: 0.0,
            PARAM_PING_PONG: 0.0,
            PARAM_REVERSE: 0.0,
            PARAM_WARP_ACTIVE: 0.0,
            PARAM_SPRING: 0.0,
            PARAM_REVERSE_MODE: REV_MODE_RHYTHMIC,
            PARAM_REVERSE_DIVISION: REV_DIV_QUARTER,
        }
        params = defaults.copy()
        params.update(config.get("params", {}))

        for param_id, val in params.items():
            runner.lib.hydra_dsp_set_parameter(handle, param_id, float(val))

        source_type = config.get("tempo_source", TEMPO_SOURCE_INTERNAL)
        runner.lib.hydra_dsp_set_tempo_source(handle, source_type)

        if config.get("initial_playing", True):
            runner.lib.hydra_dsp_transport_play(handle)
        else:
            runner.lib.hydra_dsp_transport_stop(handle)

        stimulus = config["stimulus"]
        total_frames = len(stimulus)
        output = np.zeros_like(stimulus)

        # Buffers for ctypes
        in_l_buf = (c_float * BLOCK)()
        in_r_buf = (c_float * BLOCK)()
        out_l_buf = (c_float * BLOCK)()
        out_r_buf = (c_float * BLOCK)()

        events = config.get("events", [])
        events_by_frame = {}
        for ev in events:
            f = int(ev["time_sec"] * RATE)
            events_by_frame.setdefault(f, []).append(ev)

        telemetry_rows = []
        clock_pulses = config.get("clock_pulses", [])
        pulse_idx = 0
        num_pulses = len(clock_pulses)

        t_state = HydraDspTransportState()
        m_state = HydraDspMidiClockState()
        tap_state = HydraDspTapTempoState()

        num_blocks = math.ceil(total_frames / BLOCK)
        for b in range(num_blocks):
            frame_start = b * BLOCK
            frame_end = min(frame_start + BLOCK, total_frames)
            block_frames = frame_end - frame_start

            # Process scheduled parameter or control events
            for f in range(frame_start, frame_end):
                if f in events_by_frame:
                    for ev in events_by_frame[f]:
                        ev_type = ev["type"]
                        if ev_type == "param":
                            runner.lib.hydra_dsp_set_parameter(handle, ev["id"], float(ev["value"]))
                        elif ev_type == "bpm":
                            runner.lib.hydra_dsp_transport_set_bpm(handle, float(ev["bpm"]))
                        elif ev_type == "source":
                            runner.lib.hydra_dsp_set_tempo_source(handle, int(ev["source"]))
                        elif ev_type == "play":
                            runner.lib.hydra_dsp_transport_play(handle)
                        elif ev_type == "pause":
                            runner.lib.hydra_dsp_transport_pause(handle)
                        elif ev_type == "stop":
                            runner.lib.hydra_dsp_transport_stop(handle)
                        elif ev_type == "tap":
                            runner.lib.hydra_dsp_tap_tempo_event(handle, f)
                        elif ev_type == "midi":
                            runner.lib.hydra_dsp_midi_clock_event(handle, ev["status"], f)
                        else:
                            raise ValueError(f"Unsupported scheduled event: {ev_type}")

                # Process MIDI clock pulses
                while pulse_idx < num_pulses and clock_pulses[pulse_idx]["frame"] <= f:
                    p = clock_pulses[pulse_idx]
                    runner.lib.hydra_dsp_midi_clock_event(handle, p["status"], p["frame"])
                    pulse_idx += 1

            # Prepare block buffers
            for i in range(block_frames):
                idx = frame_start + i
                in_l_buf[i] = float(stimulus[idx, 0])
                in_r_buf[i] = float(stimulus[idx, 1])

            result = runner.lib.hydra_dsp_process(handle, in_l_buf, in_r_buf, out_l_buf, out_r_buf, block_frames)
            if result != 0:
                raise RuntimeError(f"Processing failed at frame {frame_start}: {result}")

            for i in range(block_frames):
                idx = frame_start + i
                output[idx, 0] = out_l_buf[i]
                output[idx, 1] = out_r_buf[i]

            # Collect telemetry snapshot once every 4 blocks (~10.6 ms)
            if b % 4 == 0:
                runner.lib.hydra_dsp_transport_get_state(handle, byref(t_state))
                runner.lib.hydra_dsp_midi_clock_get_state(handle, byref(m_state))
                runner.lib.hydra_dsp_tap_tempo_get_state(handle, byref(tap_state))
                req_ms = c_float()
                eff_ms = c_float()
                clamped = c_int()
                hist_ms = c_float()
                runner.lib.hydra_dsp_get_reverse_diagnostics(handle, byref(req_ms), byref(eff_ms), byref(clamped), byref(hist_ms))

                block_peak = max(np.max(np.abs(output[frame_start:frame_end])), 1e-9)
                block_rms = np.sqrt(np.mean(output[frame_start:frame_end]**2))

                telemetry_rows.append({
                    "time_sec": round(frame_start / RATE, 4),
                    "transport_bpm": round(float(t_state.bpm), 3),
                    "beat_phase": round(float(t_state.beat_phase), 4),
                    "bar_phase": round(float(t_state.bar_phase), 4),
                    "musical_position": round(float(t_state.musical_position), 4),
                    "transport_sample_pos": int(t_state.transport_sample_position),
                    "is_playing": int(t_state.playing),
                    "tempo_source": int(m_state.tempo_source),
                    "midi_status": int(m_state.sync_status),
                    "midi_applied_bpm": round(float(m_state.applied_bpm), 3),
                    "midi_phase_error": round(float(m_state.phase_error_beats), 5),
                    "tap_applied_bpm": round(float(tap_state.applied_bpm), 3),
                    "tap_count": int(tap_state.tap_count),
                    "reverse_eff_ms": round(float(eff_ms.value), 2),
                    "reverse_clamped": int(clamped.value),
                    "block_peak": round(float(block_peak), 5),
                    "block_rms": round(float(block_rms), 5),
                })

        return {
            "output": output,
            "telemetry": telemetry_rows,
            "parameters": params,
        }

    finally:
        runner.destroy_instance(handle)

def generate_midi_pulses(bpm: float, duration_sec: float, start_sec: float = 0.0,
                         jitter_ms: float = 0.0, drop_interval: tuple = None) -> list:
    pulses = []
    # 0xFA Start at start_sec
    start_frame = int(start_sec * RATE)
    pulses.append({"status": 0xFA, "frame": start_frame})

    # 24 PPQN = 24 pulses per quarter note
    pulse_interval_frames = (60.0 / bpm / 24.0) * RATE
    total_pulses = int((duration_sec - start_sec) * (bpm / 60.0) * 24.0)

    drop_start = int(drop_interval[0] * RATE) if drop_interval else -1
    drop_end = int(drop_interval[1] * RATE) if drop_interval else -1

    for p in range(total_pulses):
        nominal_frame = int(start_frame + p * pulse_interval_frames)
        if drop_start <= nominal_frame <= drop_end:
            continue # Simulates dropped/lost MIDI clock pulses

        frame = nominal_frame
        if jitter_ms > 0:
            jitter_frames = int((np.random.uniform(-jitter_ms, jitter_ms) * 0.001) * RATE)
            frame = max(0, nominal_frame + jitter_frames)

        pulses.append({"status": 0xF8, "frame": frame})

    pulses.sort(key=lambda x: x["frame"])
    return pulses

def build_all_scenarios(output_dir: Path, runner: HydraEngineRunner):
    output_dir.mkdir(parents=True, exist_ok=True)
    np.random.seed(6100)  # MIDI jitter is constructed before the scenario render loop.
    root = Path(__file__).resolve().parents[1]
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()
    manifest = {
        "schema_version": 1,
        "suite": "Hydra Tape Engine 2.0 — Phase 6 Musical Validation & Performance Readiness",
        "reference_commit": commit,
        "api_version": runner.api_version,
        "native_library_sha256": runner.library_sha256,
        "working_tree_dirty": bool(subprocess.check_output(["git", "status", "--porcelain", "--untracked-files=no"], cwd=root, text=True).strip()),
        "environment": {"os": platform.platform(), "python": platform.python_version(), "numpy": np.__version__},
        "random_seed": 6100,
        "event_timing": "Control changes applied at containing block start; MIDI/tap carry sample timestamps. Telemetry every four blocks.",
        "baseline_pr": "#53 — Hotfix MIDI Clock WASM BigInt Interop",
        "sample_rate_hz": RATE,
        "block_size_frames": BLOCK,
        "human_listening_signoff": "PENDING_HUMAN_AUDITION",
        "scenarios": {}
    }

    # 20 Scenarios Specification
    scenarios_def = [
        # GROUP A — TAPE ECHO
        {
            "id": "01_tape_echo_single_head_quarter",
            "group": "Group A — Tape Echo",
            "name": "Single-Head Quarter-Note Echo",
            "description": "Standard 1/4 note delay on clean guitar arpeggio with 35% feedback and warm tape character.",
            "duration": 4.0,
            "bpm": 120.0,
            "source_kind": "clean_guitar",
            "tempo_source": TEMPO_SOURCE_INTERNAL,
            "params": {
                PARAM_DELAY_ACTIVE: 1.0,
                PARAM_DELAY_MS: 500.0,
                PARAM_FEEDBACK: 35.0,
                PARAM_DRY_WET: 50.0,
                PARAM_ACTIVE_HEADS: 4.0, # Head 3 (quarter note)
                PARAM_BPM: 120.0,
                PARAM_HEADS_MUSICAL: 0.0,
            },
            "expected_delay_ms": 500.0,
            "markers": [
                {"time": 0.0, "label": "Stimulus Start (Clean Guitar Arpeggio)"},
                {"time": 0.5, "label": "First Quarter-Note Repeat (500 ms)"},
                {"time": 1.0, "label": "Second Repeat (1000 ms)"},
            ]
        },
        {
            "id": "02_tape_echo_dotted_eighth",
            "group": "Group A — Tape Echo",
            "name": "Dotted-Eighth Rhythmic Echo",
            "description": "Dotted-eighth (375 ms at 120 BPM) delay on 16th-note drum pattern with rhythmic bounce.",
            "duration": 4.0,
            "bpm": 120.0,
            "source_kind": "drum_machine",
            "tempo_source": TEMPO_SOURCE_INTERNAL,
            "params": {
                PARAM_DELAY_ACTIVE: 1.0,
                PARAM_DELAY_MS: 375.0,
                PARAM_FEEDBACK: 40.0,
                PARAM_DRY_WET: 50.0,
                PARAM_ACTIVE_HEADS: 4.0,
                PARAM_BPM: 120.0,
            },
            "expected_delay_ms": 375.0,
            "markers": [
                {"time": 0.0, "label": "Drum Pattern Start"},
                {"time": 0.375, "label": "Dotted-Eighth First Repeat"},
                {"time": 0.750, "label": "Dotted-Eighth Second Repeat"},
            ]
        },
        {
            "id": "03_tape_echo_multi_head_stereo",
            "group": "Group A — Tape Echo",
            "name": "Multi-Head Stereo Pattern",
            "description": "3 heads active (triplet 8th, dotted 8th, quarter) with ping-pong stereo space expansion on synth lead.",
            "duration": 4.0,
            "bpm": 120.0,
            "source_kind": "mono_synth",
            "tempo_source": TEMPO_SOURCE_INTERNAL,
            "params": {
                PARAM_DELAY_ACTIVE: 1.0,
                PARAM_DELAY_MS: 500.0,
                PARAM_FEEDBACK: 45.0,
                PARAM_DRY_WET: 50.0,
                PARAM_ACTIVE_HEADS: 7.0, # Heads 1+2+3 (mask 7)
                PARAM_HEADS_MUSICAL: 1.0, # Musical subdivisions (triplet 8th, dotted 8th, quarter)
                PARAM_PING_PONG: 1.0,
                PARAM_BPM: 120.0,
            },
            "expected_delay_ms": 166.7, # Head 1 triplet
            "markers": [
                {"time": 0.0, "label": "Mono Synth Phrase"},
                {"time": 0.167, "label": "Head 1 Repeat (1/3 beat triplet)"},
                {"time": 0.375, "label": "Head 2 Repeat (dotted 8th)"},
                {"time": 0.500, "label": "Head 3 Repeat (quarter)"},
            ]
        },
        {
            "id": "04_tape_echo_high_feedback_sustained",
            "group": "Group A — Tape Echo",
            "name": "High-Feedback Sustained Echoes",
            "description": "High feedback (82%) on overdriven guitar; testing soft saturation, limiter control and non-runaway decay.",
            "duration": 5.0,
            "bpm": 100.0,
            "source_kind": "driven_guitar",
            "tempo_source": TEMPO_SOURCE_INTERNAL,
            "params": {
                PARAM_DELAY_ACTIVE: 1.0,
                PARAM_DELAY_MS: 450.0,
                PARAM_FEEDBACK: 82.0,
                PARAM_DRY_WET: 60.0,
                PARAM_ACTIVE_HEADS: 4.0,
                PARAM_BPM: 100.0,
            },
            "expected_delay_ms": 450.0,
            "markers": [
                {"time": 0.0, "label": "Overdriven Guitar Attack"},
                {"time": 2.0, "label": "Dry Ceases — Sustained Feedback Tail"},
                {"time": 4.5, "label": "Decaying Tape Saturated Echoes"},
            ]
        },

        # GROUP B — REVERSE V2
        {
            "id": "05_reverse_v2_quarter_note",
            "group": "Group B — Reverse V2",
            "name": "Reverse Quarter-Note",
            "description": "Reverse Tape Echo V2 rhythmic quarter-note window (500 ms at 120 BPM) with clean guitar.",
            "duration": 4.0,
            "bpm": 120.0,
            "source_kind": "clean_guitar",
            "tempo_source": TEMPO_SOURCE_INTERNAL,
            "params": {
                PARAM_DELAY_ACTIVE: 1.0,
                PARAM_REVERSE: 1.0,
                PARAM_REVERSE_MODE: REV_MODE_RHYTHMIC,
                PARAM_REVERSE_DIVISION: REV_DIV_QUARTER,
                PARAM_FEEDBACK: 30.0,
                PARAM_DRY_WET: 50.0,
                PARAM_DELAY_WET: 80.0,
                PARAM_BPM: 120.0,
            },
            "expected_reverse_ms": 500.0,
            "markers": [
                {"time": 0.0, "label": "Clean Pluck (Window Accumulation)"},
                {"time": 0.5, "label": "First Reversed Quarter Note Playback"},
                {"time": 1.0, "label": "Window Boundary 2 Crossfade"},
            ]
        },
        {
            "id": "06_reverse_v2_eighth_note",
            "group": "Group B — Reverse V2",
            "name": "Reverse Eighth-Note",
            "description": "Reverse Tape Echo V2 eighth-note window (250 ms at 120 BPM) on lush polyphonic chords.",
            "duration": 4.0,
            "bpm": 120.0,
            "source_kind": "poly_synth",
            "tempo_source": TEMPO_SOURCE_INTERNAL,
            "params": {
                PARAM_DELAY_ACTIVE: 1.0,
                PARAM_REVERSE: 1.0,
                PARAM_REVERSE_MODE: REV_MODE_RHYTHMIC,
                PARAM_REVERSE_DIVISION: REV_DIV_EIGHTH,
                PARAM_FEEDBACK: 35.0,
                PARAM_DRY_WET: 50.0,
                PARAM_DELAY_WET: 80.0,
                PARAM_BPM: 120.0,
            },
            "expected_reverse_ms": 250.0,
            "markers": [
                {"time": 0.0, "label": "Chord Entry"},
                {"time": 0.25, "label": "Reversed 8th Window Playback"},
                {"time": 0.50, "label": "Continuous Hermite Crossfade"},
            ]
        },
        {
            "id": "07_reverse_v2_triplet",
            "group": "Group B — Reverse V2",
            "name": "Reverse Triplet Quarter-Note",
            "description": "Reverse V2 triplet-quarter window (333.3 ms at 120 BPM) on melodic synth phrase.",
            "duration": 4.0,
            "bpm": 120.0,
            "source_kind": "mono_synth",
            "tempo_source": TEMPO_SOURCE_INTERNAL,
            "params": {
                PARAM_DELAY_ACTIVE: 1.0,
                PARAM_REVERSE: 1.0,
                PARAM_REVERSE_MODE: REV_MODE_RHYTHMIC,
                PARAM_REVERSE_DIVISION: REV_DIV_TRIPLET_QUARTER,
                PARAM_FEEDBACK: 40.0,
                PARAM_DRY_WET: 50.0,
                PARAM_DELAY_WET: 80.0,
                PARAM_BPM: 120.0,
            },
            "expected_reverse_ms": 333.33,
            "markers": [
                {"time": 0.0, "label": "Phrase Start"},
                {"time": 0.333, "label": "Triplet Window 1 Reverse"},
                {"time": 0.667, "label": "Triplet Window 2 Reverse"},
            ]
        },
        {
            "id": "08_reverse_v2_tempo_transition",
            "group": "Group B — Reverse V2",
            "name": "Reverse with Tempo Transition",
            "description": "Dynamic tempo change (120 BPM -> 90 BPM at t=2.0s); verifying window recalculation without buffer reset.",
            "duration": 4.5,
            "bpm": 120.0,
            "source_kind": "clean_guitar",
            "tempo_source": TEMPO_SOURCE_INTERNAL,
            "params": {
                PARAM_DELAY_ACTIVE: 1.0,
                PARAM_REVERSE: 1.0,
                PARAM_REVERSE_MODE: REV_MODE_RHYTHMIC,
                PARAM_REVERSE_DIVISION: REV_DIV_QUARTER,
                PARAM_FEEDBACK: 35.0,
                PARAM_DRY_WET: 50.0,
                PARAM_BPM: 120.0,
            },
            "expected_reverse_ms": 500.0,
            "events": [
                {"time_sec": 2.0, "type": "bpm", "bpm": 90.0}
            ],
            "markers": [
                {"time": 0.0, "label": "Running at 120 BPM (500 ms window)"},
                {"time": 2.0, "label": "Tempo Transition: 120 -> 90 BPM (Window adapts to 666.7 ms)"},
                {"time": 2.667, "label": "New 90 BPM Window Locked"},
            ]
        },
        {
            "id": "09_reverse_v2_high_feedback",
            "group": "Group B — Reverse V2",
            "name": "Reverse with Feedback at High Intensity",
            "description": "High feedback (75%) on Reverse V2; evaluating swell build-up and boundary stability.",
            "duration": 5.0,
            "bpm": 120.0,
            "source_kind": "poly_synth",
            "tempo_source": TEMPO_SOURCE_INTERNAL,
            "params": {
                PARAM_DELAY_ACTIVE: 1.0,
                PARAM_REVERSE: 1.0,
                PARAM_REVERSE_MODE: REV_MODE_RHYTHMIC,
                PARAM_REVERSE_DIVISION: REV_DIV_QUARTER,
                PARAM_FEEDBACK: 75.0,
                PARAM_DRY_WET: 55.0,
                PARAM_DELAY_WET: 80.0,
                PARAM_BPM: 120.0,
            },
            "expected_reverse_ms": 500.0,
            "markers": [
                {"time": 0.0, "label": "Poly Chord Pluck"},
                {"time": 1.5, "label": "Harmonic Swell Build-up"},
                {"time": 3.5, "label": "Stable Non-divergent Reverse Feedback"},
            ]
        },

        # GROUP C — CLOCK & TRANSPORT
        {
            "id": "10_gtc_internal_bpm_ramp",
            "group": "Group C — Clock & Transport",
            "name": "Internal BPM Steps",
            "description": "Internal tempo shifts (120 BPM -> 140 BPM -> 100 BPM); verifying phase continuity and history preservation.",
            "duration": 4.5,
            "bpm": 120.0,
            "source_kind": "drum_machine",
            "tempo_source": TEMPO_SOURCE_INTERNAL,
            "params": {
                PARAM_DELAY_ACTIVE: 1.0,
                PARAM_DELAY_MS: 500.0,
                PARAM_FEEDBACK: 40.0,
                PARAM_DRY_WET: 50.0,
                PARAM_BPM: 120.0,
            },
            "events": [
                {"time_sec": 1.5, "type": "bpm", "bpm": 140.0},
                {"time_sec": 3.0, "type": "bpm", "bpm": 100.0},
            ],
            "markers": [
                {"time": 0.0, "label": "Internal Transport at 120 BPM"},
                {"time": 1.5, "label": "BPM Step: 120 -> 140 BPM"},
                {"time": 3.0, "label": "BPM Step: 140 -> 100 BPM"},
            ]
        },
        {
            "id": "11_tap_tempo_acquisition",
            "group": "Group C — Clock & Transport",
            "name": "Tap Tempo Acquisition & Lock",
            "description": "User taps injected at 90 BPM (666.7 ms intervals); GTC converges from 120 to 90 BPM with Lock status.",
            "duration": 4.5,
            "bpm": 120.0,
            "source_kind": "clean_guitar",
            "tempo_source": TEMPO_SOURCE_TAP,
            "params": {
                PARAM_DELAY_ACTIVE: 1.0,
                PARAM_DELAY_MS: 500.0,
                PARAM_FEEDBACK: 35.0,
                PARAM_DRY_WET: 50.0,
                PARAM_BPM: 120.0,
            },
            "events": [
                {"time_sec": 0.500, "type": "tap"},
                {"time_sec": 1.167, "type": "tap"}, # Tap 2 -> Acquisition
                {"time_sec": 1.833, "type": "tap"}, # Tap 3 -> Convergence
                {"time_sec": 2.500, "type": "tap"}, # Tap 4 -> Lock established
            ],
            "markers": [
                {"time": 0.500, "label": "Tap 1 (Reference Start)"},
                {"time": 1.167, "label": "Tap 2 (Interval measured, ACQUIRING)"},
                {"time": 1.833, "label": "Tap 3 (Convergence to ~90 BPM)"},
                {"time": 2.500, "label": "Tap 4 (LOCKED at 90 BPM)"},
            ]
        },
        {
            "id": "12_midi_clock_stable",
            "group": "Group C — Clock & Transport",
            "name": "MIDI Clock Stable 120 BPM",
            "description": "External MIDI Clock 24 PPQN at exact 120.0 BPM with transport Start; PLL establishes phase lock.",
            "duration": 4.0,
            "bpm": 120.0,
            "source_kind": "drum_machine",
            "tempo_source": TEMPO_SOURCE_MIDI,
            "params": {
                PARAM_DELAY_ACTIVE: 1.0,
                PARAM_DELAY_MS: 500.0,
                PARAM_FEEDBACK: 40.0,
                PARAM_DRY_WET: 50.0,
            },
            "clock_pulses": generate_midi_pulses(bpm=120.0, duration_sec=4.0, start_sec=0.05),
            "markers": [
                {"time": 0.05, "label": "MIDI Start (0xFA)"},
                {"time": 0.25, "label": "PLL Acquiring 120 BPM"},
                {"time": 0.50, "label": "PLL Phase-Locked (120.0 BPM, zero phase error)"},
            ]
        },
        {
            "id": "13_midi_clock_jitter",
            "group": "Group C — Clock & Transport",
            "name": "MIDI Clock Jitter Resistance",
            "description": "MIDI Clock with +/- 4.5 ms random timing jitter; tracking PLL rejects jitter and maintains stable audio delay.",
            "duration": 4.0,
            "bpm": 120.0,
            "source_kind": "drum_machine",
            "tempo_source": TEMPO_SOURCE_MIDI,
            "params": {
                PARAM_DELAY_ACTIVE: 1.0,
                PARAM_DELAY_MS: 500.0,
                PARAM_FEEDBACK: 40.0,
                PARAM_DRY_WET: 50.0,
            },
            "clock_pulses": generate_midi_pulses(bpm=120.0, duration_sec=4.0, start_sec=0.05, jitter_ms=4.5),
            "markers": [
                {"time": 0.05, "label": "MIDI Start with +/- 4.5ms Jitter"},
                {"time": 0.60, "label": "Jitter Filtered, Robust Phase Lock Maintained"},
            ]
        },
        {
            "id": "14_midi_clock_loss_recovery",
            "group": "Group C — Clock & Transport",
            "name": "MIDI Clock Loss & Recovery",
            "description": "MIDI clock stops for 1.2s at t=1.8s (Holdover mode), then resumes at t=3.0s without audio dropouts.",
            "duration": 4.5,
            "bpm": 120.0,
            "source_kind": "poly_synth",
            "tempo_source": TEMPO_SOURCE_MIDI,
            "params": {
                PARAM_DELAY_ACTIVE: 1.0,
                PARAM_DELAY_MS: 500.0,
                PARAM_FEEDBACK: 35.0,
                PARAM_DRY_WET: 50.0,
            },
            "clock_pulses": generate_midi_pulses(bpm=120.0, duration_sec=4.5, start_sec=0.05, drop_interval=(1.8, 3.0)),
            "markers": [
                {"time": 0.05, "label": "MIDI Start (120 BPM Locked)"},
                {"time": 1.80, "label": "Clock Pulses Lost — Enter HOLDOVER Mode"},
                {"time": 3.00, "label": "Clock Resumed — Smooth Relock"},
            ]
        },
        {
            "id": "15_midi_stop_continue",
            "group": "Group C — Clock & Transport",
            "name": "MIDI Stop/Continue Transport",
            "description": "Transport Stop (0xFC) at t=1.8s, followed by Continue (0xFB) at t=2.6s. Delay tails naturally sustain.",
            "duration": 4.5,
            "bpm": 120.0,
            "source_kind": "mono_synth",
            "tempo_source": TEMPO_SOURCE_MIDI,
            "params": {
                PARAM_DELAY_ACTIVE: 1.0,
                PARAM_DELAY_MS: 500.0,
                PARAM_FEEDBACK: 45.0,
                PARAM_DRY_WET: 50.0,
            },
            "clock_pulses": generate_midi_pulses(bpm=120.0, duration_sec=4.5, start_sec=0.05),
            "events": [
                {"time_sec": 1.8, "type": "midi", "status": 0xFC},
                {"time_sec": 2.6, "type": "midi", "status": 0xFB},
            ],
            "markers": [
                {"time": 0.05, "label": "Transport Running (Play)"},
                {"time": 1.80, "label": "MIDI Stop (0xFC) — Tail Decay Preserved"},
                {"time": 2.60, "label": "MIDI Continue (0xFB) — Resume Seamlessly"},
            ]
        },

        # GROUP D — PERFORMANCE READINESS
        {
            "id": "16_perf_live_source_switch",
            "group": "Group D — Performance",
            "name": "Live Source Switching",
            "description": "Scheduled source switching: Internal (120 BPM) -> MIDI Clock (140 BPM) -> Internal (100 BPM). Evaluate continuity and clicks.",
            "duration": 4.5,
            "bpm": 120.0,
            "source_kind": "irregular_percussion",
            "tempo_source": TEMPO_SOURCE_INTERNAL,
            "params": {
                PARAM_DELAY_ACTIVE: 1.0,
                PARAM_DELAY_MS: 500.0,
                PARAM_FEEDBACK: 40.0,
                PARAM_DRY_WET: 50.0,
                PARAM_BPM: 120.0,
            },
            "clock_pulses": generate_midi_pulses(bpm=140.0, duration_sec=4.5, start_sec=0.05),
            "events": [
                {"time_sec": 1.5, "type": "source", "source": TEMPO_SOURCE_MIDI},
                {"time_sec": 3.0, "type": "source", "source": TEMPO_SOURCE_INTERNAL},
                {"time_sec": 3.0, "type": "bpm", "bpm": 100.0},
            ],
            "markers": [
                {"time": 0.0, "label": "Internal Source Active (120 BPM)"},
                {"time": 1.5, "label": "Switch to MIDI External Source (140 BPM)"},
                {"time": 3.0, "label": "Switch back to Internal Source (100 BPM)"},
            ]
        },
        {
            "id": "17_perf_rapid_param_adjustments",
            "group": "Group D — Performance",
            "name": "Rapid Parameter Sweeps",
            "description": "Fast automation of Delay Time (250 -> 650 -> 350 ms) and Feedback (20% -> 75% -> 30%) under playing audio.",
            "duration": 4.0,
            "bpm": 120.0,
            "source_kind": "driven_guitar",
            "tempo_source": TEMPO_SOURCE_INTERNAL,
            "params": {
                PARAM_DELAY_ACTIVE: 1.0,
                PARAM_DELAY_MS: 250.0,
                PARAM_FEEDBACK: 20.0,
                PARAM_DRY_WET: 50.0,
                PARAM_BPM: 120.0,
            },
            "events": [
                {"time_sec": 1.0, "type": "param", "id": PARAM_DELAY_MS, "value": 650.0},
                {"time_sec": 1.2, "type": "param", "id": PARAM_FEEDBACK, "value": 75.0},
                {"time_sec": 2.2, "type": "param", "id": PARAM_DELAY_MS, "value": 350.0},
                {"time_sec": 2.4, "type": "param", "id": PARAM_FEEDBACK, "value": 30.0},
            ],
            "markers": [
                {"time": 0.0, "label": "Steady 250 ms Delay"},
                {"time": 1.0, "label": "Rapid Sweep to 650 ms / 75% Feedback"},
                {"time": 2.2, "label": "Sweep back to 350 ms / 30% Feedback"},
            ]
        },
        {
            "id": "18_perf_effect_activation_bypass",
            "group": "Group D — Performance",
            "name": "Effect Activation & Bypass Ramping",
            "description": "Delay toggled On -> Off -> On (delayActive 1 -> 0 -> 1) with smooth cross-fade ramp to prevent clicks.",
            "duration": 4.0,
            "bpm": 120.0,
            "source_kind": "clean_guitar",
            "tempo_source": TEMPO_SOURCE_INTERNAL,
            "params": {
                PARAM_DELAY_ACTIVE: 1.0,
                PARAM_DELAY_MS: 400.0,
                PARAM_FEEDBACK: 45.0,
                PARAM_DRY_WET: 60.0,
                PARAM_BPM: 120.0,
            },
            "events": [
                {"time_sec": 1.5, "type": "param", "id": PARAM_DELAY_ACTIVE, "value": 0.0},
                {"time_sec": 2.5, "type": "param", "id": PARAM_DELAY_ACTIVE, "value": 1.0},
            ],
            "markers": [
                {"time": 0.0, "label": "Delay Active (1.0)"},
                {"time": 1.5, "label": "Delay Bypassed (delayActive = 0)"},
                {"time": 2.5, "label": "Delay Re-engaged (delayActive = 1)"},
            ]
        },
        {
            "id": "19_perf_long_feedback_tails",
            "group": "Group D — Performance",
            "name": "Long Feedback Tail Decay",
            "description": "Stimulus halts at t=1.2s; repeats decay over 3.8s through head bump filters and tape roll-off without runaway.",
            "duration": 5.0,
            "bpm": 110.0,
            "source_kind": "clean_guitar",
            "tempo_source": TEMPO_SOURCE_INTERNAL,
            "params": {
                PARAM_DELAY_ACTIVE: 1.0,
                PARAM_DELAY_MS: 480.0,
                PARAM_FEEDBACK: 78.0,
                PARAM_DRY_WET: 65.0,
                PARAM_BPM: 110.0,
            },
            "markers": [
                {"time": 0.0, "label": "Clean Guitar Plucks"},
                {"time": 1.2, "label": "Input Silenced — Monitoring Decay Tail"},
                {"time": 4.5, "label": "Tail Residual Energy Attenuated Smoothly"},
            ]
        },
        {
            "id": "20_perf_multi_effect_stress",
            "group": "Group D — Performance",
            "name": "Multi-Effect Stress Scenario",
            "description": "Full chain simultaneously active: Multi-head Delay + Reverse V2 + Accidental Warp + Spring Reverb + Wow/Flutter.",
            "duration": 5.0,
            "bpm": 125.0,
            "source_kind": "irregular_percussion",
            "tempo_source": TEMPO_SOURCE_INTERNAL,
            "params": {
                PARAM_DELAY_ACTIVE: 1.0,
                PARAM_DELAY_MS: 400.0,
                PARAM_FEEDBACK: 50.0,
                PARAM_DRY_WET: 50.0,
                PARAM_ACTIVE_HEADS: 7.0,
                PARAM_HEADS_MUSICAL: 1.0,
                PARAM_REVERSE: 1.0,
                PARAM_REVERSE_MODE: REV_MODE_RHYTHMIC,
                PARAM_REVERSE_DIVISION: REV_DIV_QUARTER,
                PARAM_WARP_ACTIVE: 1.0,
                PARAM_WARP_SPEED: 1.0,
                PARAM_WARP_GRAIN_MS: 120.0,
                PARAM_WARP_FEEDBACK: 40.0,
                PARAM_SPRING: 1.0,
                PARAM_SPRING_MIX: 35.0,
                PARAM_FLUTTER_DEPTH: 15.0,
                PARAM_WOW_DEPTH: 12.0,
                PARAM_BPM: 125.0,
            },
            "markers": [
                {"time": 0.0, "label": "Full Multi-Effect Chain Engaged"},
                {"time": 2.5, "label": "Polyphonic Texture Interaction Peak"},
                {"time": 4.5, "label": "Multi-Module Processing Invariant Confirmed"},
            ]
        },
    ]

    print(f"Rendering {len(scenarios_def)} Musical Validation Scenarios...")

    for scen in scenarios_def:
        np.random.seed(6100 + int(scen["id"][:2]))
        scen_id = scen["id"]
        print(f" -> Rendering scenario: {scen_id} ({scen['name']})...")

        dur = scen["duration"]
        bpm = scen["bpm"]
        stimulus = musical_sources.get_musical_source(scen["source_kind"], dur, RATE, bpm=bpm)
        
        # Mute stimulus for tail analysis scenarios after given cutoff
        if scen_id == "19_perf_long_feedback_tails":
            cutoff = int(1.2 * RATE)
            stimulus[cutoff:, :] = 0.0
        elif scen_id == "04_tape_echo_high_feedback_sustained":
            cutoff = int(2.0 * RATE)
            stimulus[cutoff:, :] = 0.0

        scen["stimulus"] = stimulus
        scen["sample_rate"] = RATE
        scen["initial_bpm"] = bpm

        sim_res = run_simulation(runner, scen)
        output_audio = sim_res["output"]
        telemetry = sim_res["telemetry"]

        orig_wav_path = output_dir / f"{scen_id}_original.wav"
        proc_wav_path = output_dir / f"{scen_id}_processed.wav"
        csv_path = output_dir / f"{scen_id}_telemetry.csv"

        write_wav_24bit(orig_wav_path, stimulus, RATE)
        write_wav_24bit(proc_wav_path, output_audio, RATE)

        # Write CSV telemetry
        if telemetry:
            header = list(telemetry[0].keys())
            with csv_path.open("w", encoding="utf-8") as f_csv:
                f_csv.write(",".join(header) + "\n")
                for row in telemetry:
                    line = ",".join(str(row[k]) for k in header)
                    f_csv.write(line + "\n")

        # Audio analysis
        exp_dly = scen.get("expected_delay_ms")
        exp_rev = scen.get("expected_reverse_ms")
        metrics = audio_analysis.full_audio_analysis(
            output_audio, stimulus, RATE, expected_delay_ms=exp_dly, expected_reverse_ms=exp_rev
        )

        manifest["scenarios"][scen_id] = {
            "name": scen["name"],
            "group": scen["group"],
            "description": scen["description"],
            "duration_sec": dur,
            "bpm": bpm,
            "tempo_source": scen.get("tempo_source", TEMPO_SOURCE_INTERNAL),
            "reference_commit": commit,
            "random_seed": 6100 + int(scen_id[:2]),
            "events": scen.get("events", []),
            "clock_pulses": scen.get("clock_pulses", []),
            "sample_rate_hz": RATE,
            "source_material": scen["source_kind"],
            "original_wav_file": orig_wav_path.name,
            "original_wav_sha256": sha256_file(orig_wav_path),
            "processed_wav_file": proc_wav_path.name,
            "processed_wav_sha256": sha256_file(proc_wav_path),
            "telemetry_csv_file": csv_path.name,
            "telemetry_csv_sha256": sha256_file(csv_path),
            "dsp_params": {str(k): float(v) for k, v in sim_res["parameters"].items()},
            "markers": [{"time": e["time_sec"], "label": json.dumps(e, sort_keys=True)} for e in scen.get("events", [])]
                + [{"time": 0.0, "label": "Audio start"}, {"time": len(stimulus) / RATE, "label": "Audio end"}]
                + [{"time": row["time_sec"], "label": "Beat phase wrap (telemetry)"}
                   for previous, row in zip(telemetry, telemetry[1:])
                   if row["is_playing"] and previous["beat_phase"] - row["beat_phase"] > 0.5]
                + [{"time": p["frame"] / RATE, "label": {250: "MIDI Start", 251: "MIDI Continue", 252: "MIDI Stop"}[p["status"]]}
                   for p in scen.get("clock_pulses", []) if p["status"] in (250, 251, 252)]
                + ([{"time": 1.2, "label": "Input silenced"}] if scen_id.startswith("19_") else [])
                + ([{"time": 2.0, "label": "Input silenced"}] if scen_id.startswith("04_") else []),
            "analysis_metrics": metrics,
        }

    manifest_path = output_dir / "manifest.json"
    with manifest_path.open("w", encoding="utf-8") as f_man:
        json.dump(manifest, f_man, indent=2, allow_nan=False)

    print(f"All 20 scenarios rendered successfully to {output_dir}")
    print(f"Listening Lab manifest written to {manifest_path}")

def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--library", type=Path)
    parser.add_argument("--build-dir", type=Path, default=root / "build-native")
    parser.add_argument("--config", default="Release")
    parser.add_argument("--output", type=Path, default=root / "artifacts/listening-lab/phase6-musical-validation")
    args = parser.parse_args()
    lib_path = resolve_library(args.library, args.build_dir, args.config)
    print(f"Loading native library: {lib_path}")
    runner = HydraEngineRunner(lib_path)
    build_all_scenarios(args.output, runner)

def resolve_library(explicit, build_dir, config):
    if explicit:
        path = explicit.expanduser().resolve()
        if not path.is_file():
            raise FileNotFoundError(f"Native library does not exist: {path}")
        return path
    suffix = {"win32": ".dll", "darwin": ".dylib"}.get(sys.platform, ".so")
    names = [f"{prefix}hydra_abi_test_library{suffix}" for prefix in ("", "lib")]
    directories = [build_dir / config, build_dir, build_dir / "lib" / config, build_dir / "lib"]
    for directory in directories:
        for name in names:
            candidate = directory / name
            if candidate.is_file():
                return candidate.resolve()
    raise FileNotFoundError(f"No native {suffix} library in {directories}. Build hydra_abi_test_library or use --library PATH.")

if __name__ == "__main__":
    main()
