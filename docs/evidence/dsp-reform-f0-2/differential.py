"""Bounded hypotheses against F0; one change relative to documented runner each time."""
import argparse
import ctypes as C
import json
from pathlib import Path
import baseline as b


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--library', type=Path, required=True)
    p.add_argument('--historical-root', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    old = json.loads((b.HERE.parent/'dsp-reform-f0/baseline_renders_manifest.json').read_text())
    runner = b.render.HydraEngineRunner(args.library.resolve())
    rows = {}
    name = '01_clean_guitar_fb45'
    scene = old['scenarios'][name]
    x = b.stimulus(scene)
    historical = args.historical_root/scene['output_wav'].replace('\\', '/')
    for label, extra in [('runner_control', {}), ('explicit_reset_only', {'reset': True}),
                          ('explicit_commit_only', {'commit': True}),
                          ('block64_only', {'block64': True})]:
        # Same existing runner: retains original partial defaults, telemetry and lifecycle.
        original_create = runner.create_instance
        original_block = b.render.BLOCK
        def create(*a, **kw):
            h = original_create(*a, **kw)
            if extra.get('reset'):
                runner.lib.hydra_dsp_reset(h)
            return h
        runner.create_instance = create
        if extra.get('block64'):
            b.render.BLOCK = 64
        if extra.get('commit'):
            # A commit immediately before first process; no other state change.
            original_process = runner.lib.hydra_dsp_process
            runner.lib.hydra_dsp_commit.argtypes = [C.c_void_p]
            runner.lib.hydra_dsp_commit.restype = C.c_int
            def process(h, *a):
                assert runner.lib.hydra_dsp_commit(h) == 0
                return original_process(h, *a)
            runner.lib.hydra_dsp_process = process
        b.render.RATE = 44100
        y = b.render.run_simulation(runner, {'sample_rate': 44100, 'stimulus': x,
                                            'params': {int(k):v for k,v in scene['params_overrides'].items()}})['output']
        path = args.output/(label+'.wav')
        b.render.write_wav_24bit(path, y, 44100)
        rows[label] = b.compare(historical, path)
        runner.create_instance = original_create
        b.render.BLOCK = original_block
        if extra.get('commit'):
            runner.lib.hydra_dsp_process = original_process
    # A single explicit impulse hypothesis, never labelled historical unless hash matches.
    rows['impulse_candidate'] = {}
    for name, scene in old['scenarios'].items():
        if scene['source_kind'] == 'impulse':
            digest = b.sha(b.stimulus(scene).tobytes())
            rows['impulse_candidate'][name] = {'recipe': 'float32 stereo .5 at frame0; otherwise zero',
                                             'sha256': digest, 'historical_input_match': digest == scene['stimulus_sha256']}
    b.dump(b.HERE/'differential_results.json', {'library_sha256': runner.library_sha256, 'rows': rows})


if __name__ == '__main__':
    main()
