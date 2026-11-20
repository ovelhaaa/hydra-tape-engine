"""Native diagnostic ablations. Only generated source copies are edited."""
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import numpy as np
from scipy.signal import welch

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "artifacts/bias-isolation"
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
    OUT.mkdir(parents=True, exist_ok=True)
    source_path = ROOT / 'dsp/core/tape_core.cpp'
    # Reconstruct Phase 6.1 even after production adopts the phase-averaged model.
    # Never reinterpret the candidate as the historical triangular control.
    source = model_sources()[0]['baseline']
    source_hash = sha(source_path)
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
    manifest = json.loads((ROOT / 'artifacts/listening-lab/phase6-musical-validation/manifest.json').read_text())
    scenario = manifest['scenarios']['01_tape_echo_single_head_quarter']
    np.random.seed(scenario['random_seed'])
    musical = musical_sources.get_musical_source(scenario['source_material'], scenario['duration_sec'], render.RATE, bpm=scenario['bpm'])
    t = np.arange(4 * render.RATE) / render.RATE
    sine = np.repeat((0.1 * np.sin(2 * np.pi * 440 * t)).astype(np.float32)[:, None], 2, axis=1)
    silence = np.zeros_like(sine)
    inputs = {'musical': musical, 'sine440': sine, 'silence': silence}
    params = {int(k): v for k, v in scenario['dsp_params'].items()}
    result = {
        'commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        'environment': {'os': platform.platform(), 'python': platform.python_version(), 'numpy': np.__version__, 'compiler': subprocess.check_output([compiler, '--version'], text=True).splitlines()[0]},
        'official_source_sha256': source_hash,
        'baseline_model_sha256': sha(ROOT/'dsp/diagnostics/tape_bias_baseline.inc'),
        'method': 'Separate shared libraries from source copies; -O3 -DNDEBUG -std=c++17. Identical stimuli, seeds and scenario-01 parameters. PSD band RMS: +/-10Hz around carrier, Welch 48000 samples, stereo channels separately. Silence retains the configured hiss.',
        'variants': {},
    }
    for name, text in variants.items():
        directory = OUT / name
        directory.mkdir(exist_ok=True)
        cpp = directory / 'tape_core.cpp'
        cpp.write_text(text, encoding='utf-8')
        windows = platform.system() == 'Windows'
        lib = directory / (f'{name}.dll' if windows else f'lib{name}.so')
        shared_flags = ['-Wl,--export-all-symbols'] if windows else ['-fPIC']
        command = [compiler, '-O3', '-DNDEBUG', '-std=c++17', '-shared', *shared_flags, '-I'+str(ROOT/'include'), '-I'+str(ROOT/'dsp/core'), str(cpp), str(ROOT/'dsp/core/hydra_dsp_c_api.cpp'), '-o', str(lib)]
        print(f'Building {name}...', flush=True)
        subprocess.run(command, check=True, capture_output=True)
        runner = render.HydraEngineRunner(lib)
        entry = variant_provenance(name, text, compiler, command)
        entry.update(source_sha256=sha(cpp), library_sha256=sha(lib), build_command=command, inputs={})
        for input_name, stimulus in inputs.items():
            audio = render.run_simulation(runner, {'stimulus': stimulus, 'params': params, 'initial_bpm': 120, 'tempo_source': 0})['output']
            path = directory / f'{input_name}.wav'
            render.write_wav_24bit(path, audio)
            # Ignore the initial second for spectral metrics; retain complete WAVs.
            tail = audio[render.RATE:]
            metrics = {'band_3k_dbfs': band_db(tail, 3000), 'band_6k_dbfs': band_db(tail, 6000), 'band_17k_dbfs': band_db(tail, 17000), 'band_18k_dbfs': band_db(tail, 18000), 'wav_sha256': sha(path), 'peak': float(np.max(np.abs(audio)))}
            entry['inputs'][input_name] = metrics
            print(name, input_name, json.dumps(metrics), flush=True)
        result['variants'][name] = entry
    official = ROOT/'artifacts/listening-lab/phase6-musical-validation'/scenario['processed_wav_file']
    result['copied_baseline_matches_official_wav'] = (OUT/'baseline/musical.wav').read_bytes() == official.read_bytes()
    assert result['copied_baseline_matches_official_wav'], 'Control build differs from official local WAV'
    assert sha(source_path) == source_hash, 'Official source was modified'
    result['official_source_unchanged'] = True
    (OUT/'results.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print('Control matches official WAV; official DSP source unchanged.', flush=True)

if __name__ == '__main__':
    main()
