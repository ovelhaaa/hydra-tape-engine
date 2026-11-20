"""Render the ten final Phase 6.4.1 before/after pairs without human signoff."""
import argparse
import csv
import hashlib
import json
import platform
import subprocess
from pathlib import Path

import numpy as np
import musical_sources
import package_listening_lab
import package_reverse_ab as common
import phase64_audio_delta as delta
import render_phase6_review as render

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'artifacts/listening-lab/phase6-final-review')
    parser.add_argument('--archive', type=Path, default=ROOT / 'artifacts/hydra-phase6-final-ab.zip')
    parser.add_argument('--work', type=Path, default=ROOT / 'artifacts/tmp/phase641-listening')
    args = parser.parse_args()
    common.extract_commit(delta.BEFORE_COMMIT, args.work)
    build = args.work / 'build'
    build.mkdir()
    before_hash, before_cmd = common.build_library(args.work, build / 'before.dll')
    after_hash, after_cmd = common.build_library(ROOT, build / 'after.dll')
    before = render.HydraEngineRunner(build / 'before.dll')
    after = render.HydraEngineRunner(build / 'after.dll')
    if before.api_version != after.api_version:
        raise RuntimeError('API mismatch')
    args.output.mkdir(parents=True, exist_ok=True)
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    manifest = dict(schema_version=1, suite='Hydra Phase 6.4 Final A/B',
        reference_commit=commit, before_commit=delta.BEFORE_COMMIT, api_version=after.api_version,
        source_sha256=render.sha256_file(ROOT / 'dsp/core/tape_core.cpp'),
        before_library_sha256=before_hash, after_library_sha256=after_hash,
        native_library_sha256=after_hash, sample_rate_hz=render.RATE,
        block_size_frames=render.BLOCK, random_seed=6400,
        working_tree_dirty=bool(subprocess.check_output(['git', 'status', '--porcelain', '--untracked-files=no'], cwd=ROOT, text=True).strip()),
        environment=dict(os=platform.platform(), numpy=np.__version__, python=platform.python_version()),
        human_listening_signoff='PENDING_HUMAN_AUDITION', scenarios={})
    comparisons = {}
    for index, (title, source, params, events) in enumerate(delta.CASES, 1):
        seed = 6400 + index
        seconds = 60.0 if title == 'Long feedback' else 6.0
        np.random.seed(seed)
        stimulus = musical_sources.get_musical_source(source, seconds, render.RATE, bpm=120)
        if title == 'Long feedback':
            stimulus[render.RATE:] = 0.0
        cfg = dict(stimulus=stimulus, params=params, initial_bpm=120, tempo_source=0,
                   events=events, clock_pulses=[], sample_rate=render.RATE)
        a, b = render.run_simulation(before, cfg), render.run_simulation(after, cfg)
        metric = common.metrics(a['output'], b['output'])
        comparisons[title] = metric
        key = f'{index:02d}_final_{index:02d}'
        entry = dict(name=title, group='Final A/B',
            description='A: approved Phase 6.3 DSP; B: final DSP. Identical source, seed and events; no normalization.',
            duration_sec=seconds, bpm=120.0, tempo_source=0, reference_commit=commit,
            random_seed=seed, events=events, clock_pulses=[], sample_rate_hz=render.RATE,
            source_material=source, comparison_variant='final', comparison_case=index,
            comparison_title=title, human_review_state='NOT REVIEWED',
            stimulus_sha256=hashlib.sha256(stimulus.tobytes()).hexdigest(),
            dsp_params={str(k): float(v) for k, v in b['parameters'].items()},
            markers=[dict(time=0.0, label='Audio start'), dict(time=seconds, label='Audio end')],
            analysis_metrics=dict(before_rms=float(np.sqrt(np.mean(a['output'].astype(float)**2))),
                                  after_rms=float(np.sqrt(np.mean(b['output'].astype(float)**2))),
                                  after_peak=float(np.max(np.abs(b['output']))), **metric))
        for kind, audio in (('original_wav', a['output']), ('processed_wav', b['output'])):
            path = args.output / f'{key}_{kind}.wav'
            render.write_wav_24bit(path, audio, render.RATE)
            entry[kind + '_file'], entry[kind + '_sha256'] = path.name, render.sha256_file(path)
        path = args.output / f'{key}_telemetry.csv'
        with path.open('w', encoding='utf-8', newline='') as stream:
            writer = csv.DictWriter(stream, fieldnames=list(b['telemetry'][0]))
            writer.writeheader()
            writer.writerows(b['telemetry'])
        entry['telemetry_csv_file'], entry['telemetry_csv_sha256'] = path.name, render.sha256_file(path)
        manifest['scenarios'][key] = entry
        print(title, metric, flush=True)
    (args.output / 'manifest.json').write_text(json.dumps(manifest, indent=2, allow_nan=False), encoding='utf-8')
    reports = args.output / 'reports'
    reports.mkdir(exist_ok=True)
    (reports / 'comparison.json').write_text(json.dumps(dict(before_build=before_cmd, after_build=after_cmd,
        compiler_version=subprocess.check_output([common.compiler_path(), '--version'], text=True),
        comparisons=comparisons), indent=2), encoding='utf-8')
    package_listening_lab.package(args.output, args.archive)
    digest = render.sha256_file(args.archive)
    args.archive.with_suffix('.zip.sha256').write_text(f'{digest}  {args.archive.name}\n', encoding='ascii')
    print('ZIP SHA-256:', digest)


if __name__ == '__main__':
    main()
