"""Build compact Markdown tables from the F5 measurement JSON files."""
from __future__ import annotations

import json
from pathlib import Path

HERE = Path(__file__).resolve().parent
OUT = HERE.parents[2] / 'artifacts' / 'dsp-reform-f5'


def load(name, default=None):
    p = OUT / name
    return json.loads(p.read_text()) if p.exists() else default


def fmt(value, digits=3):
    if value is None:
        return '—'
    if isinstance(value, bool):
        return 'yes' if value else 'no'
    if isinstance(value, float):
        return f'{value:.{digits}f}'
    return str(value)


def main():
    timing = load('TIMING_MEASUREMENTS.json')
    if timing:
        print('# Generated measurement tables\n')
        groups = {'manual': [], 'ms': [], 'tap': [], 'midi': [], 'mode': [], 'control': []}
        for name, scene in timing['scenes'].items():
            key = ('ms' if name.startswith('ms_') else
                   'tap' if name.startswith('tap_') else
                   'midi' if name.startswith('midi_') or name.startswith('source_switch')
                   else 'control' if name.startswith('control') else
                   'mode' if name.startswith('mode_') else 'manual')
            groups[key].append((name, scene))
        for key in ('manual', 'ms', 'tap', 'midi', 'mode', 'control'):
            if not groups[key]:
                continue
            print(f'\n## {key}\n')
            print('| scene | v | conv 1 ms (s) | max err (ms) | final err (smp) | peak pitch (cents) | HF 5 ms (dB) | peak vel |')
            print('|---|---|---|---|---|---|---|---|')
            for name, scene in groups[key]:
                for v in ('0', '1', '2'):
                    var = scene['variants'][v]
                    m = var['metrics']
                    print(f"| {name} | {v} | {fmt(m.get('convergence_to_1ms_s'))} | "
                          f"{fmt(m.get('max_error_ms'))} | {fmt(m.get('final_error_samples'))} | "
                          f"{fmt(var.get('peak_pitch_cents_abs'))} | {fmt(m.get('hf_db_5ms'))} | "
                          f"{fmt(var.get('max_read_velocity'), 4)} |")
        print('\n## clock final state\n')
        print('| scene | v | transport bpm | midi status | midi applied | tap applied | missed | phase |')
        print('|---|---|---|---|---|---|---|---|')
        for name, scene in timing['scenes'].items():
            for v in ('0', '1', '2'):
                cf = scene['variants'][v].get('clock_final') or {}
                if not cf:
                    continue
                print(f"| {name} | {v} | {fmt(cf.get('transport_bpm'), 3)} | "
                      f"{fmt(cf.get('midi_status'))} | {fmt(cf.get('midi_applied_bpm'), 3)} | "
                      f"{fmt(cf.get('tap_applied_bpm'), 3)} | {fmt(cf.get('midi_missed'))} | "
                      f"{fmt(cf.get('midi_phase_error'), 5)} |")
        print('\n## reverse\n')
        print('| scene | requested ms | effective ms | clamped |')
        print('|---|---|---|---|')
        for name, scene in timing['scenes'].items():
            for v in ('0', '1', '2'):
                var = scene['variants'][v]
                if 'reverse_requested_ms' in var:
                    print(f"| {name} v{v} | {fmt(var['reverse_requested_ms'], 2)} | "
                          f"{fmt(var['reverse_effective_ms'], 2)} | {fmt(var['reverse_clamped'])} |")
    static = load('STATIC_PRESERVATION.json')
    if static:
        print(f"\nStatic F0.2 scenes: v1 {static['f0_v1_bit_exact']}/{static['f0_total']} bit-exact, "
              f"v2 {static['f0_v2_bit_exact']}/{static['f0_total']} bit-exact.")
        print(f"Scene controls: {static['scene_controls']}")
    blocks = load('BLOCK_SIZE_CONTRACT.json')
    if blocks:
        print('\n## block size (max abs vs block 128)\n')
        print('| variant | 37 | 64 | 128 | 256 |')
        print('|---|---|---|---|---|')
        for v, rows in blocks['variants'].items():
            print(f"| {v} | {fmt(rows['37']['max_abs_vs_128'], 9)} | "
                  f"{fmt(rows['64']['max_abs_vs_128'], 9)} | 0 | "
                  f"{fmt(rows['256']['max_abs_vs_128'], 9)} |")
    for name in ('DRIFT_manual_60s', 'DRIFT_manual_60s_second'):
        drift = load(name + '.json')
        if drift:
            print(f"\n## {name}\n")
            print('| v | transport pos err (beats) | head err (smp) | final bpm | tail rms |')
            print('|---|---|---|---|---|')
            for v, row in drift['variants'].items():
                print(f"| {v} | {row['transport_position_error_beats']:.3e} | "
                      f"{row['head_error_samples']:.4f} | {row['transport_bpm_final']:.3f} | "
                      f"{row['tail_rms_last_second']:.4g} |")
    tail = load('LONG_TAIL_CHECK.json')
    if tail:
        print('\n## 60 s tails\n')
        print('| case | v | peak | last-second growth dB | finite |')
        print('|---|---|---|---|---|')
        for case, rows in tail.items():
            for v, row in rows.items():
                print(f"| {case} | {v} | {row['peak']:.4f} | "
                      f"{row['last_second_growth_db']:.6f} | {fmt(row['finite'])} |")
    ref = load('F5-A-reference.json')
    if ref:
        print(f"\nF5=0 vs F3-A: {ref['bit_exact_count']}/{ref['total']} bit-exact.")
    eq = load('INSTRUMENTATION_EQUIVALENCE.json')
    if eq:
        print(f"Instrumented vs plain: {eq['bit_exact_count']}/{eq['total']} bit-exact.")


if __name__ == '__main__':
    main()
