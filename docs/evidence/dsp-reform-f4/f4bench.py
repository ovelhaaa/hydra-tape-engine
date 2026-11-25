"""F4 performance-transition bench.

Reproducible, block-accurate automation for the Frozen Vintage F3-A reference
(HYDRA_DSP_REFORM_F4_TRANSITIONS=0) and the opt-in F4 transition build (=1).
All commands are delivered at block edges, exactly like the host adapter.

Actions:
  build          compile both explicit libraries and record provenance
  baseline       prove F4=0 is bit-exact with the published F3-A manifest
  probe          print a compact before/after transition table
  measure        full transition measurements (all scenes, both variants)
  blocksize      event at the common multiple of 1/37/64/128/256
  render         deterministic F4=0/1 listening renders
  package        A/B listening package (RAW + MATCHED), hashes, answer key
"""
from __future__ import annotations

import argparse
import ctypes as C
import hashlib
import json
import os
import platform
import random
import shutil
import subprocess
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
OUT = ROOT / 'artifacts' / 'dsp-reform-f4'
sys.path.insert(0, str(ROOT / 'docs' / 'evidence' / 'dsp-reform-f1'))
sys.path.insert(0, str(ROOT / 'scripts'))
import baseline as b  # noqa: E402  (frozen F1/F0.2 recipe)


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def dump(path: Path, value) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, allow_nan=False, sort_keys=False) + '\n',
                    encoding='utf-8')


FLAGS = {
    0: ['-DHYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING=1',
        '-DHYDRA_DSP_REFORM_F2_VARIANT=2',
        '-DHYDRA_DSP_REFORM_F3_VARIANT=0',
        '-DHYDRA_DSP_REFORM_F4_TRANSITIONS=0'],
    1: ['-DHYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING=1',
        '-DHYDRA_DSP_REFORM_F2_VARIANT=2',
        '-DHYDRA_DSP_REFORM_F3_VARIANT=0',
        '-DHYDRA_DSP_REFORM_F4_TRANSITIONS=1'],
}

# F4 experiments are explicitly evaluated on the frozen Vintage setting.
BASE = b.PARAMS.copy()
# Keep the measurement bed free of modulation/noise so a gain step is not
# confused with mechanical movement; identity is preserved by using F3=0.
for _id, _value in {0: 0, 1: 0, 2: 0, 4: 0, 8: 0, 11: 1, 12: 500, 13: 65,
                    14: 50, 15: 50, 16: 4, 17: 120, 18: 0, 19: 0, 20: 50,
                    21: 0, 22: 0, 23: 0, 24: 0, 25: 0, 30: 0, 36: 0, 37: 0}.items():
    BASE[_id] = _value


def b128(seconds: float, block: int = 128, rate: int = 44100) -> int:
    """Nearest block edge to a musical instant; the exact frame is recorded."""
    return int(round(seconds * rate / block)) * block


def bed(duration: float, rate: int = 44100, lead: float = 0.25, tail: float = 0.5) -> np.ndarray:
    """Steady harmonic bed plus a little seeded noise.

    Harmonic content is deliberately static: the only discontinuity in the
    window is the parameter command. Natural sample-to-sample slope is the
    control baseline for each measurement.
    """
    n = int(duration * rate)
    t = np.arange(n, dtype=np.float64) / rate
    am = 0.85 + 0.15 * np.sin(2 * np.pi * 0.7 * t)
    left = (0.14 * np.sin(2 * np.pi * 110 * t) +
            0.10 * np.sin(2 * np.pi * 220 * t + 0.3) +
            0.06 * np.sin(2 * np.pi * 440 * t + 1.1) +
            0.035 * np.sin(2 * np.pi * 1760 * t + 0.7)) * am
    right = (0.14 * np.sin(2 * np.pi * 110 * t + 0.15) +
             0.10 * np.sin(2 * np.pi * 220 * t + 0.55) +
             0.06 * np.sin(2 * np.pi * 440 * t + 1.35) +
             0.035 * np.sin(2 * np.pi * 1760 * t + 0.95)) * am
    rng = np.random.default_rng(20261124)
    left += rng.standard_normal(n) * 0.002
    right += rng.standard_normal(n) * 0.002
    y = np.stack([left, right], axis=1).astype('<f4')
    pre = int(lead * rate)
    post = int(tail * rate)
    return np.concatenate([np.zeros((pre, 2), '<f4'), y, np.zeros((post, 2), '<f4')])


def musical(kind: str, duration: float, rate: int = 44100) -> np.ndarray:
    src = b.musical_sources.get_musical_source(kind, duration, rate).astype('<f4')
    if kind in ('electric_bass_proxy', 'low_mono_synth'):
        src = np.zeros((int(duration * rate), 2), '<f4')
        for j, freq in enumerate([41.2034, 55.0, 73.4162, 82.4069, 55.0, 41.2034, 65.4064, 82.4069]):
            start = int(j * 0.65 * rate)
            n = min(int(0.6 * rate), len(src) - start)
            tt = np.arange(n) / rate
            env = (1 - np.exp(-tt / 0.003)) * np.exp(-tt / 0.22)
            if kind == 'low_mono_synth':
                env = (1 - np.exp(-tt / 0.01)) * np.exp(-tt / 0.5)
            yv = 0.25 * env * sum(np.sin(2 * np.pi * freq * k * tt) / k ** 1.5 for k in range(1, 9))
            src[start:start + n] = yv[:, None]
    return src


def source_signal(kind: str, duration: float, rate: int = 44100,
                  lead: float = 0.25, tail: float = 0.5) -> np.ndarray:
    if kind == 'bed':
        return bed(duration, rate, lead, tail)
    body = musical(kind, duration - lead - tail, rate)
    return np.concatenate([np.zeros((int(lead * rate), 2), '<f4'), body,
                           np.zeros((int(tail * rate), 2), '<f4')])


class Engine:
    def __init__(self, lib_path: Path):
        self.lib_path = Path(lib_path)
        if sys.platform == 'win32':
            gcc = shutil.which('gcc')
            for directory in [self.lib_path.parent] + ([Path(gcc).parent] if gcc else []):
                os.add_dll_directory(str(directory))
        self.lib = C.CDLL(str(self.lib_path))
        self.lib.hydra_dsp_create_v1_4.argtypes = [C.c_float, C.c_float, C.c_float, C.POINTER(C.c_void_p)]
        self.lib.hydra_dsp_create_v1_4.restype = C.c_int
        self.lib.hydra_dsp_destroy.argtypes = [C.c_void_p]
        self.lib.hydra_dsp_prepare.argtypes = [C.c_void_p, C.c_uint32, C.c_uint32]
        self.lib.hydra_dsp_prepare.restype = C.c_int
        self.lib.hydra_dsp_reset.argtypes = [C.c_void_p]
        self.lib.hydra_dsp_set_parameter.argtypes = [C.c_void_p, C.c_int, C.c_float]
        self.lib.hydra_dsp_set_parameter.restype = C.c_int
        self.lib.hydra_dsp_get_parameter.argtypes = [C.c_void_p, C.c_int, C.POINTER(C.c_float)]
        self.lib.hydra_dsp_get_parameter.restype = C.c_int
        self.lib.hydra_dsp_set_tempo_source.argtypes = [C.c_void_p, C.c_uint32]
        self.lib.hydra_dsp_transport_play.argtypes = [C.c_void_p]
        self.lib.hydra_dsp_commit.argtypes = [C.c_void_p]
        self.lib.hydra_dsp_commit.restype = C.c_int
        self.lib.hydra_dsp_process.argtypes = [C.c_void_p, C.POINTER(C.c_float), C.POINTER(C.c_float),
                                               C.POINTER(C.c_float), C.POINTER(C.c_float), C.c_uint32]
        self.lib.hydra_dsp_process.restype = C.c_int

    def destroy_instance(self, handle):
        self.lib.hydra_dsp_destroy(handle)


def render(engine: Engine, x: np.ndarray, params, block: int = 128,
           events=(), rate: int = 44100) -> tuple[np.ndarray, list[dict]]:
    """Render x with parameter commits exactly at block starts.

    events: [(frame, {param_id: value})]. frame must be a multiple of block so
    the command instant is identical at every supported block size used by the
    block-size contract.
    """
    events = sorted(events, key=lambda e: e[0])
    for frame, _ in events:
        assert frame % block == 0, f'event {frame} is not a block edge for block {block}'
    handle = C.c_void_p()
    assert engine.lib.hydra_dsp_create_v1_4(rate, 2000, 8000, C.byref(handle)) == 0 and handle
    try:
        assert engine.lib.hydra_dsp_prepare(handle, 128, 2) == 0
        engine.lib.hydra_dsp_reset(handle)
        for i, value in enumerate(params):
            if i == 29:
                assert float(value) == 0
                continue
            assert engine.lib.hydra_dsp_set_parameter(handle, i, float(value)) == 0
        assert engine.lib.hydra_dsp_set_tempo_source(handle, 0) == 0
        assert engine.lib.hydra_dsp_transport_play(handle) == 0
        assert engine.lib.hydra_dsp_commit(handle) == 0
        out = np.empty_like(x)
        pointer = C.POINTER(C.c_float)
        event_cursor = 0
        applied = []
        for start in range(0, len(x), block):
            n = min(block, len(x) - start)
            while event_cursor < len(events) and events[event_cursor][0] == start:
                for param_id, value in events[event_cursor][1].items():
                    assert engine.lib.hydra_dsp_set_parameter(handle, int(param_id), float(value)) == 0
                assert engine.lib.hydra_dsp_commit(handle) == 0
                applied.append({'frame': start, 'seconds': start / rate,
                                'params': {str(k): float(v) for k, v in events[event_cursor][1].items()}})
                event_cursor += 1
            left = np.ascontiguousarray(x[start:start + n, 0])
            right = np.ascontiguousarray(x[start:start + n, 1])
            ol, orr = np.empty(n, np.float32), np.empty(n, np.float32)
            assert engine.lib.hydra_dsp_process(handle, left.ctypes.data_as(pointer),
                                                right.ctypes.data_as(pointer), ol.ctypes.data_as(pointer),
                                                orr.ctypes.data_as(pointer), n) == 0
            out[start:start + n, 0] = ol
            out[start:start + n, 1] = orr
        assert event_cursor == len(events), 'event after end of input'
        assert np.isfinite(out).all(), 'non-finite output'
        return out, applied
    finally:
        engine.lib.hydra_dsp_destroy(handle)


def build(variant: int) -> Path:
    build_dir = ROOT / f'build/f4-{variant}-native'
    build_dir.mkdir(parents=True, exist_ok=True)
    suffix = '.dll' if os.name == 'nt' else '.so'
    lib = build_dir / f'libhydra_f4{suffix}'
    compiler = os.environ.get('CXX', 'g++')
    cmd = [compiler, '-O3', '-DNDEBUG', '-std=c++17', '-ffp-contract=off', '-shared',
           *FLAGS[variant], '-I' + str(ROOT / 'include'), '-I' + str(ROOT / 'dsp/core'),
           str(ROOT / 'dsp/core/tape_core.cpp'), str(ROOT / 'dsp/core/hydra_dsp_c_api.cpp'),
           '-o', str(lib)]
    result = subprocess.run(cmd, capture_output=True, text=True)
    (build_dir / 'build.log').write_text(result.stdout + result.stderr, encoding='utf-8')
    if result.returncode:
        raise RuntimeError(f'build {variant} failed:\n{result.stderr[-3000:]}')
    return lib


# ---------------------------------------------------------------------------
# Scenes. Every command is expressed in seconds and snapped to a block edge.
# ---------------------------------------------------------------------------
def S(**kw):
    p = BASE.copy()
    for k, v in kw.items():
        p[int(k[1:])] = v
    return p


SCENES = {
    'mix_0_100_0': {
        'params': S(p14=0, p15=0), 'source': 'bed', 'duration': 4.0,
        'events': [(1.5, {14: 100, 15: 100}), (2.5, {14: 0, 15: 0})],
    },
    'mix_100_0': {
        'params': S(p14=100, p15=100), 'source': 'bed', 'duration': 3.5,
        'events': [(1.5, {14: 0, 15: 0})],
    },
    'mix_25_75': {
        'params': S(p14=25, p15=25), 'source': 'bed', 'duration': 3.5,
        'events': [(1.5, {14: 75, 15: 75})],
    },
    'drive_0_100_25': {
        'params': S(p3=0), 'source': 'bed', 'duration': 5.0,
        'events': [(1.5, {3: 100}), (2.5, {3: 25})],
    },
    'drive_100_25_stored': {
        'params': S(p3=100), 'source': 'bed', 'duration': 5.0,
        'events': [(1.5, {3: 25})],
    },
    'head_3_to_123': {
        'params': S(p16=4), 'source': 'poly_synth', 'duration': 4.0,
        'events': [(1.5, {16: 7})],
    },
    'head_1_to_3': {
        'params': S(p16=1), 'source': 'poly_synth', 'duration': 4.0,
        'events': [(1.5, {16: 4})],
    },
    'head_12_to_23': {
        'params': S(p16=3), 'source': 'poly_synth', 'duration': 4.0,
        'events': [(1.5, {16: 6})],
    },
    'heads_rapid': {
        'params': S(p16=4), 'source': 'poly_synth', 'duration': 4.0,
        'events': [(1.5, {16: 7}), (1.55, {16: 1}), (1.60, {16: 6}), (1.65, {16: 3})],
    },
    'freeze_on': {
        'params': S(p16=7, p12=300), 'source': 'clean_guitar', 'duration': 5.0,
        'events': [(1.5, {22: 1})],
    },
    'freeze_on_off': {
        'params': S(p16=7, p12=300), 'source': 'clean_guitar', 'duration': 5.0,
        'events': [(1.5, {22: 1}), (2.5, {22: 0})],
    },
    'freeze_rapid': {
        'params': S(p16=7, p12=300), 'source': 'clean_guitar', 'duration': 4.0,
        'events': [(1.4, {22: 1}), (1.6, {22: 0}), (1.8, {22: 1}), (2.0, {22: 0}), (2.2, {22: 1})],
    },
    'freeze_high_fb': {
        'params': S(p16=7, p12=300, p13=95), 'source': 'clean_guitar', 'duration': 5.0,
        'events': [(1.5, {22: 1}), (2.5, {22: 0})],
    },
    'freeze_reverse_multi': {
        'params': S(p16=7, p12=300, p23=1, p36=0), 'source': 'driven_guitar', 'duration': 6.0,
        'events': [(1.5, {22: 1}), (3.0, {22: 0})],
    },
    'tone_0_100_0': {
        'params': S(p13=85, p20=0), 'source': 'bed', 'duration': 4.0,
        'events': [(1.5, {20: 100}), (2.5, {20: 0})],
    },
    'tone_100_0_fb95': {
        'params': S(p13=95, p20=100), 'source': 'bed', 'duration': 4.0,
        'events': [(1.5, {20: 0})],
    },
    'reverse_head_switch': {
        'params': S(p16=4, p23=1, p36=0, p12=300), 'source': 'poly_synth', 'duration': 5.0,
        'events': [(1.5, {16: 7}), (2.5, {16: 1})],
    },
    'multihead_mix': {
        'params': S(p16=7, p14=0, p15=0), 'source': 'bed', 'duration': 4.0,
        'events': [(1.5, {14: 100, 15: 100})],
    },
    'control_no_automation': {
        'params': S(p16=7), 'source': 'bed', 'duration': 4.0, 'events': [],
    },
    'control_mix_no_automation': {
        'params': S(p14=50, p15=50), 'source': 'bed', 'duration': 4.0, 'events': [],
    },
}


def scene_input(name: str, block: int = 128) -> tuple[np.ndarray, list]:
    scene = SCENES[name]
    x = source_signal(scene.get('source', 'bed'), scene['duration'])
    events = [(b128(sec, block), params) for sec, params in scene['events']]
    return x, events


def high_freq_ratio(y: np.ndarray, start: int, stop: int, rate: int = 44100) -> float:
    seg = y[start:stop].mean(axis=1).astype(float)
    if len(seg) < 8 or not np.any(seg):
        return 0.0
    spec = np.abs(np.fft.rfft(seg * np.hanning(len(seg)))) ** 2
    freq = np.fft.rfftfreq(len(seg), 1 / rate)
    total = spec.sum()
    hf = spec[(freq >= 4000) & (freq <= 16000)].sum()
    return float(hf / total) if total else 0.0


def highpass_mono(y: np.ndarray, rate: int = 44100) -> np.ndarray:
    from scipy import signal as sps
    mono = y.mean(axis=1).astype(float)
    sos = sps.butter(2, 4000.0 / (rate / 2.0), 'high', output='sos')
    return sps.sosfilt(sos, mono)


def hf_rms_db(hf: np.ndarray, start: int, stop: int) -> float:
    seg = hf[max(0, start):min(len(hf), stop)]
    if len(seg) == 0:
        return -240.0
    rms = float(np.sqrt(np.mean(seg ** 2)))
    return 20.0 * np.log10(max(rms, 1e-12))


def step_metrics(y: np.ndarray, ev: int, rate: int = 44100) -> dict:
    mono = y.mean(axis=1).astype(float)
    d = np.abs(np.diff(mono))
    pre = d[max(0, ev - int(0.050 * rate)):max(0, ev - 1)]
    boundary = d[ev - 1] if 0 <= ev - 1 < len(d) else 0.0
    around = d[max(0, ev - 2):min(len(d), ev + int(0.005 * rate))]
    post1ms = d[max(0, ev - 1):min(len(d), ev + int(0.001 * rate))]
    post50 = d[max(0, ev):min(len(d), ev + int(0.050 * rate))]
    return {
        'pre_max_step_50ms': float(pre.max()) if len(pre) else 0.0,
        'boundary_step': float(boundary),
        'post_max_step_1ms': float(post1ms.max()) if len(post1ms) else 0.0,
        'post_max_step_5ms': float(around.max()) if len(around) else 0.0,
        'post_max_step_50ms': float(post50.max()) if len(post50) else 0.0,
    }


def scene_metrics(cmd: np.ndarray, control: np.ndarray, events, rate: int = 44100) -> dict:
    hf_cmd = highpass_mono(cmd, rate)
    hf_ctl = highpass_mono(control, rate)
    rows = []
    for ev, params in events:
        a = step_metrics(cmd, ev, rate)
        c = step_metrics(control, ev, rate)
        pre = cmd[max(0, ev - int(0.050 * rate)):ev]
        post = cmd[ev:min(len(cmd), ev + int(0.050 * rate))]
        cpost = control[ev:min(len(control), ev + int(0.050 * rate))]
        win = int(0.005 * rate)
        hf_cmd_db = hf_rms_db(hf_cmd, ev, ev + win)
        hf_ctl_db = hf_rms_db(hf_ctl, ev, ev + win)
        hf_pre_db = hf_rms_db(hf_cmd, ev - int(0.050 * rate), ev)
        excess = a['post_max_step_5ms'] / max(c['post_max_step_5ms'], 1e-9)
        rows.append({
            'frame': ev, 'seconds': ev / rate, 'params': {str(k): float(v) for k, v in params.items()},
            'pre_rms_50ms': float(np.sqrt(np.mean(pre.astype(float) ** 2))) if len(pre) else 0.0,
            'post_rms_50ms': float(np.sqrt(np.mean(post.astype(float) ** 2))) if len(post) else 0.0,
            'post_peak_10ms': float(np.max(np.abs(cmd[ev:min(len(cmd), ev + int(0.010 * rate))])))
            if ev < len(cmd) else 0.0,
            'pre_peak_50ms': float(np.max(np.abs(pre))) if len(pre) else 0.0,
            'step': a, 'control_step': c,
            'step_excess_vs_control': float(excess),
            'step_excess_db': float(20 * np.log10(max(excess, 1e-12))),
            'hf_rms_dbfs_5ms': hf_cmd_db,
            'control_hf_rms_dbfs_5ms': hf_ctl_db,
            'hf_pre_rms_dbfs_50ms': hf_pre_db,
            'hf_excess_db': float(hf_cmd_db - hf_ctl_db),
            'hf_over_pre_db': float(hf_cmd_db - hf_pre_db),
            'stereo_correlation_post_10ms': float(np.corrcoef(
                post[:int(0.010 * rate), 0].astype(float), post[:int(0.010 * rate), 1].astype(float))[0, 1])
            if len(post) > 16 and np.std(post[:int(0.010 * rate), 0]) > 0 and
               np.std(post[:int(0.010 * rate), 1]) > 0 else 1.0,
            'control_rms_50ms': float(np.sqrt(np.mean(cpost.astype(float) ** 2))) if len(cpost) else 0.0,
        })
    tail = []
    win = int(0.100 * rate)
    for start in range(0, len(cmd) - win + 1, win):
        seg = cmd[start:start + win].astype(float)
        tail.append(float(np.sqrt(np.mean(seg ** 2))))
    return {
        'finite': bool(np.isfinite(cmd).all()),
        'peak': float(np.max(np.abs(cmd))),
        'rms': float(np.sqrt(np.mean(cmd.astype(float) ** 2))),
        'max_abs_step': float(np.max(np.abs(np.diff(cmd, axis=0)))),
        'events': rows,
        'tail_rms_100ms': tail,
        'tail_rms_per_100ms': tail,
    }


def render_scene(lib: Path, name: str, block: int = 128) -> tuple[np.ndarray, list]:
    engine = Engine(lib)
    x, events = scene_input(name, block)
    y, applied = render(engine, x, SCENES[name]['params'], block, events)
    return y, applied
