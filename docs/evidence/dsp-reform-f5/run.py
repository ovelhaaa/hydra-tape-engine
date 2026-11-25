"""F5 CLI: build, baseline, static preservation, measurements, drift, tails,
listening package and block-size contract.

Usage:
  python docs/evidence/dsp-reform-f5/run.py build
  python docs/evidence/dsp-reform-f5/run.py baseline
  python docs/evidence/dsp-reform-f5/run.py static
  python docs/evidence/dsp-reform-f5/run.py measure
  python docs/evidence/dsp-reform-f5/run.py blocksize
  python docs/evidence/dsp-reform-f5/run.py drift
  python docs/evidence/dsp-reform-f5/run.py tail
  python docs/evidence/dsp-reform-f5/run.py render
  python docs/evidence/dsp-reform-f5/run.py package
"""
from __future__ import annotations

import argparse
import hashlib
import json
import random
import shutil
import subprocess
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import f5bench as B  # noqa: E402
import f5scenes as S  # noqa: E402

ROOT = B.ROOT
OUT = B.OUT


def cmd_build():
    libs = {}
    for variant in (0, 1, 2):
        lib = B.build(variant)
        libs[str(variant)] = B.provenance(variant, lib)
    plain = B.build(0, hooks=False)
    libs['0_plain'] = B.provenance(0, plain, hooks=False)
    head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    branch = subprocess.check_output(['git', 'rev-parse', '--abbrev-ref', 'HEAD'],
                                     cwd=ROOT, text=True).strip()
    B.dump(OUT / 'build.json', {
        'head': head, 'branch': branch, 'libraries': libs,
        'python': sys.version.splitlines()[0], 'numpy': np.__version__,
        'base_params': B.BASE,
    })
    print('built', {k: v['library_sha256'][:12] for k, v in libs.items()})


def cmd_baseline():
    lib = B.build(0)
    engine = B.Engine(lib)
    f3 = json.loads((ROOT / 'docs/evidence/dsp-reform-f3/F3-A-manifest.json').read_text())
    f0 = json.loads((ROOT / 'docs/evidence/dsp-reform-f0/baseline_renders_manifest.json').read_text())
    rows, failures = {}, []
    for name, scene in f0['scenarios'].items():
        params = B.b.PARAMS.copy()
        for k, v in scene['params_overrides'].items():
            params[int(k)] = v
        x = B.b.stimulus(scene)
        y, _ = B.b.process(engine, x, params)
        expected = f3['scenarios'][name]
        row = {
            'input_sha256': B.sha(x.astype('<f4').tobytes()),
            'F5_0_output_sha256': B.sha(y.astype('<f4').tobytes()),
            'F3_A_output_sha256': expected['F3_A_output_sha256'],
            'input_exact': B.sha(x.astype('<f4').tobytes()) == expected['input_sha256'],
            'output_bit_exact': B.sha(y.astype('<f4').tobytes()) == expected['F3_A_output_sha256'],
            'finite': bool(np.isfinite(y).all()),
            'peak': float(np.max(np.abs(y))),
        }
        rows[name] = row
        if not (row['input_exact'] and row['output_bit_exact']):
            failures.append(name)
    result = {
        'reference': ('F3-A manifest source 2c15b88680ff65aff8bd5ac09543fab44b52c785 '
                      'compiled with Classic C=1/F2=2/F3=0'),
        'variant': 0, 'library_sha256': B.provenance(0, lib)['library_sha256'],
        'scenarios': rows, 'bit_exact_count': sum(v['output_bit_exact'] for v in rows.values()),
        'total': len(rows), 'failures': failures,
        'classification': 'BIT_EXACT_F3_A' if not failures else 'REFERENCE_MISMATCH',
    }
    B.dump(OUT / 'F5-A-reference.json', result)
    print(f"F5=0 vs F3-A: {result['bit_exact_count']}/{result['total']} bit-exact")
    if failures:
        raise SystemExit('reference mismatch: ' + ','.join(failures))


def cmd_static():
    """Candidates must not change a scene without tempo automation."""
    libs = {v: B.build(v) for v in (0, 1, 2)}
    engines = {v: B.Engine(libs[v]) for v in (0, 1, 2)}
    rows = {}
    f0 = json.loads((ROOT / 'docs/evidence/dsp-reform-f0/baseline_renders_manifest.json').read_text())
    for name, scene in f0['scenarios'].items():
        params = B.b.PARAMS.copy()
        for k, v in scene['params_overrides'].items():
            params[int(k)] = v
        x = B.b.stimulus(scene)
        y = {v: B.b.process(engines[v], x, params)[0] for v in (0, 1, 2)}
        rows[name] = {
            'v1_vs_v0': {'bit_exact': bool(np.array_equal(y[1], y[0])),
                         'max_abs': float(np.max(np.abs(y[1] - y[0])))},
            'v2_vs_v0': {'bit_exact': bool(np.array_equal(y[2], y[0])),
                         'max_abs': float(np.max(np.abs(y[2] - y[0])))},
        }
    # Static controls from the scene catalogue (live path, no commands).
    controls = {}
    for scene in S.ALL_SCENES:
        if scene['actions']:
            continue
        x = B.source_full(scene['source'], scene['body'])
        outs = {v: B._render(engines[v], x, scene['params'], B.BLOCK, [])[0] for v in (0, 1, 2)}
        controls[scene['name']] = {
            'v1_vs_v0': bool(np.array_equal(outs[1], outs[0])),
            'v2_vs_v0': bool(np.array_equal(outs[2], outs[0])),
        }
    result = {'f0_scenes': rows,
              'scene_controls': controls,
              'f0_v1_bit_exact': sum(r['v1_vs_v0']['bit_exact'] for r in rows.values()),
              'f0_v2_bit_exact': sum(r['v2_vs_v0']['bit_exact'] for r in rows.values()),
              'f0_total': len(rows)}
    B.dump(OUT / 'STATIC_PRESERVATION.json', result)
    print('F0.2 static: v1', result['f0_v1_bit_exact'], '/', result['f0_total'],
          'v2', result['f0_v2_bit_exact'], '/', result['f0_total'])
    print('scene controls:', controls)


def cmd_measure():
    libs = {v: B.build(v) for v in (0, 1, 2)}
    engines = {v: B.Engine(libs[v]) for v in (0, 1, 2)}
    transient_scenes = {
        'manual_120_100', 'manual_100_120', 'manual_120_90', 'manual_90_135',
        'manual_80_160', 'manual_160_80', 'ms_500_600', 'ms_600_500',
        'midi_120_125', 'midi_120_90', 'tap_regular_90', 'tap_two_taps_new',
        'tap_accelerando',
    }
    result = {'libraries': {str(v): B.provenance(v, libs[v]) for v in (0, 1, 2)},
              'base_params': B.BASE, 'scenes': {}}
    for scene in S.ALL_SCENES:
        name = scene['name']
        result['scenes'][name] = {'mode': scene['mode'],
                                  'source': scene['source'],
                                  'tempo_source': scene['tempo_source'],
                                  'nominal_samples': scene['nominal'],
                                  'variants': {}}
        for v in (0, 1, 2):
            run, y, control, rows = S.run_scene(
                engines[v], scene, transient=name in transient_scenes)
            result['scenes'][name]['variants'][str(v)] = {
                'metrics': run['metrics'],
                'clock_final': run.get('clock_final'),
                'clock_sequence': run.get('clock_sequence'),
                **{k: run[k] for k in ('max_read_velocity', 'peak_velocity_signed',
                                       'peak_pitch_cents_signed', 'peak_pitch_cents_abs',
                                       'reverse_requested_ms', 'reverse_effective_ms',
                                       'reverse_clamped') if k in run},
            }
        # Preservation of a candidate against A on the same audio, no commands.
        v0 = result['scenes'][name]['variants']['0']['metrics']
        for v in (1, 2):
            vn = result['scenes'][name]['variants'][str(v)]['metrics']
            result['scenes'][name]['variants'][str(v)]['delta_vs_A'] = {
                'convergence_to_1ms_s': vn.get('convergence_to_1ms_s'),
                'max_error_ms': vn.get('max_error_ms'),
                'peak_pitch_cents_abs': result['scenes'][name]['variants'][str(v)].get(
                    'peak_pitch_cents_abs'),
                'A_max_error_ms': v0.get('max_error_ms'),
            }
        print(name, 'done', flush=True)
    B.dump(OUT / 'TIMING_MEASUREMENTS.json', result)
    print('measurement written')


def cmd_measure_clock():
    """Refresh only the Tap/MIDI scenes after a measurement-window change."""
    libs = {v: B.build(v) for v in (0, 1, 2)}
    engines = {v: B.Engine(libs[v]) for v in (0, 1, 2)}
    path = OUT / 'TIMING_MEASUREMENTS.json'
    result = json.loads(path.read_text())
    transient_scenes = {'midi_120_125', 'midi_120_90', 'midi_jitter',
                        'tap_regular_90', 'tap_two_taps_new', 'tap_accelerando',
                        'tap_timeout'}
    for scene in S.CLOCK:
        name = scene['name']
        entry = result['scenes'].get(name, {'mode': scene['mode'],
                                             'source': scene['source'],
                                             'tempo_source': scene['tempo_source'],
                                             'nominal_samples': scene['nominal'],
                                             'variants': {}})
        entry.update({'mode': scene['mode'], 'source': scene['source'],
                      'tempo_source': scene['tempo_source'],
                      'nominal_samples': scene['nominal'], 'variants': {}})
        for v in (0, 1, 2):
            run, y, control, rows = S.run_scene(
                engines[v], scene, transient=name in transient_scenes)
            entry['variants'][str(v)] = {
                'metrics': run['metrics'],
                'clock_final': run.get('clock_final'),
                'clock_sequence': run.get('clock_sequence'),
                **{k: run[k] for k in ('max_read_velocity', 'peak_velocity_signed',
                                       'peak_pitch_cents_signed', 'peak_pitch_cents_abs')
                   if k in run},
            }
        result['scenes'][name] = entry
        print(name, 'refreshed', flush=True)
    B.dump(path, result)
    print('clock measurements refreshed')


def cmd_blocksize():
    libs = {v: B.build(v) for v in (0, 1, 2)}
    engines = {v: B.Engine(libs[v]) for v in (0, 1, 2)}
    frame = 303104  # lcm(1,37,64,128,256)
    scene = next(s for s in S.ALL_SCENES if s['name'] == 'manual_120_100')
    x = B.source_full('bed', 9.0)
    actions = [(frame, B.apply_params({17: 100}))]
    result = {'frame': frame, 'seconds': frame / B.RATE, 'variants': {}}
    for v in (0, 1, 2):
        outputs = {}
        for block in (37, 64, 128, 256):
            y, _ = B._render(engines[v], x, scene['params'], block, actions)
            outputs[block] = y
        ref = outputs[128]
        result['variants'][str(v)] = {}
        for block in (37, 64, 128, 256):
            d = outputs[block] - ref
            result['variants'][str(v)][str(block)] = {
                'max_abs_vs_128': float(np.max(np.abs(d))),
                'rmse_vs_128': float(np.sqrt(np.mean(d.astype(float) ** 2))),
                'sha256': B.sha(outputs[block].tobytes()),
            }
    B.dump(OUT / 'BLOCK_SIZE_CONTRACT.json', result)
    for v, rows in result['variants'].items():
        print('v' + v, {k: f"{x['max_abs_vs_128']:.3g}" for k, x in rows.items()})


def _drift_scene(name, params, changes, duration=60.0):
    """Long render; returns transport drift against the closed-form schedule."""
    libs = {v: B.build(v) for v in (0, 1, 2)}
    result = {'duration_s': duration, 'schedule': changes, 'variants': {}}
    x = np.zeros((int(duration * B.RATE), 2), '<f4')
    start = int(2.0 * B.RATE)
    t = np.arange(int(8.0 * B.RATE)) / B.RATE
    tone = (0.05 * np.sin(2 * np.pi * 110 * t))[:, None]
    x[start:start + len(t)] = np.repeat(tone, 2, axis=1)
    for v in (0, 1, 2):
        engine = B.Engine(B.build(v))
        actions = [(B.b128(sec), B.apply_params({17: bpm})) for sec, bpm in changes]
        y, rows = B._render(engine, x, params, B.BLOCK, actions)
        # Closed-form musical position from the *actual* block-edge commit
        # times, not the requested seconds.
        initial_bpm = float(params[17])
        schedule = [(0.0, initial_bpm)] + sorted(
            (frame / B.RATE, bpm) for frame, bpm in
            [(B.b128(sec), bpm) for sec, bpm in changes])
        expected = 0.0

        def position_at(sec):
            nonlocal expected
            pos = 0.0
            last_t, last_bpm = 0.0, initial_bpm
            for t_sec, bpm in schedule[1:]:
                if sec <= t_sec:
                    break
                pos += (t_sec - last_t) * last_bpm / 60.0
                last_t, last_bpm = t_sec, bpm
            pos += (sec - last_t) * last_bpm / 60.0
            return pos
        ref = rows[-1]
        err_beats = abs(ref['musical'] - position_at(ref['frame'] / B.RATE))
        head_err = abs(ref['head'][2] - ref['target_head'][2])
        head_bpm = 60000.0 / ref['target_beat_ms']
        result['variants'][str(v)] = {
            'finite': bool(np.isfinite(y).all()),
            'peak': float(np.max(np.abs(y))),
            'transport_position_error_beats': float(err_beats),
            'transport_bpm_final': ref['bpm'],
            'head_error_samples': float(head_err),
            'head_target_bpm': head_bpm,
            'tail_rms_last_second': float(np.sqrt(np.mean(
                y[-B.RATE:].astype(float) ** 2))),
        }
    B.dump(OUT / f'DRIFT_{name}.json', result)
    print(name, {v: {'pos_err': r['transport_position_error_beats'],
                     'head_err': r['head_error_samples']}
                 for v, r in result['variants'].items()})
    return result


def cmd_drift():
    _drift_scene('manual_60s', S.MANUAL[0]['params'],
                 [(10.0, 100.0), (30.0, 133.0), (50.0, 80.0)], duration=60.0)
    _drift_scene('manual_60s_second', S.MANUAL[1]['params'],
                 [(20.0, 100.0), (40.0, 120.0)], duration=60.0)


def cmd_tail():
    libs = {v: B.build(v) for v in (0, 1, 2)}
    cases = {
        'static_fb95': (B.S(p13=95, p16=7), []),
        'automation_fb95': (B.S(p13=95, p16=7),
                            [(20.0, {17: 100}), (25.0, {17: 90}), (35.0, {17: 120}),
                             (40.0, {17: 80})]),
    }
    result = {}
    for name, (params, events) in cases.items():
        x = np.zeros((60 * B.RATE, 2), '<f4')
        x[0, :] = 0.5
        if events:
            start, stop = 18 * B.RATE, 32 * B.RATE
            t = np.arange(stop - start) / B.RATE
            tone = (0.05 * np.sin(2 * np.pi * 110 * t))[:, None]
            x[start:stop] += np.repeat(tone, 2, axis=1).astype('<f4')
        actions = [(B.b128(sec), B.apply_params(change)) for sec, change in events]
        rows = {}
        for v in (0, 1, 2):
            y, _ = B._render(B.Engine(libs[v]), x, params, B.BLOCK, actions)
            sec = np.array([np.sqrt(np.mean(y[i * B.RATE:(i + 1) * B.RATE].astype(float) ** 2))
                            for i in range(60)])
            rows[f'variant{v}'] = {
                'finite': bool(np.isfinite(y).all()),
                'peak': float(np.max(np.abs(y))),
                'last_second_growth_db': float(20 * np.log10(
                    max(sec[-1], 1e-12) / max(sec[-2], 1e-12))),
                'last_5s_vs_previous_5s_db': float(20 * np.log10(
                    max(float(np.sqrt(np.mean(sec[-5:] ** 2))), 1e-12) /
                    max(float(np.sqrt(np.mean(sec[-10:-5] ** 2))), 1e-12))),
                'rms_per_second': sec.tolist(),
            }
        result[name] = rows
    B.dump(OUT / 'LONG_TAIL_CHECK.json', result)
    for name, rows in result.items():
        print(name, {v: round(r['last_second_growth_db'], 5) for v, r in rows.items()})


def cmd_reverse():
    libs = {v: B.build(v) for v in (0, 1, 2)}
    engines = {v: B.Engine(libs[v]) for v in (0, 1, 2)}
    beats = [1.0, 4.0, 2.0, 0.5, 0.25, 1.5, 0.75, 2.0 / 3.0, 1.0 / 3.0]
    result = {'variants': {}, 'beats_per_division': beats}
    x = B.source_full('poly_synth', 8.0)
    actions = [(B.b128(3.0), B.apply_params({17: 90}))]
    for v in (0, 1, 2):
        rows = []
        for division in range(9):
            params = B.S(p16=4, p23=1, p36=0, p37=division)
            y, states = B._render(engines[v], x, params, B.BLOCK, actions)
            control, _ = B._render(engines[v], x, params, B.BLOCK, [])
            after = [r for r in states if r['frame'] >= B.b128(3.0)]
            before = [r for r in states if r['frame'] < B.b128(3.0)]
            row = {
                'division': division,
                'requested_after_ms': after[-1]['reverse_requested'] if after else None,
                'effective_after_ms': after[-1]['reverse_effective'] if after else None,
                'clamped_after': after[-1]['reverse_clamped'] if after else None,
                'requested_before_ms': before[-1]['reverse_requested'] if before else None,
                'effective_before_ms': before[-1]['reverse_effective'] if before else None,
                'expected_after_ms': 60000.0 * beats[division] / 90.0,
                'expected_before_ms': 60000.0 * beats[division] / 120.0,
                'finite': bool(np.isfinite(y).all()),
                'max_step_vs_control_db': B._step_excess_db(y, control, B.b128(3.0)),
            }
            rows.append(row)
        result['variants'][str(v)] = rows
        print('reverse v', v, [(r['division'], round(r['effective_after_ms'] or 0, 2),
                               round(r['expected_after_ms'], 2)) for r in rows])
    B.dump(OUT / 'REVERSE_VALIDATION.json', result)


def cmd_equiv():
    """Instrumented and plain builds must render identical audio."""
    hooks = B.build(0)
    plain = B.build(0, hooks=False)
    e_hook, e_plain = B.Engine(hooks), B.Engine(plain, hooks=False)
    rows = {}
    for scene in S.ALL_SCENES:
        x = B.source_full(scene['source'], scene['body'])
        y1, _ = B._render(e_hook, x, scene['params'], B.BLOCK, scene['actions'])
        y2, _ = B._render(e_plain, x, scene['params'], B.BLOCK, scene['actions'])
        rows[scene['name']] = {
            'bit_exact': bool(np.array_equal(y1, y2)),
            'max_abs': float(np.max(np.abs(y1 - y2))),
        }
    result = {'decisions': rows,
              'bit_exact_count': sum(r['bit_exact'] for r in rows.values()),
              'total': len(rows)}
    B.dump(OUT / 'INSTRUMENTATION_EQUIVALENCE.json', result)
    print('hook equivalence:', result['bit_exact_count'], '/', result['total'])


def write_wav_24(path: Path, y: np.ndarray, rate: int = B.RATE):
    import wave
    q = np.round(np.clip(y.astype(np.float64), -1.0, 1.0) * 8388607.0).astype(np.int32)
    u = (q & 0xFFFFFF).astype(np.uint32)
    pcm = np.empty((len(q), 6), dtype=np.uint8)
    pcm[:, 0] = (u[:, 0] & 0xFF).astype(np.uint8)
    pcm[:, 1] = ((u[:, 0] >> 8) & 0xFF).astype(np.uint8)
    pcm[:, 2] = ((u[:, 0] >> 16) & 0xFF).astype(np.uint8)
    pcm[:, 3] = (u[:, 1] & 0xFF).astype(np.uint8)
    pcm[:, 4] = ((u[:, 1] >> 8) & 0xFF).astype(np.uint8)
    pcm[:, 5] = ((u[:, 1] >> 16) & 0xFF).astype(np.uint8)
    with wave.open(str(path), 'wb') as w:
        w.setparams((2, 3, rate, 0, 'NONE', 'not compressed'))
        w.writeframes(pcm.tobytes())


LISTEN = {
    '01_manual_120_100_clean': {
        'source': 'clean_guitar', 'params': B.S(p16=7, p13=65),
        'actions': [(B.b128(2.5), B.apply_params({17: 100}))],
        'description': 'BPM 120 -> 100 during a clean guitar phrase (abrupt manual change)',
        'events': [2.5],
    },
    '02_manual_80_160_driven': {
        'source': 'driven_guitar', 'params': B.S(p16=7, p13=75),
        'actions': [(B.b128(2.5), B.apply_params({17: 160}))],
        'description': 'BPM 80 -> 160 big upward jump, saturated guitar',
        'events': [2.5],
    },
    '03_ramp_120_100_poly': {
        'source': 'poly_synth', 'params': B.S(p16=7, p13=60),
        'actions': [(B.b128(2.5 + 0.25 * i), B.apply_params(
            {17: round(120 - 20 * i / 8, 3)})) for i in range(1, 9)],
        'description': 'Gradual BPM 120 -> 100 ramp over 0.25 s steps on a poly pad',
        'events': [2.5, 2.75, 3.0, 3.25, 3.5, 3.75, 4.0, 4.25],
    },
    '04_tap_120_90_clean': {
        'source': 'clean_guitar', 'params': B.S(p16=7, p13=65),
        'actions': [(int(round(t * B.RATE)), B.tap_at(int(round(t * B.RATE))))
                    for t in S._tap_series(0.6, [0.5] * 5 + [60.0 / 90] * 4)],
        'description': 'Tap 120 -> 90 with six taps at each cadence',
        'events': [0.6, 1.1, 1.6, 2.1, 2.6, 3.1, 3.7667, 4.4333, 5.1, 5.7667],
        'tempo_source': 1,
    },
    '05_midi_120_125_poly': {
        'source': 'poly_synth', 'params': B.S(p16=7, p13=60),
        'actions': [(int(f), B.midi_at(s, int(f)))
                    for f, s in B.midi_schedule([120.0, 125.0], 8.75, [(3.0, 125.0)])],
        'description': 'MIDI clock 120 -> 125 on a sustained poly synth',
        'events': [3.0],
        'tempo_source': 2,
    },
    '06_midi_lost_recovered_bass': {
        'source': 'electric_bass_proxy', 'params': B.S(p16=7, p13=70),
        'actions': S.HOLDOVER['actions'],
        'description': 'MIDI clock lost for 2.5 s and reacquired at a new tempo',
        'events': [6.5],
        'tempo_source': 2,
        'body': 10.0,
    },
    '07_division_quarter_to_eighth_reverse': {
        'source': 'poly_synth', 'params': B.S(p16=4, p23=1, p36=0, p37=0),
        'actions': [(B.b128(2.5), B.apply_params({37: 3}))],
        'description': 'Reverse V2 quarter -> eighth subdivision',
        'events': [2.5],
    },
    '08_multihead_sync_120_100': {
        'source': 'poly_synth', 'params': B.S(p16=7, p13=60),
        'actions': [(B.b128(2.5), B.apply_params({17: 100}))],
        'description': 'Multi-head musical mode, BPM 120 -> 100',
        'events': [2.5],
    },
    '09_pad_reverse_90': {
        'source': 'poly_synth', 'params': B.S(p16=4, p23=1, p36=0, p13=65),
        'actions': [(B.b128(2.5), B.apply_params({17: 90}))],
        'description': 'Reverse V2 during a BPM 120 -> 90 change',
        'events': [2.5],
    },
    '10_drums_120_90': {
        'source': 'irregular_percussion', 'params': B.S(p16=4, p13=55),
        'actions': [(B.b128(2.5), B.apply_params({17: 90}))],
        'description': 'Irregular percussion, BPM 120 -> 90',
        'events': [2.5],
    },
    '11_control_static_guitar': {
        'source': 'clean_guitar', 'params': B.S(p16=7, p13=65),
        'actions': [],
        'description': 'Negative control: no tempo command at all',
        'events': [],
    },
}


def cmd_render():
    libs = {v: B.build(v) for v in (0, 1, 2)}
    summary = {}
    for v in (0, 1, 2):
        dest = OUT / f'variant{v}'
        dest.mkdir(parents=True, exist_ok=True)
        rows = []
        for name, spec in LISTEN.items():
            x = B.source_full(spec['source'], spec.get('body', 8.0))
            y, _ = B._render(B.Engine(libs[v]), x, spec['params'], B.BLOCK, spec['actions'],
                             source=spec.get('tempo_source', 0))
            (dest / f'{name}.f32').write_bytes(y.astype('<f4').tobytes())
            rows.append({
                'scene': name, 'source': spec['source'],
                'description': spec['description'],
                'sample_rate': B.RATE, 'block_size': B.BLOCK,
                'duration_sec': len(x) / B.RATE,
                'events': spec['events'],
                'tempo_source': spec.get('tempo_source', 0),
                'params': {str(k): float(val) for k, val in enumerate(spec['params'])},
                'input_sha256': B.sha(x.tobytes()),
                'output_sha256': B.sha(y.astype('<f4').tobytes()),
                'peak': float(np.max(np.abs(y))),
                'rms': float(np.sqrt(np.mean(y.astype(float) ** 2))),
                'finite': bool(np.isfinite(y).all()),
            })
        B.dump(dest / 'renders.json', {
            'variant': v, 'flags': B.FLAGS[v],
            'library_sha256': B.sha(libs[v].read_bytes()), 'scenes': rows})
        summary[str(v)] = {'library_sha256': B.sha(libs[v].read_bytes()),
                           'scenes': len(rows)}
    B.dump(OUT / 'render-summary.json', summary)
    print('rendered', summary)


def cmd_package():
    if not (OUT / 'build.json').exists():
        cmd_build()
    listen = OUT / 'listening'
    if listen.exists():
        shutil.rmtree(listen)
    listen.mkdir(parents=True)
    shutil.copytree(ROOT / 'tools/listening-lab', listen / 'lab')
    rows = {v: json.loads((OUT / f'variant{v}/renders.json').read_text()) for v in (0, 1, 2)}
    assert all([r['scene'] for r in rows[v]['scenes']] == [r['scene'] for r in rows[0]['scenes']]
               for v in (1, 2))
    build = json.loads((OUT / 'build.json').read_text())
    rng = random.Random(B.SEED)
    manifest = {
        'seed': B.SEED,
        'method': ('A/B/C blind listening. RAW keeps render gain; MATCHED applies one constant '
                   'gain per file so all three variants share the lower RMS in 0.5-8.0 s, with a '
                   'common headroom trim to 0.98 FS. Deterministic synthetic sources.'),
        'scenes': [],
        'provenance': {
            'source_commit': build['head'], 'branch': build['branch'],
            'flags': {'A': B.FLAGS[0], 'B': B.FLAGS[1], 'C': B.FLAGS[2]},
            'libraries': {['A', 'B', 'C'][v]: rows[v]['library_sha256'] for v in (0, 1, 2)},
            'block_size': B.BLOCK, 'rate': B.RATE,
            'sample_format': 'PCM24 round-to-nearest(clip(float32,-1,1)*8388607), no dither',
            'randomization': 'Python random.Random(20261124), one fixed X/Y/Z permutation per scene',
            'commands': 'parameter/tap/MIDI commits at documented frames, identical in A/B/C',
        },
    }
    key = {}
    for base in rows[0]['scenes']:
        name = base['scene']
        y = [np.fromfile(OUT / f'variant{v}/{name}.f32', '<f4').reshape(-1, 2) for v in (0, 1, 2)]
        active = slice(int(0.5 * B.RATE), int(8.0 * B.RATE))
        rms = [float(np.sqrt(np.mean(yv[active].astype(float) ** 2))) for yv in y]
        gain = [min(rms) / max(r, 1e-30) for r in rms]
        trim = min(1.0, 0.98 / max(float(np.max(np.abs(yv))) * g for yv, g in zip(y, gain)))
        order = rng.sample([0, 1, 2], 3)
        scene = {'scene': name, 'source': base['source'],
                 'description': base['description'], 'sample_rate': B.RATE,
                 'block_size': B.BLOCK, 'duration_sec': base['duration_sec'],
                 'events': base['events'], 'slots': []}
        key[name] = {}
        for j, variant in enumerate(order):
            code = ['X', 'Y', 'Z'][j]
            slot = {'code': code, 'files': {}}
            for fmt, yy in [('RAW', y[variant]),
                            ('MATCHED', (y[variant].astype(float) * gain[variant] * trim)
                             .astype(np.float32))]:
                filename = f'{name}-{code}-{fmt}.wav'
                write_wav_24(listen / filename, yy)
                slot['files'][fmt] = {'path': filename,
                                      'sha256': B.sha((listen / filename).read_bytes())}
            scene['slots'].append(slot)
            key[name][code] = ['Vintage F3-A (F5=0)', 'Capstan Glide (F5=1)',
                               'Clock-Focused (F5=2)'][variant]
        scene['matched_gain'] = {['X', 'Y', 'Z'][j]: float(gain[order[j]] * trim)
                                 for j in range(3)}
        manifest['scenes'].append(scene)
    B.dump(listen / 'manifest.json', manifest)
    B.dump(listen / 'answer-key.json', key)
    shutil.copy(HERE / 'listening.html', listen / 'index.html')
    shutil.copy(HERE / 'listening.mjs', listen / 'listening.mjs')
    shutil.copy(HERE / 'LISTENING_GUIDE.md', listen / 'LEIA-ME.md')
    hashes = {p.relative_to(listen).as_posix(): B.sha(p.read_bytes())
              for p in sorted(listen.rglob('*')) if p.is_file() and p.name != 'sha256.json'}
    B.dump(listen / 'sha256.json', hashes)
    for rel, digest in hashes.items():
        assert B.sha((listen / rel).read_bytes()) == digest
    B.dump(HERE / 'render-manifest.json', {
        'package': 'artifacts/dsp-reform-f5/listening',
        'scene_count': len(manifest['scenes']), 'files': len(hashes),
        'manifest_sha256': B.sha((listen / 'manifest.json').read_bytes()),
        'answer_key_sha256': B.sha((listen / 'answer-key.json').read_bytes()),
        'sha256': hashes, 'provenance': manifest['provenance'],
        'publication': 'PENDING_CI_ARTIFACT',
    })
    B.dump(OUT / 'audition-package.json', {
        'scene_count': len(manifest['scenes']), 'wav_files': len(hashes) - 2,
        'manifest_sha256': B.sha((listen / 'manifest.json').read_bytes()),
        'answer_key_sha256': B.sha((listen / 'answer-key.json').read_bytes()),
        'publication': 'PENDING_CI_ARTIFACT',
    })
    print(f'listening package: {len(manifest["scenes"])} scenes, {len(hashes)} files')


ACTIONS = {'build': cmd_build, 'baseline': cmd_baseline, 'static': cmd_static,
           'measure': cmd_measure, 'measure-clock': cmd_measure_clock,
           'blocksize': cmd_blocksize, 'drift': cmd_drift,
           'tail': cmd_tail, 'equiv': cmd_equiv, 'reverse': cmd_reverse, 'render': cmd_render,
           'package': cmd_package}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=sorted(ACTIONS))
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    ACTIONS[args.action]()


if __name__ == '__main__':
    main()
