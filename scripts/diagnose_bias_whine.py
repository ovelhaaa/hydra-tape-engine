"""Native diagnostic ablations. Only generated source copies are edited."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import wave
import numpy as np
from scipy.signal import welch

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT = ROOT / "artifacts/bias-isolation"
HISTORICAL_REFERENCE_SHA256 = "56e4936e3272f0fef3d685416e20f820fdc94c7f410452603ad9f85b559abce1"
# Phase 6.1 scenario 01 parameters, copied explicitly instead of reading a rendered package.
PARAMS = {0: 8, 1: 6, 3: 25, 4: 10, 11: 1, 12: 500, 13: 35, 14: 50,
          15: 50, 16: 4, 17: 120, 18: 0, 21: 0, 23: 0, 30: 0, 25: 0, 36: 0, 37: 0}
sys.path.insert(0, str(ROOT / 'scripts'))
import render_phase6_review as render
import musical_sources
from bias_antialiasing import model_sources, variant_provenance

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def band_db(audio, center, rate=48000):
    # A fixed 20 Hz band includes window leakage around an exact carrier.
    frequencies, psd = welch(audio.astype(np.float64), rate, nperseg=48000, axis=0)
    selected = np.abs(frequencies - center) <= 10
    power = np.trapezoid(psd[selected], frequencies[selected], axis=0)
    return [float(v) for v in 10 * np.log10(power + 1e-30)]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', help='C++ compiler; defaults to the Native CMake cache')
    parser.add_argument('--output', type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument('--reference-wav', type=Path,
                        help='Optional preserved Phase 6.1 scenario-01 WAV; requires its recorded SHA and exact output match')
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    source_path = ROOT / 'dsp/core/tape_core.cpp'
    # Reconstruct the historical magnetic model and LUT inside the current engine.
    # This is explicitly not the original whole-engine build or current production sound.
    source = model_sources()[0]['baseline']
    source_hash = sha(source_path)
    if args.compiler:
        compiler = shutil.which(args.compiler) or args.compiler
    else:
        cache = dict(line.split('=', 1) for line in (ROOT / 'build-native/CMakeCache.txt').read_text().splitlines() if '=' in line and not line.startswith('//'))
        compiler = cache['CMAKE_CXX_COMPILER:FILEPATH']
    os.environ['PATH'] = str(Path(compiler).parent) + os.pathsep + os.environ['PATH']
    variants = {
        'baseline': source,
        'bias_off': source.replace('(biasAmount * biasGain * tri)', '(0.0f * biasGain * tri)'),
        'bias_sine_18k': source.replace('float tri = 4.0f * std::fabs(biasPhase - 0.5f) - 1.0f;', 'float tri = std::sin(6.2831853071795864769f * biasPhase);'),
        'bias_triangle_17k': source.replace('biasHz = std::min(18000.0f, fs * 0.45f);', 'biasHz = std::min(17000.0f, fs * 0.45f);'),
    }
    for name, text in variants.items():
        assert name == 'baseline' or text != source, f'Ablation did not apply: {name}'
    np.random.seed(6101)
    musical = musical_sources.get_musical_source('clean_guitar', 4.0, render.RATE, bpm=120)
    t = np.arange(4 * render.RATE) / render.RATE
    sine = np.repeat((0.1 * np.sin(2 * np.pi * 440 * t)).astype(np.float32)[:, None], 2, axis=1)
    silence = np.zeros_like(sine)
    inputs = {name: np.ascontiguousarray(x, dtype='<f4') for name, x in
              {'musical': musical, 'sine440': sine, 'silence': silence}.items()}
    params = dict(PARAMS)
    stimulus_manifest = {}
    for name, stimulus in inputs.items():
        path = out / f'{name}_input.f32'
        path.write_bytes(stimulus.tobytes())
        stimulus_manifest[name] = dict(path=path.name, sha256=sha(path), frames=len(stimulus),
                                       channels=2, sample_rate_hz=render.RATE, format='float32 little-endian interleaved')
    result = {
        'commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        'environment': {'os': platform.platform(), 'python': platform.python_version(), 'numpy': np.__version__, 'compiler': subprocess.check_output([compiler, '--version'], text=True).splitlines()[0]},
        'official_source_sha256': source_hash,
        'baseline_model_sha256': sha(ROOT/'dsp/diagnostics/tape_bias_baseline.inc'),
        'method': 'Separate shared libraries from source copies; -O3 -DNDEBUG -std=c++17 -ffp-contract=off for every ablation. Frozen historical magnetic model/LUT inside current engine, not current production. PSD band RMS: +/-10Hz around carrier, Welch 48000 samples, stereo channels separately. Silence retains configured hiss.',
        'scenario': dict(source='clean_guitar', random_seed=6101, seconds=4.0, bpm=120, params=params),
        'inputs': stimulus_manifest,
        'expected_output': dict(frames=192000, channels=2, sample_rate_hz=48000, pcm_bits=24,
                                finite=True, deterministic=True),
        'historical_reference': dict(sha256=HISTORICAL_REFERENCE_SHA256,
                                     scope='Preserved Phase 6.1 Windows/UCRT, original compiler flags; optional exact control only',
                                     comparison='NOT_REQUESTED'),
        'variants': {},
    }
    for name, text in variants.items():
        directory = out / name
        directory.mkdir(exist_ok=True)
        cpp = directory / 'tape_core.cpp'
        cpp.write_text(text, encoding='utf-8')
        windows = platform.system() == 'Windows'
        lib = directory / (f'{name}.dll' if windows else f'lib{name}.so')
        shared_flags = ['-Wl,--export-all-symbols'] if windows else ['-fPIC']
        command = [compiler, '-O3', '-DNDEBUG', '-std=c++17', '-ffp-contract=off', '-shared', *shared_flags, '-I'+str(ROOT/'include'), '-I'+str(ROOT/'dsp/core'), str(cpp), str(ROOT/'dsp/core/hydra_dsp_c_api.cpp'), '-o', str(lib)]
        print(f'Building {name}...', flush=True)
        subprocess.run(command, check=True, capture_output=True)
        runner = render.HydraEngineRunner(lib)
        entry = variant_provenance(name, text, compiler, command)
        entry.update(source_sha256=sha(cpp), library_sha256=sha(lib), build_command=command, inputs={})
        for input_name, stimulus in inputs.items():
            audio = render.run_simulation(runner, {'stimulus': stimulus, 'params': params, 'initial_bpm': 120, 'tempo_source': 0})['output']
            repeat = render.run_simulation(runner, {'stimulus': stimulus, 'params': params, 'initial_bpm': 120, 'tempo_source': 0})['output']
            assert audio.shape == (192000, 2), 'Unexpected frame/channel count'
            assert np.isfinite(audio).all(), 'Non-finite diagnostic output'
            assert np.array_equal(audio, repeat), 'Non-deterministic diagnostic output'
            path = directory / f'{input_name}.wav'
            render.write_wav_24bit(path, audio)
            with wave.open(str(path), 'rb') as wav:
                assert (wav.getnframes(), wav.getnchannels(), wav.getframerate(), wav.getsampwidth()) == (192000, 2, 48000, 3), 'Unexpected WAV encoding'
            # Ignore the initial second for spectral metrics; retain complete WAVs.
            tail = audio[render.RATE:]
            metrics = {'band_3k_dbfs': band_db(tail, 3000), 'band_6k_dbfs': band_db(tail, 6000), 'band_17k_dbfs': band_db(tail, 17000), 'band_18k_dbfs': band_db(tail, 18000), 'wav_sha256': sha(path), 'peak': float(np.max(np.abs(audio))), 'frames': len(audio), 'finite': True, 'deterministic': True}
            entry['inputs'][input_name] = metrics
            print(name, input_name, json.dumps(metrics), flush=True)
        result['variants'][name] = entry
    if args.reference_wav:
        reference = args.reference_wav.resolve()
        assert sha(reference) == HISTORICAL_REFERENCE_SHA256, 'Not the preserved Phase 6.1 reference WAV'
        matches = (out/'baseline/musical.wav').read_bytes() == reference.read_bytes()
        result['historical_reference'].update(path=str(reference), comparison='PASS' if matches else 'FAIL')
        (out/'results.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
        assert matches, 'Historical exact-control mismatch (platform/flags must also match); not a current-production comparison'
    assert sha(source_path) == source_hash, 'Official source was modified'
    result['official_source_unchanged'] = True
    (out/'results.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print('Four historical ablations deterministic and finite; production source unchanged. Optional reference:', result['historical_reference']['comparison'], flush=True)

if __name__ == '__main__':
    main()
