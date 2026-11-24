"""F4 CLI: build, freeze reference, measure, render, package, contracts."""
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

import numpy as np

import f4bench as B

HERE = B.HERE
ROOT = B.ROOT
OUT = B.OUT


def provenance(variant: int, lib: Path) -> dict:
    return {
        'variant': variant,
        'flags': B.FLAGS[variant],
        'library': lib.relative_to(ROOT).as_posix(),
        'library_sha256': B.sha(lib.read_bytes()),
        'library_bytes': lib.stat().st_size,
        'compiler': subprocess.check_output(['g++', '--version'], text=True).splitlines()[0],
        'source_sha256': {
            name: B.sha((ROOT / name).read_bytes()) for name in [
                'dsp/core/tape_core.cpp', 'dsp/core/tape_core.hpp',
                'dsp/core/hydra_dsp_c_api.cpp', 'dsp/core/fast_tanh_table.inc',
                'dsp/core/performance_transitions_experiment.hpp',
                'include/hydra_dsp.h']},
    }


def cmd_build() -> None:
    libs = {}
    for variant in (0, 1):
        lib = B.build(variant)
        libs[str(variant)] = provenance(variant, lib)
    head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    branch = subprocess.check_output(['git', 'rev-parse', '--abbrev-ref', 'HEAD'], cwd=ROOT, text=True).strip()
    B.dump(OUT / 'build.json', {'head': head, 'branch': branch, 'libraries': libs,
                                'python': sys.version.splitlines()[0],
                                'numpy': np.__version__})
    print('built', {k: v['library_sha256'][:12] for k, v in libs.items()})


def cmd_baseline() -> None:
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
        y, effective = B.b.process(engine, x, params)
        expected = f3['scenarios'][name]
        row = {
            'input_sha256': B.sha(x.astype('<f4').tobytes()),
            'F4_0_output_sha256': B.sha(y.astype('<f4').tobytes()),
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
        'reference': 'F3-A manifest source 2c15b88680ff65aff8bd5ac09543fab44b52c785 compiled with Classic C=1/F2=2/F3=0',
        'variant': 0,
        'library_sha256': provenance(0, lib)['library_sha256'],
        'scenarios': rows,
        'bit_exact_count': sum(v['output_bit_exact'] for v in rows.values()),
        'total': len(rows),
        'failures': failures,
        'classification': 'BIT_EXACT_F3_A' if not failures else 'REFERENCE_MISMATCH',
    }
    B.dump(OUT / 'F4-A-reference.json', result)
    print(f"F4=0 vs F3-A: {result['bit_exact_count']}/{result['total']} bit-exact")
    if failures:
        raise SystemExit('reference mismatch: ' + ','.join(failures))


def _control(name: str, lib: Path, block: int = 128) -> np.ndarray:
    engine = B.Engine(lib)
    scene = B.SCENES[name]
    x, _ = B.scene_input(name, block)
    y, _ = B.render(engine, x, scene['params'], block, [])
    return y


def cmd_probe() -> None:
    libs = {v: B.build(v) for v in (0, 1)}
    rows = {}
    print(f"{'scene':26s} {'t':>6s} {'v0 bnd':>9s} {'v1 bnd':>9s} {'v0 1ms':>9s} {'v1 1ms':>9s}")
    for name in B.SCENES:
        if name.startswith('control'):
            continue
        scene = B.SCENES[name]
        x, events = B.scene_input(name)
        metrics = {}
        for v in (0, 1):
            engine = B.Engine(libs[v])
            ctl = _control(name, libs[v])
            y, applied = B.render(engine, x, scene['params'], 128, events)
            metrics[v] = B.scene_metrics(y, ctl, [(e['frame'], e['params']) for e in applied])
        rows[name] = {'v0': metrics[0], 'v1': metrics[1]}
        for i, ev in enumerate(metrics[0]['events']):
            ev1 = metrics[1]['events'][i]
            print(f"{name:26s} {ev['seconds']:6.2f} {ev['step']['boundary_step']:9.5f} "
                  f"{ev1['step']['boundary_step']:9.5f} {ev['step']['post_max_step_1ms']:9.5f} "
                  f"{ev1['step']['post_max_step_1ms']:9.5f}")
    B.dump(OUT / 'probe.json', rows)


def cmd_measure() -> None:
    libs = {v: B.build(v) for v in (0, 1)}
    controls = {}
    for name in B.SCENES:
        scene = B.SCENES[name]
        x, _ = B.scene_input(name)
        controls[name] = {}
        for v in (0, 1):
            y, _ = B.render(B.Engine(libs[v]), x, scene['params'], 128, [])
            controls[name][v] = y
    result = {'variant_0': provenance(0, libs[0]), 'variant_1': provenance(1, libs[1]),
              'base_params': B.BASE, 'scenes': {}}
    # Static preservation: identical parameters, no automation, fresh lifecycle.
    f0 = json.loads((ROOT / 'docs/evidence/dsp-reform-f0/baseline_renders_manifest.json').read_text())
    static_rows = {}
    e0, e1 = B.Engine(libs[0]), B.Engine(libs[1])
    for name, scene in f0['scenarios'].items():
        params = B.b.PARAMS.copy()
        for k, v in scene['params_overrides'].items():
            params[int(k)] = v
        x = B.b.stimulus(scene)
        y0, _ = B.b.process(e0, x, params)
        y1, _ = B.b.process(e1, x, params)
        static_rows[name] = {'bit_exact': bool(np.array_equal(y0, y1)),
                             'max_abs': float(np.max(np.abs(y0 - y1))),
                             'finite': bool(np.isfinite(y1).all())}
    result['static_preservation_f0_scenarios'] = static_rows

    reduced = {'head': 0, 'head_total': 0, 'mix': 0, 'mix_total': 0,
               'freeze_arrival': 0, 'freeze_arrival_total': 0,
               'drive_arrival': 0, 'drive_arrival_total': 0}
    delayed = {}
    for name, scene in B.SCENES.items():
        x, events = B.scene_input(name)
        applied = [(B.b128(s, 128), p) for s, p in scene['events']]
        scene_rows = {'duration_sec': scene['duration'], 'source': scene.get('source', 'bed'),
                      'events_requested': scene['events'], 'events_frame': applied,
                      'params': scene['params']}
        renders = {}
        for v in (0, 1):
            y, applied_v = B.render(B.Engine(libs[v]), x, scene['params'], 128,
                                    [(B.b128(s, 128), p) for s, p in scene['events']])
            renders[v] = y
            scene_rows[f'variant{v}'] = B.scene_metrics(y, controls[name][v],
                                                        [(e['frame'], e['params']) for e in applied_v])
        for i in range(len(applied)):
            a = scene_rows['variant0']['events'][i]
            c = scene_rows['variant1']['events'][i]
            scene_rows.setdefault('comparison', []).append({
                'frame': a['frame'],
                'boundary_step_v0': a['step']['boundary_step'],
                'boundary_step_v1': c['step']['boundary_step'],
                'step_1ms_v0': a['step']['post_max_step_1ms'],
                'step_1ms_v1': c['step']['post_max_step_1ms'],
                'pre_max_step_50ms_v0': a['step']['pre_max_step_50ms'],
                'step_excess_db_v0': a['step_excess_db'],
                'step_excess_db_v1': c['step_excess_db'],
                'hf_excess_db_v0': a['hf_excess_db'],
                'hf_excess_db_v1': c['hf_excess_db'],
                'boundary_lower_in_v1': c['step']['boundary_step'] <= a['step']['boundary_step'],
                'one_ms_lower_in_v1': c['step']['post_max_step_1ms'] <= a['step']['post_max_step_1ms'],
            })
            if name.startswith('head') or name.startswith('reverse_head'):
                reduced['head_total'] += 1
                if c['step']['boundary_step'] < a['step']['boundary_step']:
                    reduced['head'] += 1
            if name.startswith('mix') or name == 'multihead_mix':
                reduced['mix_total'] += 1
                if c['step']['boundary_step'] < a['step']['boundary_step']:
                    reduced['mix'] += 1

        # Drive and freeze act on the magnetic write, so their audible edge is
        # one delay time later, when the written samples reach a read head.
        is_freezer = name.startswith('freeze') and any(float(p.get(22, 1)) == 0 for _, p in scene['events'])
        if name.startswith('drive') or is_freezer:
            offset = int(round(scene['params'][12] * 44100 / 1000))
            arrivals = []
            for ev, params in applied:
                if name.startswith('drive') or float(params.get(22, 1)) == 0:
                    arrivals.append(ev + offset)
            arrival_rows = {}
            for v in (0, 1):
                arrival_rows[f'variant{v}'] = []
                for arr in arrivals:
                    a = B.step_metrics(renders[v], arr)
                    c = B.step_metrics(controls[name][v], arr)
                    hf_y = B.high_freq_ratio(renders[v], arr, min(len(renders[v]), arr + 441))
                    hf_c = B.high_freq_ratio(controls[name][v], arr, min(len(controls[name][v]), arr + 441))
                    arrival_rows[f'variant{v}'].append({
                        'frame': arr, 'seconds': arr / 44100,
                        'boundary_step': a['boundary_step'],
                        'step_1ms': a['post_max_step_1ms'],
                        'control_boundary_step': c['boundary_step'],
                        'control_step_1ms': c['post_max_step_1ms'],
                        'pre_max_step_50ms': a['pre_max_step_50ms'],
                        'hf_excess_db': float(20 * np.log10(max(hf_y, 1e-12) / max(hf_c, 1e-12))),
                    })
            delayed[name] = {'offset_ms': scene['params'][12],
                             'arrivals': arrival_rows}
            for i in range(len(arrivals)):
                a = arrival_rows['variant0'][i]
                c = arrival_rows['variant1'][i]
                key = 'freeze_arrival' if is_freezer else 'drive_arrival'
                reduced[key + '_total'] += 1
                if c['step_1ms'] < a['step_1ms']:
                    reduced[key] += 1
        result['scenes'][name] = scene_rows
    result['delayed_arrivals'] = delayed
    result['static_controls'] = {
        name: {'bit_exact': bool(np.array_equal(controls[name][0], controls[name][1])),
               'max_abs': float(np.max(np.abs(controls[name][0] - controls[name][1])))}
        for name in controls if name.startswith('control')}
    result['reduced_counts'] = reduced
    result['events_with_lower_step_in_v1'] = reduced['head'] + reduced['mix']

    # Mix is independent of the classic feedback source: when the monitor ramp
    # and the playback-filter memory have settled, F4 must recover F4=0 exactly.
    mix_rows = {}
    for name in ['mix_0_100_0', 'mix_100_0', 'mix_25_75', 'multihead_mix']:
        scene = B.SCENES[name]
        x, events = B.scene_input(name)
        y0, _ = B.render(B.Engine(libs[0]), x, scene['params'], 128, events)
        y1, _ = B.render(B.Engine(libs[1]), x, scene['params'], 128, events)
        last = max((e for e, _ in events), default=0)
        settle = last + int(0.100 * 44100)
        diff = y1[settle:] - y0[settle:]
        mix_rows[name] = {
            'settle_frame': settle,
            'max_abs_after_settle': float(np.max(np.abs(diff))) if len(diff) else 0.0,
            'bit_exact_after_settle': bool(np.array_equal(y1[settle:], y0[settle:])),
            'max_abs_before_settle': float(np.max(np.abs(y1[:settle] - y0[:settle]))),
        }
    result['mix_final_value'] = mix_rows
    B.dump(OUT / 'AUTOMATION_MEASUREMENTS.json', result)
    print('head boundary reductions:', reduced['head'], '/', reduced['head_total'])
    print('mix boundary reductions:', reduced['mix'], '/', reduced['mix_total'])
    print('freeze arrival improvements:', reduced['freeze_arrival'], '/', reduced['freeze_arrival_total'])
    print('drive arrival improvements:', reduced['drive_arrival'], '/', reduced['drive_arrival_total'])
    print('mix final value exact:', {k: v['bit_exact_after_settle'] for k, v in mix_rows.items()})


LIST_BASE = B.b.PARAMS.copy()
for _id, _value in {11: 1, 12: 500, 13: 65, 14: 50, 15: 50, 16: 4, 17: 120,
                    18: 0, 19: 0, 20: 50, 21: 0, 22: 0, 23: 0, 24: 0, 25: 0,
                    30: 0, 36: 0, 37: 0}.items():
    LIST_BASE[_id] = _value


def LP(**kw):
    p = LIST_BASE.copy()
    for k, v in kw.items():
        p[int(k[1:])] = v
    return p


LISTEN = {
    '01_head_3_to_123': {
        'source': 'clean_guitar', 'params': LP(p16=4, p13=65),
        'events': [(2.5, {16: 7})],
        'description': 'Head 3 -> heads 1+2+3 during a sustained clean guitar phrase'},
    '02_head_rapid': {
        'source': 'driven_guitar', 'params': LP(p16=4, p13=70),
        'events': [(2.0, {16: 7}), (2.15, {16: 1}), (2.30, {16: 6}), (2.45, {16: 3})],
        'description': 'Four head selections 150 ms apart, including retargeting mid-fade'},
    '03_head_1_to_3': {
        'source': 'poly_synth', 'params': LP(p16=1, p13=65, p12=420),
        'events': [(2.5, {16: 4})],
        'description': 'Single head 1 -> head 3 on a sustained polyphonic pad'},
    '04_head_12_to_23': {
        'source': 'irregular_percussion', 'params': LP(p16=3, p13=60, p12=360),
        'events': [(2.5, {16: 6})],
        'description': 'Heads 1+2 -> heads 2+3 with percussive material'},
    '05_freeze_on_off': {
        'source': 'clean_guitar', 'params': LP(p16=7, p12=300, p13=70),
        'events': [(2.5, {22: 1}), (4.5, {22: 0})],
        'description': 'Freeze on/off while the delay holds a guitar phrase'},
    '06_freeze_high_fb': {
        'source': 'driven_guitar', 'params': LP(p16=7, p12=300, p13=95),
        'events': [(2.5, {22: 1}), (4.5, {22: 0})],
        'description': 'Freeze with 95% feedback; release is the exposed edge'},
    '07_freeze_reverse': {
        'source': 'poly_synth', 'params': LP(p16=7, p12=300, p13=70, p23=1, p36=0),
        'events': [(2.5, {22: 1}), (4.5, {22: 0})],
        'description': 'Freeze with Reverse and multi-head active'},
    '08_mix_0_100_0': {
        'source': 'electric_bass_proxy', 'params': LP(p14=0, p15=0, p13=55),
        'events': [(2.5, {14: 100, 15: 100}), (4.5, {14: 0, 15: 0})],
        'description': 'Mix 0 -> 100 -> 0 (host p_mix drives dryWet and delayWet)'},
    '09_drive_0_100_25': {
        'source': 'driven_guitar', 'params': LP(p3=0, p13=65),
        'events': [(2.5, {3: 100}), (4.5, {3: 25})],
        'description': 'Drive 0 -> 100 -> 25 with a stored tape tail'},
    '10_tone_0_100_0': {
        'source': 'clean_guitar', 'params': LP(p13=85, p20=0),
        'events': [(2.5, {20: 100}), (4.5, {20: 0})],
        'description': 'Tone 0 -> 100 -> 0 at high feedback (existing pbTransition)'},
    '11_multihead_mix': {
        'source': 'poly_synth', 'params': LP(p16=7, p14=0, p15=0, p13=60, p12=420),
        'events': [(2.5, {14: 100, 15: 100})],
        'description': 'Multi-head with Mix 0 -> 100'},
    '12_reverse_head_switch': {
        'source': 'poly_synth', 'params': LP(p16=4, p23=1, p36=0, p12=360, p13=65),
        'events': [(2.5, {16: 7}), (4.5, {16: 1})],
        'description': 'Head selection while Reverse is active (mask is bypassed by design)'},
    '13_control_no_automation': {
        'source': 'clean_guitar', 'params': LP(p16=7, p13=65),
        'events': [],
        'description': 'Negative control: identical audio, no automation'},
}


def write_wav_24(path: Path, y: np.ndarray, rate: int = 44100) -> None:
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


def cmd_render() -> None:
    libs = {v: B.build(v) for v in (0, 1)}
    summary = {}
    for v in (0, 1):
        dest = OUT / f'variant{v}'
        dest.mkdir(parents=True, exist_ok=True)
        rows = []
        for name, scene in LISTEN.items():
            x = B.source_signal(scene['source'], 10.0, lead=0.5, tail=0.8)
            events = [(B.b128(sec, 128), p) for sec, p in scene['events']]
            y, applied = B.render(B.Engine(libs[v]), x, scene['params'], 128, events)
            (dest / f'{name}.f32').write_bytes(y.astype('<f4').tobytes())
            rows.append({
                'scene': name, 'source': scene['source'], 'description': scene['description'],
                'sample_rate': 44100, 'block_size': 128, 'duration_sec': len(x) / 44100,
                'params': {str(k): float(val) for k, val in enumerate(scene['params'])},
                'input_sha256': B.sha(x.tobytes()),
                'output_sha256': B.sha(y.astype('<f4').tobytes()),
                'events': applied, 'peak': float(np.max(np.abs(y))),
                'rms': float(np.sqrt(np.mean(y.astype(float) ** 2))),
                'finite': bool(np.isfinite(y).all()),
            })
        B.dump(dest / 'renders.json', {'variant': v, 'flags': B.FLAGS[v],
                                       'library_sha256': B.sha(libs[v].read_bytes()),
                                       'scenes': rows})
        summary[str(v)] = {'library_sha256': B.sha(libs[v].read_bytes()), 'scenes': len(rows)}
    B.dump(OUT / 'render-summary.json', summary)
    print('rendered', summary)


def cmd_package() -> None:
    import random
    if not (OUT / 'build.json').exists():
        cmd_build()
    listen = OUT / 'listening'
    if listen.exists():
        shutil.rmtree(listen)
    listen.mkdir(parents=True)
    shutil.copytree(ROOT / 'tools/listening-lab', listen / 'lab')
    rows = {v: json.loads((OUT / f'variant{v}/renders.json').read_text()) for v in (0, 1)}
    assert [r['scene'] for r in rows[0]['scenes']] == [r['scene'] for r in rows[1]['scenes']]
    build = json.loads((OUT / 'build.json').read_text())
    rng = random.Random(20261124)
    manifest = {
        'seed': 20261124,
        'method': ('A/B blind listening. RAW keeps render gain; MATCHED applies one constant gain '
                   'per file so both variants share the lower RMS in 0.5-8.5 s, with a common '
                   'headroom trim to 0.98 FS. All stimuli are deterministic synthetic sources.'),
        'scenes': [],
        'provenance': {
            'source_commit': build['head'],
            'branch': build['branch'],
            'flags': {'A': B.FLAGS[0], 'B': B.FLAGS[1]},
            'libraries': {'A': rows[0]['library_sha256'], 'B': rows[1]['library_sha256']},
            'block_size': 128, 'rate': 44100,
            'sample_format': 'PCM24 round-to-even(clip(float32,-1,1)*8388607), no dither',
            'randomization': 'Python random.Random(20261124), one fixed X/Y allocation per scene',
            'commands': 'parameter commits at the documented frames, identical in A and B',
        },
    }
    key = {}
    for i, base in enumerate(rows[0]['scenes']):
        name = base['scene']
        y = [np.fromfile(OUT / f'variant{v}/{name}.f32', '<f4').reshape(-1, 2) for v in (0, 1)]
        active = slice(int(0.5 * 44100), int(8.5 * 44100))
        rms = [float(np.sqrt(np.mean(yv[active].astype(float) ** 2))) for yv in y]
        gains = [min(rms) / max(r, 1e-30) for r in rms]
        trim = min(1.0, 0.98 / max(float(np.max(np.abs(yv))) * g for yv, g in zip(y, gains)))
        order = rng.sample([0, 1], 2)
        scene = {
            'scene': name, 'source': base['source'], 'description': base['description'],
            'sample_rate': 44100, 'block_size': 128, 'duration_sec': base['duration_sec'],
            'events': base['events'], 'slots': [],
        }
        key[name] = {}
        for j, variant in enumerate(order):
            code = ['X', 'Y'][j]
            slot = {'code': code, 'files': {}}
            for fmt, yy in [('RAW', y[variant]),
                            ('MATCHED', (y[variant].astype(float) * gains[variant] * trim).astype(np.float32))]:
                filename = f'{name}-{code}-{fmt}.wav'
                write_wav_24(listen / filename, yy)
                slot['files'][fmt] = {'path': filename, 'sha256': B.sha((listen / filename).read_bytes())}
            scene['slots'].append(slot)
            key[name][code] = 'Vintage F3-A (F4=0)' if variant == 0 else 'F4 Transitions (F4=1)'
        scene['matched_gain'] = {['X', 'Y'][j]: float(gains[order[j]] * trim) for j in range(2)}
        manifest['scenes'].append(scene)
    B.dump(listen / 'manifest.json', manifest)
    B.dump(listen / 'answer-key.json', key)
    shutil.copy(HERE / 'listening.html', listen / 'index.html')
    shutil.copy(HERE / 'listening.mjs', listen / 'listening.mjs')
    if (HERE / 'LISTENING_GUIDE.md').exists():
        shutil.copy(HERE / 'LISTENING_GUIDE.md', listen / 'LEIA-ME.md')
    hashes = {p.relative_to(listen).as_posix(): B.sha(p.read_bytes())
              for p in sorted(listen.rglob('*')) if p.is_file() and p.name != 'sha256.json'}
    B.dump(listen / 'sha256.json', hashes)
    for rel, digest in hashes.items():
        assert B.sha((listen / rel).read_bytes()) == digest
    B.dump(HERE / 'render-manifest.json', {
        'package': 'artifacts/dsp-reform-f4/listening',
        'scene_count': len(manifest['scenes']), 'files': len(hashes),
        'manifest_sha256': B.sha((listen / 'manifest.json').read_bytes()),
        'answer_key_sha256': B.sha((listen / 'answer-key.json').read_bytes()),
        'sha256': hashes,
        'provenance': manifest['provenance'],
        'publication': 'PENDING_CI_ARTIFACT',
    })
    B.dump(OUT / 'render-summary.json', {'scenes': len(manifest['scenes']),
                                         'wav_files': len(hashes) - 2,
                                         'manifest_sha256': B.sha((listen / 'manifest.json').read_bytes()),
                                         'answer_key_sha256': B.sha((listen / 'answer-key.json').read_bytes()),
                                         'publication': 'PENDING_CI_ARTIFACT'})
    print(f'listening package: {len(manifest["scenes"])} scenes, {len(hashes)} files')


def cmd_tail() -> None:
    libs = {v: B.build(v) for v in (0, 1)}
    cases = {
        'static_fb95': (B.S(p13=95, p16=7), []),
        'static_fb100': (B.S(p13=100, p16=7), []),
        'automation_fb95': (B.S(p13=95, p16=7), [(20.0, {16: 1}), (25.0, {22: 1}),
                                                (35.0, {22: 0}), (40.0, {16: 6})]),
    }
    result = {}
    for name, (params, events) in cases.items():
        x = np.zeros((60 * 44100, 2), '<f4')
        x[0, :] = 0.5
        if name == 'automation_fb95':
            start, stop = 18 * 44100, 32 * 44100
            t = np.arange(stop - start) / 44100
            tone = (0.05 * np.sin(2 * np.pi * 110 * t))[:, None]
            x[start:stop] += np.repeat(tone, 2, axis=1).astype('<f4')
        applied = [(B.b128(s, 128), p) for s, p in events]
        rows = {}
        for v in (0, 1):
            y, _ = B.render(B.Engine(libs[v]), x, params, 128, applied)
            sec = np.array([np.sqrt(np.mean(y[i * 44100:(i + 1) * 44100].astype(float) ** 2))
                            for i in range(60)])
            rows[f'variant{v}'] = {
                'finite': bool(np.isfinite(y).all()),
                'peak': float(np.max(np.abs(y))),
                'last_second_growth_db': float(20 * np.log10(max(sec[-1], 1e-12) /
                                                             max(sec[-2], 1e-12))),
                'last_5s_vs_previous_5s_db': float(20 * np.log10(
                    max(float(np.sqrt(np.mean(sec[-5:] ** 2))), 1e-12) /
                    max(float(np.sqrt(np.mean(sec[-10:-5] ** 2))), 1e-12))),
                'rms_per_second': sec.tolist(),
                'zero_at_end': bool(np.max(np.abs(y[-44100:])) < 1e-3),
            }
        rows['bit_exact'] = bool(np.array_equal(
            B.render(B.Engine(libs[0]), x, params, 128, applied)[0],
            B.render(B.Engine(libs[1]), x, params, 128, applied)[0]))
        result[name] = rows
    B.dump(OUT / 'LONG_TAIL_CHECK.json', result)
    for name, rows in result.items():
        print(name, {v: {'peak': round(rows[v]['peak'], 4),
                         'growth_db': round(rows[v]['last_second_growth_db'], 6),
                         'exact': rows['bit_exact']} for v in ('variant0', 'variant1')})


def cmd_blocksize() -> None:
    libs = {v: B.build(v) for v in (0, 1)}
    frame = 303104  # lcm(1,37,64,128,256); 6.8733 s at 44.1 kHz
    cases = {'head': {16: 7}, 'freeze': {22: 1}, 'mix': {14: 100, 15: 100},
             'drive': {3: 100}, 'tone': {20: 100}}
    result = {}
    for name, change in cases.items():
        params = B.BASE.copy()
        if name == 'head':
            params[16] = 4
        if name == 'freeze':
            params[12] = 300
        if name == 'mix':
            params[14] = params[15] = 0
        if name == 'drive':
            params[3] = 0
        if name == 'tone':
            params[13] = 85
            params[20] = 0
        x = B.bed(9.0)
        events = [(frame, change)]
        rows = {}
        for v in (0, 1):
            rows[f'variant{v}'] = {}
            # Block 1 is covered exactly by tests/core/f4_transitions_test.cpp;
            # the ctypes bench compares the remaining block sizes to 128.
            outputs = {}
            for block in (37, 64, 128, 256):
                outputs[block], _ = B.render(B.Engine(libs[v]), x, params, block, events)
            reference = outputs[128]
            for block in (37, 64, 128, 256):
                d = outputs[block] - reference
                rows[f'variant{v}'][str(block)] = {
                    'max_abs_vs_128': float(np.max(np.abs(d))),
                    'rmse_vs_128': float(np.sqrt(np.mean(d.astype(float) ** 2))),
                    'output_sha256': B.sha(outputs[block].tobytes()),
                }
        result[name] = rows
    B.dump(OUT / 'BLOCK_SIZE_CONTRACT.json', result)
    for name, rows in result.items():
        print(name, {v: {k: round(x['max_abs_vs_128'], 9) for k, x in r.items()}
                     for v, r in rows.items()})


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('action', choices=['build', 'baseline', 'probe', 'measure', 'blocksize',
                                      'tail', 'render', 'package'])
    args = p.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    {'build': cmd_build, 'baseline': cmd_baseline, 'probe': cmd_probe,
     'measure': cmd_measure, 'blocksize': cmd_blocksize, 'tail': cmd_tail,
     'render': cmd_render, 'package': cmd_package}[args.action]()


if __name__ == '__main__':
    main()

