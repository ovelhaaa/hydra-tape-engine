"""F0.2 offline recipe. Never writes historical files; output must be new.

render creates a candidate manifest; verify compares it to the frozen manifest.
diagnose compares the historical WAVs to the documented F0.1 probe.
"""
import argparse
import ctypes as C
import hashlib
import json
from pathlib import Path
import platform
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'scripts'))
import numpy as np
import scipy
from scipy import signal
import musical_sources
import render_phase6_review as render

SOURCE_SHA = '3cc6c1b5f4ce8b93d7665744d266a82cb42e80e2'
PARAMS = [8, 6, 5, 25, 10, 50, 40, 30, 10, 6, .8, 1, 500,
          35, 50, 50, 4, 120, 0, 0, 50, 0, 0, 0, 0, 0, 60, 45,
          50, 0, 0, 1, 80, .5, 0, 0, 0, 0]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def dump(path, value):
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + '\n', encoding='utf-8')


def wav_info(path):
    data = path.read_bytes()
    assert data[:4] == b'RIFF' and data[8:12] == b'WAVE'
    assert struct.unpack_from('<I', data, 4)[0] + 8 == len(data)
    chunks, pcm, fmt = [], None, None
    pos = 12
    while pos + 8 <= len(data):
        tag, size = struct.unpack_from('<4sI', data, pos)
        body = data[pos+8:pos+8+size]
        assert len(body) == size
        chunks.append({'id': tag.decode('ascii'), 'offset': pos, 'size': size,
                       'sha256': sha(body)})
        if tag == b'fmt ':
            fmt = struct.unpack_from('<HHIIHH', body)
        if tag == b'data':
            assert pcm is None
            pcm = body
        pos += 8 + size + size % 2
    assert pos == len(data) and fmt is not None and pcm is not None
    code, channels, rate, byte_rate, align, bits = fmt
    assert code == 1 and bits == 24 and align == channels * 3
    a = np.frombuffer(pcm, np.uint8).reshape(-1, 3).astype(np.int32)
    q = a[:, 0] | (a[:, 1] << 8) | (a[:, 2] << 16)
    q = (q ^ 0x800000) - 0x800000
    x = (q.astype(np.float64) / 8388608).reshape(-1, channels)
    return {'file_sha256': sha(data), 'pcm_sha256': sha(pcm),
            'canonical_f64_sha256': sha(x.astype('<f8').tobytes()),
            'size': len(data), 'frames': len(x), 'format': {
                'code': code, 'channels': channels, 'sample_rate': rate,
                'bits': bits, 'byte_rate': byte_rate, 'block_align': align},
            'chunks': chunks}, x, data


def metrics(a, b):
    assert a.shape == b.shape and np.isfinite(a).all() and np.isfinite(b).all()
    d = b - a
    energy = float(np.sum(d*d))
    rms = float(np.sqrt(np.mean(d*d)))
    denom = float(np.sum(b*b))
    corr = float(np.corrcoef(a.ravel(), b.ravel())[0, 1]) if np.std(a) and np.std(b) else None
    return {'max_abs': float(np.max(np.abs(d))), 'rmse': rms,
            'difference_energy': energy, 'difference_dbfs': 20*np.log10(rms) if rms else None,
            'correlation': corr, 'gain_b_to_a': float(np.sum(a*b)/denom) if denom else None,
            'first_divergent_frame': int(np.flatnonzero(np.any(d != 0, axis=1))[0]) if np.any(d) else None}


def compare(a_path, b_path):
    ai, a, ab = wav_info(a_path)
    bi, b, bb = wav_info(b_path)
    n = min(len(a), len(b))
    raw = metrics(a[:n], b[:n])
    # Positive lag means regenerated b is later. Search +/- one second, both channels.
    aa, ba = a[:n], b[:n]
    corr = sum((signal.correlate(ba[:, ch], aa[:, ch], mode='full', method='fft') for ch in range(2)))
    lags = signal.correlation_lags(n, n)
    valid = np.abs(lags) <= 44100
    lag = int(lags[valid][np.argmax(corr[valid])])
    if lag >= 0:
        aa, ba = a[:n-lag], b[lag:n]
    else:
        aa, ba = a[-lag:n], b[:n+lag]
    gain = float(np.sum(aa*ba)/np.sum(ba*ba)) if np.sum(ba*ba) else 1.0
    equivalent = a.shape == b.shape and raw['max_abs'] <= 1e-5 and raw['rmse'] <= 1e-6
    classification = ('FILES_IDENTICAL' if ab == bb else 'PCM_IDENTICAL_CONTAINER_DIFFERENT'
                      if ai['pcm_sha256'] == bi['pcm_sha256'] and ai['format'] == bi['format'] else
                      'NUMERICALLY_EQUIVALENT' if equivalent else 'CAUSE_UNDETERMINED')
    return {'historical': ai, 'regenerated': bi, 'classification': classification,
            'format_equal': ai['format'] == bi['format'],
            'first_divergent_file_offset': next((i for i, (x, y) in enumerate(zip(ab, bb)) if x != y),
                                                 min(len(ab), len(bb)) if len(ab) != len(bb) else None),
            'raw': raw, 'prefixes': {str(k): metrics(a[:min(k,n)], b[:min(k,n)]) for k in (128,512,4096)},
            'diagnostic_only_alignment': {'lag_frames_b_later': lag, 'gain_b_to_a': gain,
                                          'aligned': metrics(aa, ba), 'aligned_and_gain': metrics(aa, ba*gain)}}


def stimulus(scene):
    np.random.seed(20261123)
    n = int(scene['duration_sec']*44100)
    kind = scene['source_kind']
    if kind in ('silence', 'impulse'):
        x = np.zeros((n, 2), dtype='<f4')
        if kind == 'impulse':
            x[0, :] = np.float32(.5)
        return x
    return np.ascontiguousarray(musical_sources.get_musical_source(kind, scene['duration_sec'], 44100), dtype='<f4')


def process(runner, x, params, reset=True, commit=True):
    lib = runner.lib
    h = C.c_void_p()
    assert lib.hydra_dsp_create_v1_4(44100, 2000, 8000, C.byref(h)) == 0 and h
    lib.hydra_dsp_commit.argtypes = [C.c_void_p]
    lib.hydra_dsp_commit.restype = C.c_int
    try:
        assert lib.hydra_dsp_prepare(h, 128, 2) == 0
        if reset:
            lib.hydra_dsp_reset(h)
        for i, v in enumerate(params):
            if i == 29:  # command, not a persistent parameter: zero means do not invoke
                assert v == 0
                continue
            assert lib.hydra_dsp_set_parameter(h, i, float(v)) == 0
        assert lib.hydra_dsp_set_tempo_source(h, 0) == 0
        assert lib.hydra_dsp_transport_play(h) == 0
        if commit:
            assert lib.hydra_dsp_commit(h) == 0
        effective = []
        for i in range(38):
            value = C.c_float()
            assert lib.hydra_dsp_get_parameter(h, i, C.byref(value)) == 0
            effective.append(value.value)
        out = np.empty_like(x)
        pointer = C.POINTER(C.c_float)
        for start in range(0, len(x), 128):
            size = min(128, len(x)-start)
            left = np.ascontiguousarray(x[start:start+size, 0])
            right = np.ascontiguousarray(x[start:start+size, 1])
            ol, or_ = np.empty(size, np.float32), np.empty(size, np.float32)
            assert lib.hydra_dsp_process(h, left.ctypes.data_as(pointer), right.ctypes.data_as(pointer),
                                         ol.ctypes.data_as(pointer), or_.ctypes.data_as(pointer), size) == 0
            out[start:start+size, 0], out[start:start+size, 1] = ol, or_
        assert np.isfinite(out).all()
        return out, effective
    finally:
        runner.destroy_instance(h)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('action', choices=['render', 'verify', 'diagnose'])
    p.add_argument('--library', type=Path)
    p.add_argument('--output', type=Path)
    p.add_argument('--historical-root', type=Path)
    p.add_argument('--probe', type=Path)
    p.add_argument('--manifest', type=Path, default=HERE/'baseline_manifest.json')
    p.add_argument('--allow-rebuilt-library', action='store_true',
                   help='Allow only binary SHA to differ; still require identical audio and all other provenance')
    args = p.parse_args()
    old = json.loads((HERE.parent/'dsp-reform-f0/baseline_renders_manifest.json').read_text())
    if args.action == 'diagnose':
        rows = {}
        for name, scene in old['scenarios'].items():
            historical = args.historical_root/Path(scene['output_wav'].replace('\\', '/'))
            info, _, _ = wav_info(historical)
            row = {'historical': info, 'historical_integrity': info['file_sha256'] == scene['output_wav_sha256']}
            if scene['source_kind'] == 'impulse':
                row['classification'] = 'HISTORICAL_RECIPE_UNKNOWN'
            else:
                row.update(compare(historical, args.probe/(name+'.wav')))
            rows[name] = row
        dump(args.output, rows)
        return
    if args.action == 'verify':
        expected = json.loads(args.manifest.read_text())
        actual = json.loads((args.output/'manifest.json').read_text())
        if args.allow_rebuilt_library:
            print('Binary provenance:', expected['library_sha256'], '->', actual['library_sha256'])
            expected['library_sha256'] = actual['library_sha256']
        assert expected == actual, 'Manifest differs from frozen baseline'
        for name, row in expected['scenarios'].items():
            assert wav_info(args.output/(name+'.wav'))[0] == row['wav']
            assert sha((args.output/(name+'.input.f32')).read_bytes()) == row['input_sha256']
            assert sha((args.output/(name+'.output.f32')).read_bytes()) == row['output_f32_sha256']
        print('PASS: 10 inputs / float32 outputs / PCM / WAV / provenance match')
        return
    args.output.mkdir(parents=True, exist_ok=False)
    runner = render.HydraEngineRunner(args.library.resolve())
    assert runner.api_version == 0x010700
    commands = json.loads((args.library.parent/'compile_commands.json').read_text())
    library_commands = [v['command'] for v in commands if 'hydra_abi_test_library.dir' in v['command']]
    assert len(library_commands) == 2
    assert all('-ffp-contract=off' in v and '-O3' in v and '-DNDEBUG' in v and
               'HYDRA_PHASE' not in v for v in library_commands)
    sources = ['dsp/core/tape_core.cpp', 'dsp/core/tape_core.hpp', 'dsp/core/hydra_dsp_c_api.cpp',
               'dsp/core/fast_tanh_table.inc', 'include/hydra_dsp.h', 'include/hydra_mech_noise.hpp',
               'scripts/musical_sources.py', 'scripts/render_phase6_review.py']
    for name in sources:
        assert subprocess.check_output(['git', 'show', SOURCE_SHA+':'+name], cwd=ROOT) == subprocess.check_output(['git','show','HEAD:'+name], cwd=ROOT)
        assert subprocess.run(['git','diff','--quiet','HEAD','--',name], cwd=ROOT).returncode == 0
    result = {'classification': 'NEW_BASELINE_REPRODUCIBLE', 'source_sha': SOURCE_SHA,
              'route': 'A production; no B/C macros', 'library_sha256': runner.library_sha256,
              'source_file_sha256': {name: sha((ROOT/name).read_bytes()) for name in sources},
              'recipe_sha256': sha(Path(__file__).read_bytes()),
              'compiler': subprocess.check_output(['g++', '--version'], text=True).splitlines()[0],
              'build_flags': '-O3 -DNDEBUG -ffp-contract=off -std=c++17; CMake Release; no experimental macros',
              'internal_seeds': {'hiss_L_R': [123456789,987654321],
                                 'scrape_L_R': [22222,33333],
                                 'dropout_common_L_R': [987654321,2246822519,3266489917],
                                 'wow_L_R': [0x13579BDF,0x2468ACE1],
                                 'flutter_L_R': [0x10293847,0x56473829]},
              'environment': {'python': platform.python_version(), 'numpy': np.__version__, 'scipy': scipy.__version__,
                              'platform': platform.platform()},
              'sample_rate': 44100, 'block_size': 128, 'seed': 20261123,
              'lifecycle': 'fresh create_v1_4(44100,2000,8000); prepare(128,2); reset; set IDs ascending except command29=0; internal tempo; play; commit; process blocks including short final block; destroy; no preroll',
              'impulse': {'frame': 0, 'amplitude': .5, 'channels': [0,1], 'dtype': 'little-endian float32', 'initial_silence': 0},
              'export': 'PCM24 round-to-even(clip(float32,-1,1)*8388607); no normalization/dither; wave stdlib',
              'artifact_url': None, 'scenarios': {}}
    for name, scene in old['scenarios'].items():
        params = PARAMS.copy()
        for k, v in scene['params_overrides'].items():
            params[int(k)] = v
        x = stimulus(scene)
        y, effective = process(runner, x, params)
        (args.output/(name+'.input.f32')).write_bytes(x.astype('<f4').tobytes())
        (args.output/(name+'.output.f32')).write_bytes(y.astype('<f4').tobytes())
        wav = args.output/(name+'.wav')
        render.write_wav_24bit(wav, y, 44100)
        result['scenarios'][name] = {'kind': scene['source_kind'], 'duration_sec': scene['duration_sec'],
                                    'parameters_requested': dict(enumerate(params)),
                                    'parameters_readback': dict(enumerate(effective)),
                                    'input_sha256': sha(x.astype('<f4').tobytes()),
                                    'output_f32_sha256': sha(y.astype('<f4').tobytes()),
                                    'wav': wav_info(wav)[0], 'finite': True,
                                    'peak': float(np.max(np.abs(y)))}
        print(name, flush=True)
    dump(args.output/'manifest.json', result)


if __name__ == '__main__':
    main()
