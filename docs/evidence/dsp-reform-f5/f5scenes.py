"""F5 scene catalogue and measurement orchestration."""
from __future__ import annotations

import numpy as np

import f5bench as B

RATE = B.RATE
EVENT = 2.5  # absolute seconds; sources carry a 0.25 s lead


def _scene(name, body, source='bed', params=None, actions=None, mode='musical',
           nominal=None, metric_frame=None, reverse=False, tempo_source=0):
    return {'name': name, 'body': body, 'source': source,
            'params': params if params is not None else B.S(),
            'actions': actions or [], 'mode': mode, 'nominal': nominal,
            'metric_frame': metric_frame, 'reverse': reverse,
            'tempo_source': tempo_source}


def _bpm_step(from_bpm, to_bpm, body=7.0, **kw):
    params = B.S(p17=from_bpm, **kw)
    return _scene(f'manual_{from_bpm}_{to_bpm}', body, params=params,
                  actions=[(B.b128(EVENT), B.apply_params({17: to_bpm}))],
                  nominal=60.0 / to_bpm * RATE)


def _ms_step(from_ms, to_ms, body=7.0, **kw):
    params = B.S(p18=0, p12=from_ms, **kw)  # headsMusical = 0
    return _scene(f'ms_{int(from_ms)}_{int(to_ms)}', body, params=params,
                  actions=[(B.b128(EVENT), B.apply_params({12: to_ms}))],
                  mode='ms', nominal=to_ms * RATE / 1000.0)


def _ramp(name, from_bpm, to_bpm, seconds, steps=20, **kw):
    params = B.S(p17=from_bpm, **kw)
    actions = []
    for i in range(1, steps + 1):
        value = from_bpm + (to_bpm - from_bpm) * i / steps
        actions.append((B.b128(EVENT + seconds * i / steps),
                        B.apply_params({17: round(value, 4)})))
    return _scene(name, 9.0, params=params, actions=actions,
                  nominal=60.0 / to_bpm * RATE)


MANUAL = []
for _from, _to in [(120, 100), (100, 120), (120, 90), (90, 135), (80, 160), (160, 80)]:
    MANUAL.append(_bpm_step(_from, _to))
for _from, _to in [(500, 600), (600, 500), (500, 666.667), (666.667, 444.444),
                   (750, 375), (375, 750)]:
    MANUAL.append(_ms_step(_from, _to))
MANUAL.append(_ramp('ramp_120_100', 120, 100, 1.0))
MANUAL.append(_ramp('ramp_80_160', 80, 160, 2.0))

MODES = [
    _scene('mode_single_head', 7.0, source='clean_guitar', params=B.S(p16=1),
           actions=[(B.b128(EVENT), B.apply_params({17: 100}))],
           nominal=26460.0),
    _scene('mode_multi_head', 7.0, source='poly_synth', params=B.S(p16=7),
           actions=[(B.b128(EVENT), B.apply_params({17: 100}))],
           nominal=26460.0),
    _scene('mode_pingpong', 7.0, source='irregular_percussion',
           params=B.S(p16=4, p21=1),
           actions=[(B.b128(EVENT), B.apply_params({17: 100}))],
           nominal=26460.0),
    _scene('mode_freeze', 7.0, source='clean_guitar', params=B.S(p16=7, p12=300),
           actions=[(B.b128(2.0), B.apply_params({22: 1})),
                    (B.b128(3.0), B.apply_params({17: 100}))],
           nominal=26460.0, metric_frame=3.0),
    _scene('mode_fb95', 9.0, source='driven_guitar', params=B.S(p16=7, p13=95),
           actions=[(B.b128(EVENT), B.apply_params({17: 100}))],
           nominal=26460.0),
    _scene('mode_reverse_v2', 8.0, source='poly_synth',
           params=B.S(p16=4, p23=1, p36=0, p37=0),
           actions=[(B.b128(EVENT), B.apply_params({17: 90}))], reverse=True),
    _scene('control_static', 6.0, source='bed', params=B.S(p16=7), actions=[]),
    _scene('control_static_reverse', 6.0, source='poly_synth',
           params=B.S(p16=4, p23=1, p36=0), actions=[], reverse=True),
]


def _tap_scene(name, taps, nominal=None, metric_frame=None, body=8.0):
    actions = [(int(round(t * RATE)), B.tap_at(int(round(t * RATE))))
               for t in taps]
    return _scene(name, body, source='clean_guitar', params=B.S(),
                  actions=actions, nominal=nominal, metric_frame=metric_frame,
                  tempo_source=1)


def _tap_series(start, intervals):
    taps = [start]
    for interval in intervals:
        taps.append(taps[-1] + interval)
    return taps


TAPS = [
    _tap_scene('tap_regular_120', _tap_series(0.6, [0.5] * 9), 22050.0),
    _tap_scene('tap_regular_90', _tap_series(0.6, [60.0 / 90] * 9), 29400.0,
               metric_frame=2.5),
    _tap_scene('tap_accelerando', _tap_series(
        0.6, [0.60, 0.57, 0.54, 0.51, 0.48, 0.45, 0.42]), 18522.0),
    _tap_scene('tap_ritardando', _tap_series(
        0.6, [0.42, 0.45, 0.48, 0.51, 0.54, 0.57, 0.60]), 26460.0),
    _tap_scene('tap_outlier', _tap_series(0.6, [0.5, 0.5, 0.5, 0.9, 0.5, 0.5]),
               22050.0),
    _tap_scene('tap_two_taps_new', _tap_series(
        0.6, [0.5, 0.5, 0.5, 0.5, 0.5, 60.0 / 90, 60.0 / 90, 60.0 / 90]),
        29400.0, metric_frame=4.6),
    _tap_scene('tap_bounce', _tap_series(
        0.6, [0.5, 0.03, 0.5, 0.03, 0.5, 0.03, 0.5]), 22050.0),
    _tap_scene('tap_timeout', _tap_series(
        0.6, [0.5, 0.5, 0.5, 4.0, 60.0 / 90, 60.0 / 90, 60.0 / 90]),
        29400.0, metric_frame=8.35, body=11.0),
]


def _midi_scene(name, bpms, change_times, body=8.0, jitter=0.0, drop=0,
                nominal=None, metric_frame=None):
    events = B.midi_schedule(bpms, body + 0.75, change_times,
                             jitter_ms=jitter, drop_every=drop)
    actions = [(int(f), B.midi_at(s, int(f))) for f, s in events]
    if change_times and metric_frame is None:
        metric_frame = change_times[0][0] + 0.25
    return _scene(name, body, source='poly_synth', params=B.S(),
                  actions=actions, mode='midi', nominal=nominal,
                  metric_frame=metric_frame, tempo_source=2)


MIDI = [
    _midi_scene('midi_stable_120', [120.0], [], nominal=22050.0),
    _midi_scene('midi_120_125', [120.0, 125.0], [(3.0, 125.0)],
                nominal=21168.0),
    _midi_scene('midi_120_90', [120.0, 90.0], [(3.0, 90.0)],
                nominal=29400.0),
    _midi_scene('midi_jitter', [120.0], [], jitter=1.5, nominal=None),
    _midi_scene('midi_loss_1', [120.0], [], drop=97, nominal=22050.0),
    _midi_scene('midi_loss_2', [120.0], [], drop=61, nominal=22050.0),
]


def _midi_pulses(bpms, duration, change_times=(), offset=0.0):
    return [(int(f + offset * RATE), B.midi_at(s, int(f + offset * RATE)))
            for f, s in B.midi_schedule(bpms, duration, list(change_times))
            if s == 0xF8]


HOLDOVER = _scene(
    'midi_holdover_reacquire', 10.0, source='poly_synth', params=B.S(),
    actions=_midi_pulses([120.0], 4.0) +
            [(int(6.5 * RATE), B.midi_at(0xFA, int(6.5 * RATE)))] +
            _midi_pulses([125.0], 3.0, offset=6.5),
    mode='midi', nominal=21168.0, metric_frame=6.9, tempo_source=2)

TRANSPORT = _scene(
    'midi_start_stop_continue', 10.0, source='poly_synth', params=B.S(),
    actions=[(0, B.midi_at(0xFA, 0))] + _midi_pulses([120.0], 3.0) +
            [(int(3.2 * RATE), B.midi_at(0xFC, int(3.2 * RATE)))] +
            [(int(5.0 * RATE), B.midi_at(0xFB, int(5.0 * RATE)))] +
            _midi_pulses([120.0], 4.0, offset=5.0),
    mode='midi', nominal=None, metric_frame=5.2, tempo_source=2)

SOURCE_SWITCH = _scene(
    'source_switch_internal_midi', 9.0, source='poly_synth', params=B.S(),
    actions=[(B.b128(2.0), B.apply_params({17: 125})),
             (B.b128(2.0), lambda e, h: e.lib.hydra_dsp_set_tempo_source(h, 2)),
             ] + _midi_pulses([125.0], 6.0),
    mode='param', nominal=21168.0, metric_frame=2.5)

CLOCK = TAPS + MIDI + [HOLDOVER, TRANSPORT, SOURCE_SWITCH]

# Measurement window and nominal target per clock scene. For jitter/loss and
# changing tap cadences the engine target is authoritative; comparing to a
# fixed nominal would charge the clock estimator's bias to the glide.
TUNE = {
    'tap_accelerando': {'nominal': None, 'metric_frame': 4.45},
    'tap_ritardando': {'nominal': None, 'metric_frame': 4.60},
    'tap_bounce': {'nominal': None, 'metric_frame': 4.10},
    'tap_timeout': {'metric_frame': 8.35},
    'midi_jitter': {'nominal': None, 'metric_frame': 3.0},
    'midi_loss_1': {'nominal': 22050.0, 'metric_frame': 0.5},
    'midi_loss_2': {'nominal': 22050.0, 'metric_frame': 0.5},
    'midi_start_stop_continue': {'nominal': None, 'metric_frame': 5.2},
}
for _s in CLOCK:
    _s.update(TUNE.get(_s['name'], {}))

ALL_SCENES = MANUAL + MODES + CLOCK


def _clock_metrics(result, rows):
    if not rows:
        return
    last = rows[-1]
    result['clock_final'] = {
        'transport_bpm': last['bpm'],
        'tap_applied_bpm': last.get('tap_applied'),
        'tap_status': last.get('tap_status'),
        'tap_rejection': last.get('tap_rejection'),
        'midi_status': last.get('midi_status'),
        'midi_applied_bpm': last.get('midi_applied'),
        'midi_phase_error': last.get('midi_phase'),
        'midi_missed': last.get('midi_missed'),
    }
    # Any status/rejection values seen over the scene.
    result['clock_sequence'] = sorted({
        (r.get('midi_status'), r.get('tap_status'), r.get('tap_rejection'))
        for r in rows})


def run_scene(engine, scene, block=B.BLOCK, transient=False):
    x = B.source_full(scene['source'], scene['body'])
    params = scene['params']
    actions = list(scene['actions'])
    control, _ = B._render(engine, x, params, block, [], source=scene.get('tempo_source', 0))
    y, rows = B._render(engine, x, params, block, actions, source=scene.get('tempo_source', 0))
    if scene['metric_frame'] is not None:
        metric_seconds = scene['metric_frame']
    elif scene['mode'] in ('midi',) or scene['name'].startswith('tap_'):
        metric_seconds = scene['actions'][0][0] / RATE if scene['actions'] else EVENT
    else:
        metric_seconds = EVENT
    action_frame = int(round(metric_seconds * RATE))
    metrics = B.timing_metrics(rows, y, control, action_frame, action_frame,
                               nominal=scene.get('nominal'))
    if scene['mode'] == 'ms' and rows:
        post = [r for r in rows if r['frame'] >= action_frame]
        if post:
            head = np.array([r['smoothed_delay'] for r in post], dtype=float)
            target = np.array([r['target_delay'] for r in post], dtype=float)
            err = np.abs(head - target)
            frames = np.array([r['frame'] for r in post])

            def first(limit):
                idx = np.flatnonzero(err <= limit)
                return float((frames[idx[0]] - action_frame) / RATE) if len(idx) else None
            metrics.update({
                'convergence_to_1ms_s': first(44.1),
                'convergence_to_0p1ms_s': first(4.41),
                'convergence_to_1sample_s': first(1.0),
                'max_error_samples': float(np.max(err)),
                'max_error_ms': float(np.max(err) / RATE * 1000.0),
                'final_error_samples': float(err[-1]),
            })
    extra = {}
    if transient and scene['actions']:
        extra = B.transient_metrics(engine, x, params, actions, action_frame,
                                     source=scene.get('tempo_source', 0))
    if scene['reverse']:
        import ctypes as C
        h = engine.create(params, 0)
        requested, effective, clamped = C.c_float(), C.c_float(), C.c_int()
        engine.lib.hydra_dsp_get_reverse_diagnostics(
            h, C.byref(requested), C.byref(effective), C.byref(clamped), None)
        engine.lib.hydra_dsp_destroy(h)
        extra.update({'reverse_requested_ms': requested.value,
                      'reverse_effective_ms': effective.value,
                      'reverse_clamped': clamped.value})
    result = {'name': scene['name'], 'mode': scene['mode'],
              'source': scene['source'], 'body_s': scene['body'],
              'events': [{'frame': int(f), 'seconds': f / RATE}
                         for f, _ in actions],
              'params': {str(i): float(v) for i, v in enumerate(params)},
              'metrics': metrics, **extra}
    _clock_metrics(result, rows)
    return result, y, control, rows
