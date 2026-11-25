"""F5 musical-timing bench.

Builds the three timing variants (0 = Vintage F3-A, 1 = Capstan Glide,
2 = Clock-Focused) and measures tempo transitions, Tap, MIDI clock, Reverse,
multiblock rendering and long-term drift. The timing snapshot comes from the
read-only test hook added in dsp/core/f5_timing_hooks.hpp; the same library
built without the hook is rendered too, proving the instrumentation is
output-neutral.
"""
from __future__ import annotations

import ctypes as C
import hashlib
import json
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
OUT = ROOT / 'artifacts' / 'dsp-reform-f5'
sys.path.insert(0, str(ROOT / 'docs' / 'evidence' / 'dsp-reform-f1'))
sys.path.insert(0, str(ROOT / 'scripts'))
import baseline as b  # noqa: E402
import musical_sources  # noqa: E402

RATE = 44100
BLOCK = 128
SEED = 20261124

FLAGS = {
    0: ['-DHYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING=1',
        '-DHYDRA_DSP_REFORM_F2_VARIANT=2',
        '-DHYDRA_DSP_REFORM_F3_VARIANT=0',
        '-DHYDRA_DSP_REFORM_F5_TIMING_VARIANT=0'],
    1: ['-DHYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING=1',
        '-DHYDRA_DSP_REFORM_F2_VARIANT=2',
        '-DHYDRA_DSP_REFORM_F3_VARIANT=0',
        '-DHYDRA_DSP_REFORM_F5_TIMING_VARIANT=1'],
    2: ['-DHYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING=1',
        '-DHYDRA_DSP_REFORM_F2_VARIANT=2',
        '-DHYDRA_DSP_REFORM_F3_VARIANT=0',
        '-DHYDRA_DSP_REFORM_F5_TIMING_VARIANT=2'],
}
TIMING_HOOK = '-DHYDRA_DSP_TEST_HOOKS=1'


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def dump(path: Path, value) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + '\n', encoding='utf-8')


class Timing(C.Structure):
    _fields_ = [
        ('transport_bpm', C.c_double), ('musical_position', C.c_double),
        ('absolute_sample_position', C.c_uint64), ('transport_sample_position', C.c_uint64),
        ('target_beat_ms', C.c_float), ('effective_beat_ms', C.c_float),
        ('head_delay_samples', C.c_float * 3), ('target_head_samples', C.c_float * 3),
        ('smoothed_delay_samples', C.c_float), ('target_delay_samples', C.c_float),
        ('reverse_requested_ms', C.c_float), ('reverse_effective_ms', C.c_float),
        ('reverse_clamped', C.c_int32), ('musical', C.c_int32),
        ('gliding', C.c_int32), ('variant', C.c_int32),
    ]


class Engine:
    def __init__(self, path: Path, hooks: bool = True):
        self.path = Path(path)
        self.hooks = hooks
        if sys.platform == 'win32':
            gcc = shutil.which('g++')
            for directory in [self.path.parent] + ([Path(gcc).parent] if gcc else []):
                os.add_dll_directory(str(directory))
        self.lib = C.CDLL(str(self.path))
        L = self.lib
        L.hydra_dsp_create_v1_4.argtypes = [C.c_float, C.c_float, C.c_float,
                                            C.POINTER(C.c_void_p)]
        L.hydra_dsp_create_v1_4.restype = C.c_int
        L.hydra_dsp_destroy.argtypes = [C.c_void_p]
        L.hydra_dsp_prepare.argtypes = [C.c_void_p, C.c_uint32, C.c_uint32]
        L.hydra_dsp_prepare.restype = C.c_int
        L.hydra_dsp_reset.argtypes = [C.c_void_p]
        L.hydra_dsp_set_parameter.argtypes = [C.c_void_p, C.c_int, C.c_float]
        L.hydra_dsp_set_parameter.restype = C.c_int
        L.hydra_dsp_get_parameter.argtypes = [C.c_void_p, C.c_int, C.POINTER(C.c_float)]
        L.hydra_dsp_get_parameter.restype = C.c_int
        L.hydra_dsp_commit.argtypes = [C.c_void_p]
        L.hydra_dsp_commit.restype = C.c_int
        L.hydra_dsp_process.argtypes = [C.c_void_p, C.POINTER(C.c_float),
                                        C.POINTER(C.c_float), C.POINTER(C.c_float),
                                        C.POINTER(C.c_float), C.c_uint32]
        L.hydra_dsp_process.restype = C.c_int
        L.hydra_dsp_transport_play.argtypes = [C.c_void_p]
        L.hydra_dsp_transport_set_bpm.argtypes = [C.c_void_p, C.c_float]
        L.hydra_dsp_transport_set_bpm.restype = C.c_int
        L.hydra_dsp_set_tempo_source.argtypes = [C.c_void_p, C.c_uint32]
        L.hydra_dsp_set_tempo_source.restype = C.c_int
        L.hydra_dsp_tap_tempo_event.argtypes = [C.c_void_p, C.c_uint64]
        L.hydra_dsp_tap_tempo_event.restype = C.c_int
        L.hydra_dsp_tap_tempo_get_state.argtypes = [C.c_void_p, C.c_void_p]
        L.hydra_dsp_tap_tempo_get_state.restype = C.c_int
        L.hydra_dsp_midi_clock_event.argtypes = [C.c_void_p, C.c_uint8, C.c_uint64]
        L.hydra_dsp_midi_clock_event.restype = C.c_int
        L.hydra_dsp_midi_clock_get_state.argtypes = [C.c_void_p, C.c_void_p]
        L.hydra_dsp_midi_clock_get_state.restype = C.c_int
        L.hydra_dsp_get_reverse_diagnostics.argtypes = [
            C.c_void_p, C.POINTER(C.c_float), C.POINTER(C.c_float),
            C.POINTER(C.c_int), C.POINTER(C.c_float)]
        L.hydra_dsp_get_reverse_diagnostics.restype = C.c_int
        if hooks:
            L.hydra_dsp_test_timing_state.argtypes = [C.c_void_p, C.POINTER(Timing)]
            L.hydra_dsp_test_timing_state.restype = C.c_int

    def create(self, params, source=0):
        h = C.c_void_p()
        assert self.lib.hydra_dsp_create_v1_4(RATE, 2000, 8000, C.byref(h)) == 0 and h
        assert self.lib.hydra_dsp_prepare(h, BLOCK, 2) == 0
        self.lib.hydra_dsp_reset(h)
        # Select the source before writing parameters: the C API refreshes
        # params.bpm from manualBpm, so the other order would silently reset
        # a non-default initial BPM.
        assert self.lib.hydra_dsp_set_tempo_source(h, source) == 0
        for i, v in enumerate(params):
            if i == 29:
                assert float(v) == 0
                continue
            assert self.lib.hydra_dsp_set_parameter(h, i, float(v)) == 0, i
        assert self.lib.hydra_dsp_transport_play(h) == 0
        assert self.lib.hydra_dsp_commit(h) == 0
        return h

    def state(self, h):
        t = Timing()
        assert self.lib.hydra_dsp_test_timing_state(h, C.byref(t)) == 0
        return t

    def destroy_instance(self, handle):
        self.lib.hydra_dsp_destroy(handle)

    def tap_state(self, h):
        class St(C.Structure):
            _fields_ = [('estimated_bpm', C.c_float), ('applied_bpm', C.c_float),
                        ('tap_count', C.c_uint32), ('confidence', C.c_float),
                        ('time_since_last_tap_s', C.c_float), ('is_timed_out', C.c_int32),
                        ('status', C.c_int32), ('last_rejection', C.c_int32)]
        st = St()
        assert self.lib.hydra_dsp_tap_tempo_get_state(h, C.byref(st)) == 0
        return st

    def midi_state(self, h):
        class St(C.Structure):
            _fields_ = [('tempo_source', C.c_uint32), ('sync_status', C.c_int32),
                        ('estimated_bpm', C.c_float), ('applied_bpm', C.c_float),
                        ('phase_error_beats', C.c_float), ('estimated_jitter_ms', C.c_float),
                        ('pulse_count', C.c_uint64), ('last_pulse_frame', C.c_uint64),
                        ('is_transport_running', C.c_int32), ('missed_pulses', C.c_uint32)]
        st = St()
        assert self.lib.hydra_dsp_midi_clock_get_state(h, C.byref(st)) == 0
        return st


# ---------------------------------------------------------------------------
# Parameters and sources
# ---------------------------------------------------------------------------
BASE = b.PARAMS.copy()
for _id, _value in {0: 0, 1: 0, 2: 0, 4: 0, 8: 0, 11: 1, 12: 500, 13: 65,
                    14: 50, 15: 50, 16: 4, 17: 120, 18: 1, 19: 0, 20: 50,
                    21: 0, 22: 0, 23: 0, 24: 0, 25: 0, 30: 0, 36: 0, 37: 0}.items():
    BASE[_id] = _value


def S(**kw):
    p = BASE.copy()
    for k, v in kw.items():
        p[int(k[1:])] = v
    return p


def b128(seconds: float, block: int = BLOCK) -> int:
    return int(round(seconds * RATE / block)) * block


def bed(duration: float, lead: float = 0.25, tail: float = 0.5) -> np.ndarray:
    n = int(duration * RATE)
    t = np.arange(n, dtype=np.float64) / RATE
    am = 0.85 + 0.15 * np.sin(2 * np.pi * 0.7 * t)
    left = (0.14 * np.sin(2 * np.pi * 110 * t) +
            0.10 * np.sin(2 * np.pi * 220 * t + 0.3) +
            0.06 * np.sin(2 * np.pi * 440 * t + 1.1) +
            0.035 * np.sin(2 * np.pi * 1760 * t + 0.7)) * am
    right = (0.14 * np.sin(2 * np.pi * 110 * t + 0.15) +
             0.10 * np.sin(2 * np.pi * 220 * t + 0.55) +
             0.06 * np.sin(2 * np.pi * 440 * t + 1.35) +
             0.035 * np.sin(2 * np.pi * 1760 * t + 0.95)) * am
    rng = np.random.default_rng(SEED)
    left += rng.standard_normal(n) * 0.002
    right += rng.standard_normal(n) * 0.002
    y = np.stack([left, right], axis=1).astype('<f4')
    return np.concatenate([np.zeros((int(lead * RATE), 2), '<f4'), y,
                           np.zeros((int(tail * RATE), 2), '<f4')])


def source_signal(kind: str, duration: float) -> np.ndarray:
    if kind == 'bed':
        return bed(duration)
    # scripts/musical_sources.py draws the driven-guitar pick attack from
    # NumPy's legacy global RNG. Seed it so the listening material can be
    # regenerated byte-identically; the rest of the F5 bench uses default_rng.
    np.random.seed(SEED)
    body = musical_sources.get_musical_source(kind, duration, RATE).astype('<f4')
    if kind in ('electric_bass_proxy', 'low_mono_synth'):
        body = np.zeros((int(duration * RATE), 2), '<f4')
        for j, freq in enumerate([41.2034, 55.0, 73.4162, 82.4069,
                                  55.0, 41.2034, 65.4064, 82.4069]):
            start = int(j * 0.65 * RATE)
            n = min(int(0.6 * RATE), len(body) - start)
            tt = np.arange(n) / RATE
            env = (1 - np.exp(-tt / 0.003)) * np.exp(-tt / 0.22)
            if kind == 'low_mono_synth':
                env = (1 - np.exp(-tt / 0.01)) * np.exp(-tt / 0.5)
            yv = 0.25 * env * sum(np.sin(2 * np.pi * freq * k * tt) / k ** 1.5
                                  for k in range(1, 9))
            body[start:start + n] = yv[:, None]
    return body


def source_full(kind: str, duration: float) -> np.ndarray:
    body = source_signal(kind, duration)
    return np.concatenate([np.zeros((int(0.25 * RATE), 2), '<f4'), body,
                           np.zeros((int(0.5 * RATE), 2), '<f4')])


# ---------------------------------------------------------------------------
# Rendering
# ---------------------------------------------------------------------------
def _render(engine: Engine, x: np.ndarray, params, block: int, actions,
            source: int = 0, per_block_diag: bool = True):
    """actions: sorted [(frame, callable)]. Callables receive (engine, handle)."""
    h = engine.create(params, source)
    out = np.empty_like(x)
    rows = []
    pointer = C.POINTER(C.c_float)
    actions = sorted(actions, key=lambda a: a[0])
    cursor = 0
    try:
        for start in range(0, len(x), block):
            n = min(block, len(x) - start)
            while cursor < len(actions) and actions[cursor][0] < start + n:
                actions[cursor][1](engine, h)
                cursor += 1
            left = np.ascontiguousarray(x[start:start + n, 0])
            right = np.ascontiguousarray(x[start:start + n, 1])
            ol, orr = np.empty(n, np.float32), np.empty(n, np.float32)
            assert engine.lib.hydra_dsp_process(
                h, left.ctypes.data_as(pointer), right.ctypes.data_as(pointer),
                ol.ctypes.data_as(pointer), orr.ctypes.data_as(pointer), n) == 0
            out[start:start + n, 0] = ol
            out[start:start + n, 1] = orr
            if per_block_diag and engine.hooks:
                s = engine.state(h)
                tap = engine.tap_state(h)
                midi = engine.midi_state(h)
                rows.append({
                    'frame': start + n, 'bpm': s.transport_bpm,
                    'musical': s.musical_position,
                    'target_beat_ms': s.target_beat_ms,
                    'effective_beat_ms': s.effective_beat_ms,
                    'head': list(s.head_delay_samples),
                    'target_head': list(s.target_head_samples),
                    'smoothed_delay': s.smoothed_delay_samples,
                    'target_delay': s.target_delay_samples,
                    'reverse_requested': s.reverse_requested_ms,
                    'reverse_effective': s.reverse_effective_ms,
                    'reverse_clamped': s.reverse_clamped,
                    'gliding': s.gliding,
                    'tap_applied': tap.applied_bpm,
                    'tap_status': tap.status,
                    'tap_rejection': tap.last_rejection,
                    'midi_status': midi.sync_status,
                    'midi_applied': midi.applied_bpm,
                    'midi_phase': midi.phase_error_beats,
                    'midi_missed': midi.missed_pulses,
                })
        assert cursor == len(actions)
        return out, rows
    finally:
        engine.lib.hydra_dsp_destroy(h)


def apply_params(change):
    def action(engine, handle):
        for pid, value in change.items():
            assert engine.lib.hydra_dsp_set_parameter(handle, int(pid), float(value)) == 0
        assert engine.lib.hydra_dsp_commit(handle) == 0
    return action


def transport_bpm(value):
    def action(engine, handle):
        assert engine.lib.hydra_dsp_transport_set_bpm(handle, float(value)) == 0
    return action


def tap_at(timestamp):
    def action(engine, handle):
        # Rejected taps (bounce/out-of-range) are a valid contract result.
        engine.lib.hydra_dsp_tap_tempo_event(handle, int(timestamp))
    return action


def midi_at(status, timestamp):
    def action(engine, handle):
        # Out-of-order/ignored bytes are a valid contract result.
        engine.lib.hydra_dsp_midi_clock_event(handle, int(status), int(timestamp))
    return action


def midi_schedule(bpms, duration, change_times, jitter_ms=0.0, drop_every=0):
    """[(frame,status)] pulses for a piecewise BPM schedule."""
    events = [(0, 0xFA)]
    rng = np.random.default_rng(7)
    frame = 0.0
    index = 0
    while frame < duration * RATE:
        bpm = bpms[0]
        for t, value in change_times:
            if frame >= t * RATE:
                bpm = value
        if index:
            if not (drop_every and index % drop_every == 0):
                jitter = (rng.normal(0, jitter_ms / 1000.0 * RATE)
                          if jitter_ms else 0.0)
                events.append((max(0, int(frame + jitter)), 0xF8))
        frame += 60.0 / (24.0 * bpm) * RATE
        index += 1
    return events


def tap_schedule(taps):
    """taps: [(seconds, bpm_interval)] -> [(frame, bpm)] where each next tap
    is scheduled using the previous interval."""
    out = []
    t = taps[0][0]
    out.append(t)
    for seconds, bpm in taps[1:]:
        t += seconds if seconds is not None else 60.0 / bpm
        out.append(t)
    return out


# ---------------------------------------------------------------------------
# Metrics
# ---------------------------------------------------------------------------
def _hf_db(y, start, stop):
    seg = y[start:stop].mean(axis=1).astype(float)
    if len(seg) < 8 or not np.any(seg):
        return -240.0
    spec = np.abs(np.fft.rfft(seg * np.hanning(len(seg)))) ** 2
    freq = np.fft.rfftfreq(len(seg), 1 / RATE)
    total = spec.sum()
    hf = spec[(freq >= 4000) & (freq <= 16000)].sum()
    return float(10 * np.log10(max(hf / total, 1e-24))) if total else -240.0


def _step_excess_db(y, control, event, window=0.05):
    d = np.abs(np.diff(y.mean(axis=1).astype(float)))
    c = np.abs(np.diff(control.mean(axis=1).astype(float)))
    a = d[event:min(len(d), event + int(window * RATE))]
    b = c[event:min(len(c), event + int(window * RATE))]
    ratio = (np.max(a) if len(a) else 0.0) / max(np.max(b) if len(b) else 0.0, 1e-12)
    return float(20 * np.log10(max(ratio, 1e-12)))


def timing_metrics(rows, y, control, event_frame, action_frame, nominal=None):
    rows = [r for r in rows if r['frame'] >= action_frame]
    if not rows:
        return {}
    frames = np.array([r['frame'] for r in rows])
    head = np.array([r['head'][2] for r in rows])
    target = np.array([r['target_head'][2] for r in rows], dtype=float)
    if nominal is not None:
        target = np.full_like(target, nominal)
    err = head - target
    abs_err = np.abs(err)

    def first_within(limit):
        idx = np.flatnonzero(abs_err <= limit)
        return float((frames[idx[0]] - action_frame) / RATE) if len(idx) else None

    peak_step = 0.0
    # Per-block average is a lower bound; the transient pass refines it.
    if len(head) > 1:
        peak_step = float(np.max(np.abs(np.diff(head))) / BLOCK)
    # overshoot relative to the final target
    final = target[-1]
    initial_sign = np.sign(target[0] - head[0]) if len(head) else 0
    overshoot = 0.0
    if initial_sign > 0:
        overshoot = float(max(0.0, np.max(head) - final)) if final >= head[0] else 0.0
    else:
        overshoot = float(max(0.0, final - np.min(head))) if final <= head[0] else 0.0
    gesture = slice(event_frame, min(len(y), event_frame + int(0.5 * RATE)))
    pre = slice(max(0, event_frame - int(0.25 * RATE)), event_frame)
    return {
        'action_frame': int(action_frame),
        'convergence_to_1ms_s': first_within(44.1),
        'convergence_to_0p1ms_s': first_within(4.41),
        'convergence_to_1sample_s': first_within(1.0),
        'max_error_samples': float(np.max(abs_err)),
        'max_error_ms': float(np.max(abs_err) / RATE * 1000.0),
        'max_error_beats': float(np.max(abs_err) / max(1.0, float(np.mean(target)))),
        'final_error_samples': float(abs_err[-1]),
        'peak_block_velocity': peak_step,
        'overshoot_samples': overshoot,
        'peak_gesture': float(np.max(np.abs(y[gesture]))) if len(y[gesture]) else 0.0,
        'rms_gesture': float(np.sqrt(np.mean(y[gesture].astype(float) ** 2))) if len(y[gesture]) else 0.0,
        'rms_pre': float(np.sqrt(np.mean(y[pre].astype(float) ** 2))) if len(y[pre]) else 0.0,
        'max_sample_step_gesture': float(np.max(np.abs(np.diff(y[gesture].mean(axis=1))))) if len(y[gesture]) > 1 else 0.0,
        'step_excess_db': _step_excess_db(y, control, event_frame),
        'hf_db_5ms': _hf_db(y, event_frame, event_frame + int(0.005 * RATE)),
        'control_hf_db_5ms': _hf_db(control, event_frame, event_frame + int(0.005 * RATE)),
        'finite': bool(np.isfinite(y).all()),
        'transport_bpm_final': float(rows[-1]['bpm']),
        'read_target_final': float(rows[-1]['target_head'][2]),
        'read_effective_final': float(head[-1]),
    }


def transient_metrics(engine: Engine, x, params, actions, action_frame, source=0):
    """1-frame rendering over the transition window for the true peak step."""
    window = int(1.2 * RATE)
    pre = max(0, action_frame - int(0.25 * RATE))
    h = engine.create(params, source)
    pointer = C.POINTER(C.c_float)
    head_prev = None
    peak = 0.0
    peak_signed = 0.0
    fired = 0
    try:
        # pre-roll at block size then per-frame window; clock events are
        # delivered at the same block edges as in the audible render.
        for start in range(0, pre, BLOCK):
            n = min(BLOCK, pre - start)
            while fired < len(actions) and actions[fired][0] < start + n:
                actions[fired][1](engine, h)
                fired += 1
            left = np.ascontiguousarray(x[start:start + n, 0])
            right = np.ascontiguousarray(x[start:start + n, 1])
            ol, orr = np.empty(n, np.float32), np.empty(n, np.float32)
            engine.lib.hydra_dsp_process(h, left.ctypes.data_as(pointer),
                                         right.ctypes.data_as(pointer),
                                         ol.ctypes.data_as(pointer),
                                         orr.ctypes.data_as(pointer), n)
        stop = min(len(x), action_frame + window)
        for frame in range(pre, stop):
            while fired < len(actions) and actions[fired][0] <= frame:
                actions[fired][1](engine, h)
                fired += 1
            left = np.array([x[frame, 0]], np.float32)
            right = np.array([x[frame, 1]], np.float32)
            ol, orr = np.empty(1, np.float32), np.empty(1, np.float32)
            engine.lib.hydra_dsp_process(h, left.ctypes.data_as(pointer),
                                         right.ctypes.data_as(pointer),
                                         ol.ctypes.data_as(pointer),
                                         orr.ctypes.data_as(pointer), 1)
            s = engine.state(h)
            head = (s.head_delay_samples[2] if s.musical
                    else s.smoothed_delay_samples)
            if head_prev is not None:
                d = head - head_prev
                if abs(d) > abs(peak_signed):
                    peak_signed = d
                peak = max(peak, abs(d))
            head_prev = head
        ratio = max(1e-6, 1.0 - peak_signed)
        return {'max_read_velocity': float(peak),
                'peak_velocity_signed': float(peak_signed),
                'peak_pitch_cents_signed': float(1200 * np.log2(ratio)),
                'peak_pitch_cents_abs': float(abs(1200 * np.log2(ratio)))}
    finally:
        engine.lib.hydra_dsp_destroy(h)


def _build_digest() -> str:
    sources = [
        'dsp/core/tape_core.cpp', 'dsp/core/tape_core.hpp',
        'dsp/core/hydra_dsp_c_api.cpp', 'dsp/core/musical_timing_experiment.hpp',
        'dsp/core/f5_timing_hooks.hpp', 'dsp/core/fast_tanh_table.inc',
        'include/hydra_dsp.h', 'include/hydra_mech_noise.hpp',
    ]
    h = hashlib.sha256()
    for name in sources:
        h.update((ROOT / name).read_bytes())
    return h.hexdigest()[:12]


def build(variant: int, hooks: bool = True) -> Path:
    suffix = '.dll' if os.name == 'nt' else '.so'
    tag = f'v{variant}' + ('' if hooks else '-plain')
    build_dir = ROOT / f'build/f5-{tag}-native'
    build_dir.mkdir(parents=True, exist_ok=True)
    # One unique library per source digest: rebuilding over a DLL that the
    # current process still has loaded is denied by Windows and needlessly
    # repeats work across actions.
    lib = build_dir / f'libhydra_f5_{tag}_{_build_digest()}{suffix}'
    if lib.exists():
        return lib
    compiler = os.environ.get('CXX', 'g++')
    # Linux links a PIE by default and rejects non-PIC objects in a shared
    # library; Windows PE builds do not need the flag.
    cmd = [compiler, '-O3', '-DNDEBUG', '-std=c++17', '-ffp-contract=off', '-shared',
           *(['-fPIC'] if os.name != 'nt' else []),
           *FLAGS[variant], *([TIMING_HOOK] if hooks else []),
           '-I' + str(ROOT / 'include'), '-I' + str(ROOT / 'dsp/core'),
           str(ROOT / 'dsp/core/tape_core.cpp'),
           str(ROOT / 'dsp/core/hydra_dsp_c_api.cpp'),
           '-o', str(lib)]
    result = subprocess.run(cmd, capture_output=True, text=True)
    (build_dir / 'build.log').write_text(result.stdout + result.stderr, encoding='utf-8')
    if result.returncode:
        raise RuntimeError(f'build {tag} failed:\n{result.stderr[-3000:]}')
    return lib


def provenance(variant: int, lib: Path, hooks: bool = True) -> dict:
    return {
        'variant': variant,
        'hooks': hooks,
        'flags': FLAGS[variant] + ([TIMING_HOOK] if hooks else []),
        'library': lib.relative_to(ROOT).as_posix(),
        'library_sha256': sha(lib.read_bytes()),
        'library_bytes': lib.stat().st_size,
        'compiler': subprocess.check_output(['g++', '--version'], text=True).splitlines()[0],
        'source_sha256': {
            name: sha((ROOT / name).read_bytes()) for name in [
                'dsp/core/tape_core.cpp', 'dsp/core/tape_core.hpp',
                'dsp/core/hydra_dsp_c_api.cpp', 'dsp/core/fast_tanh_table.inc',
                'dsp/core/musical_timing_experiment.hpp', 'dsp/core/f5_timing_hooks.hpp',
                'include/hydra_dsp.h']},
    }

