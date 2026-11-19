"""Isolated Phase 6.2 experiments; no edits to production or reference audio.

Run before selecting a candidate. Acceptance limits below are fixed prospectively.
Compiler selected by CMake cache or --compiler; Linux GCC/Clang and MinGW supported.
"""
import argparse
import ctypes as ct
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import time

import numpy as np
from scipy import signal
import render_phase6_review as render
import musical_sources

ROOT = Path(__file__).resolve().parents[1]
NAMES = ('baseline', 'bias_off', 'sine', 'bandlimited', 'oversampled', 'averaged')
LIMITS = {
    'alias_reduction_db': 40.0,
    'sine_spurious_max_dbfs': -80.0,
    'silence_peak_max': 1e-7,
    'lowband_gain_change_max_db': 0.5,
    'lowband_crest_change_max_db': 0.5,
    'transfer_max_abs_error': 0.003,
    'transfer_slope_max_abs_error': 0.02,
    'finite_peak_max': 1.0,
    'parameter_excess_step_max': 0.03,
}


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def model_sources():
    official = (ROOT / 'dsp/core/tape_core.cpp').read_text()
    frozen = (ROOT / 'dsp/diagnostics/tape_bias_baseline.inc').read_text()
    baseline = frozen[frozen.index('struct TapeMagnetics {'):].strip()
    a = official.index('struct TapeMagnetics {')
    b = official.index('\nclass DCBlocker', a)
    old = 'float tri = 4.0f * std::fabs(biasPhase - 0.5f) - 1.0f;'
    models = {'baseline': baseline,
              'bias_off': baseline.replace('(biasAmount * biasGain * tri)', '0.0f'),
              'sine': baseline.replace(old, 'float tri = std::sin(TWO_PI * biasPhase);'),
              # Triangle Fourier fundamental; 3*f exceeds Nyquist at both target rates.
              'bandlimited': baseline.replace(old, 'float tri = (8.0f / (PI * PI)) * std::cos(TWO_PI * biasPhase);')}
    averaged = baseline.replace('    biasPhase += biasPhaseInc;\n    while (biasPhase >= 1.0f) {\n      biasPhase -= 1.0f;\n    }\n    '+old+'\n', '')
    averaged = averaged.replace('float xb = x * drive + (biasAmount * biasGain * tri);', 'float xb = x * drive;')
    averaged = averaged.replace('float y = lut->process(h);', '''// Phase-average of the original triangle's uniform amplitude distribution.
    // Two-point Gauss-Legendre: exact moments through order three.
    // Keep the original coercivity, remanence, normalization and gain ramp.
    const float b = biasAmount * biasGain * 0.5773502691896258f;
    float y = 0.5f * (lut->process(h - b) + lut->process(h + b));''')
    models['averaged'] = averaged
    # Local 2x experiment: explicit interpolation/reconstruction FIR on both sides.
    # 49 taps, Kaiser beta=9, cutoff .23 high-rate cycles/sample, 24 input-frame delay.
    taps = signal.firwin(49, .46, window=('kaiser', 9.0))
    coeff = ','.join(f'{v:.12e}f' for v in taps)
    oversampled = baseline.replace('float process(float x, bool allowBias)', 'float inner(float x, bool allowBias)')
    oversampled = oversampled.replace('biasPhaseInc = biasHz / fs;', 'biasPhaseInc = biasHz / (2.0f * fs);')
    oversampled = oversampled.replace('biasGainStep = 1.0f /', 'biasGainStep = 0.5f /')
    oversampled = oversampled.replace('satNorm = 1.0f /', 'satNorm = 1.0f /')
    # Normalize m time constant but retain normalization of original base-rate model.
    oversampled = oversampled.replace('\n  }\n\n  HYDRA_DSP_IRAM', '\n    remanence = std::sqrt(remanence);\n  }\n\n  HYDRA_DSP_IRAM', 1)
    code = '''
  float up[49]{}, down[49]{};
  int pos = 0;
  float process(float x, bool allowBias) {
    static constexpr float taps[49] = {COEFF};
    float result = 0.0f;
    for (int phase = 0; phase < 2; ++phase) {
      up[pos] = phase == 0 ? 2.0f*x : 0.0f;
      float interp = 0.0f;
      for (int k=0; k<49; ++k) interp += taps[k]*up[(pos-k+49)%49];
      down[pos] = inner(interp, allowBias);
      float decim = 0.0f;
      for (int k=0; k<49; ++k) decim += taps[k]*down[(pos-k+49)%49];
      if (phase == 0) result = decim;
      pos = (pos+1)%49;
    }
    return result;
  }
'''.replace('COEFF', coeff)
    oversampled = oversampled.replace('  inline void flushDenormals()', code+'\n  inline void flushDenormals()')
    oversampled = oversampled.replace('    m = 0.0f;\n    biasPhase', '    std::fill_n(up,49,0.0f); std::fill_n(down,49,0.0f); pos=0;\n    m = 0.0f;\n    biasPhase')
    models['oversampled'] = oversampled
    return {name: official[:a]+models[name]+'\n'+official[b:] for name in NAMES}, averaged


WRAPPER = r'''
#include <chrono>
#include "tape_core.cpp"
extern "C" {
void bias_stage(const float* input, float* output, int n, float fs, float drive, float age, float speed) {
  hydra::dsp::FastTanhLUT lut; lut.init();
  hydra::dsp::TapeMagnetics model; model.lut=&lut;
  hydra::dsp::TapeParams p; p.drive=drive; p.tapeAge=age; p.tapeSpeed=speed;
  model.updateParams(fs,p); model.reset();
  for(int i=0;i<n;++i) output[i]=model.process(input[i],true);
}
void bias_benchmark(double* timings, int blocks, float fs) {
  hydra::dsp::FastTanhLUT lut; lut.init();
  hydra::dsp::TapeMagnetics model; model.lut=&lut;
  hydra::dsp::TapeParams p; p.drive=75; model.updateParams(fs,p);
  volatile float sink=0;
  for(int block=-100;block<blocks;++block) {
    auto start=std::chrono::steady_clock::now();
    for(int i=0;i<256;++i) sink=model.process(0.3f+0.1f*std::sin(float(i)*0.1f),true);
    auto end=std::chrono::steady_clock::now();
    if(block>=0) timings[block]=std::chrono::duration<double,std::micro>(end-start).count();
  }
}
}
'''


def build(out, compiler):
    sources, _ = model_sources()
    windows = platform.system() == 'Windows'
    libraries = {}
    os.environ['PATH'] = str(Path(compiler).parent)+os.pathsep+os.environ['PATH']
    for name, text in sources.items():
        directory = out / name
        directory.mkdir(parents=True, exist_ok=True)
        cpp = directory/'tape_core.cpp'
        cpp.write_text(text)
        (directory/'wrapper.cpp').write_text(WRAPPER)
        lib = directory / ('bias.dll' if windows else 'libbias.so')
        cmd = [compiler, '-O3', '-DNDEBUG', '-std=c++17', '-shared', '-Wno-subobject-linkage']
        cmd += ['-Wl,--export-all-symbols'] if windows else ['-fPIC']
        cmd += ['-I'+str(ROOT/'include'), '-I'+str(ROOT/'dsp/core'), str(directory/'wrapper.cpp'), str(ROOT/'dsp/core/hydra_dsp_c_api.cpp'), '-o', str(lib)]
        print('Building', name, flush=True)
        subprocess.run(cmd, check=True)
        libraries[name] = str(lib.resolve())
        (directory/'build.json').write_text(json.dumps({'source_sha256': sha(cpp), 'library_sha256': sha(lib), 'command': cmd}, indent=2))
    (out/'libraries.json').write_text(json.dumps(libraries, indent=2))
    return libraries


def stage(lib, x, rate, drive=25, age=50, speed=50):
    x = np.ascontiguousarray(x, dtype=np.float32)
    y = np.zeros_like(x)
    ptr = ct.POINTER(ct.c_float)
    lib.bias_stage.argtypes = [ptr, ptr, ct.c_int]+[ct.c_float]*4
    lib.bias_stage(x.ctypes.data_as(ptr), y.ctypes.data_as(ptr), len(x), rate, drive, age, speed)
    return y


def db(value):
    return float(20*np.log10(max(float(value), 1e-15)))


def metrics(y, rate, fundamental=None):
    y = y[rate:].astype(float)  # exclude first second, analyze stationary 3 seconds
    f, psd = signal.welch(y, rate, window='hann', nperseg=rate, noverlap=rate//2)
    power = lambda mask: float(np.sum(psd[mask])*(f[1]-f[0]))
    audible = (f>=20)&(f<=min(20000,rate/2))
    peaks, _ = signal.find_peaks(psd)
    peaks = sorted((i for i in peaks if audible[i]), key=lambda i: psd[i], reverse=True)[:18]
    rms = np.sqrt(np.mean(y*y))
    result = {'rms_dbfs': db(rms), 'peak': float(np.max(np.abs(y))), 'crest_db': db(np.max(np.abs(y))/max(rms,1e-15)),
              'clipped_samples': int(np.sum(np.abs(y)>=1)),
              'bands_dbfs': {str(c): db(np.sqrt(power(np.abs(f-c)<=10))) for c in (3000,6000,10000,11000,12000,18000)},
              'audible_band_dbfs': db(np.sqrt(power(audible))),
              'top_psd_peaks': [{'hz': float(f[i]), 'dbfs_hz': float(10*np.log10(max(psd[i],1e-30)))} for i in peaks]}
    if fundamental:
        fund = np.abs(f-fundamental)<=3
        harmonics = np.zeros_like(f,dtype=bool)
        for k in range(2, int(20000/fundamental)+1):
            harmonics |= np.abs(f-k*fundamental)<=3
        fp = power(fund)
        result.update(thd_db=db(np.sqrt(power(harmonics&audible)/max(fp,1e-30))),
                      thdn_db=db(np.sqrt(power(audible&~fund)/max(fp,1e-30))),
                      nonharmonic_dbfs=db(np.sqrt(power(audible&~fund&~harmonics))))
    return result, f, psd


def stimuli(rate):
    t = np.arange(4*rate)/rate
    result = {'silence': np.zeros_like(t)}
    for hz in (110,440,1000,5000): result[f'sine{hz}'] = .1*np.sin(2*np.pi*hz*t)
    result['two_tone'] = .08*(np.sin(2*np.pi*440*t)+np.sin(2*np.pi*1000*t))
    result['impulse'] = np.zeros_like(t); result['impulse'][rate//2] = .7
    rng = np.random.default_rng(6200)
    result['noise'] = signal.sosfilt(signal.butter(6, 8000, fs=rate, output='sos'), rng.normal(0,.08,len(t)))
    for name in ('guitar_clean','guitar_driven','mono_synth','poly_synth','drum_machine'):
        np.random.seed(6200)
        result[name] = musical_sources.get_musical_source(name, 4, rate, bpm=120)[:,0]
    return result


def transfer(lib, drive, age, speed):
    levels = np.linspace(-1.5,1.5,151)
    # DC plateau per reset; final 1/2 second removes transient and carrier phase bias.
    means = [float(np.mean(stage(lib,np.full(48000,level),48000,drive,age,speed)[24000:])) for level in levels]
    return {'input': levels.tolist(), 'output': means, 'slope': np.gradient(means,levels).tolist(),
            'asymmetry_max': float(np.max(np.abs(np.array(means)+np.array(means)[::-1])))}


def analyze(out, libraries, rates):
    result = {'limits': LIMITS, 'commit': subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),
              'official_source_sha256': sha(ROOT/'dsp/core/tape_core.cpp'),
              'baseline_source_sha256': sha(ROOT/'dsp/diagnostics/tape_bias_baseline.inc'),
              'environment': {'os':platform.platform(),'python':sys.version,'numpy':np.__version__},
              'method': {'window':'periodic Hann','welch_nperseg':'sample_rate (1 Hz bins)','welch_overlap':'50%',
                         'interval_seconds':[1,4], 'thdn_band_hz':[20,20000], 'harmonic_exclusion_hz':3,
                         'THD':'RMS harmonic bands / RMS fundamental', 'THDN':'all in-band power excluding fundamental / fundamental',
                         'spectral_input':'isolated magnetic stage unless full_engine specified',
                         'benchmark':'Native stereo-equivalent magnetic stage, 128 frames; not ESP32 timing'},
              'variants':{}, 'gates':[]}
    spectra = {}
    for name, path in libraries.items():
        print('Analyzing',name,flush=True)
        lib = ct.CDLL(path)
        entry = {'build':json.loads((out/name/'build.json').read_text()), 'rates':{},'transfer':{}}
        for rate in rates:
            rows = {}
            for key,x in stimuli(rate).items():
                y = stage(lib,x,rate)
                fundamental = int(key[4:]) if key.startswith('sine') else None
                m,f,p = metrics(y,rate,fundamental)
                rows[key] = m
                if key in ('sine440','two_tone','guitar_clean'): spectra[f'{name}_{rate}_{key}'] = p
                if key == 'sine440':
                    rows['deterministic'] = bool(np.array_equal(y,stage(lib,x,rate)))
                    rows['high_drive'] = metrics(stage(lib,x*7,rate,100,100,0),rate,440)[0]
                    # Explicit IMD products, exclude harmonics of either tone separately below.
                if key == 'two_tone':
                    rows[key]['imd_products_dbfs'] = {str(hz): db(np.sqrt(np.sum(p[np.abs(f-hz)<=3]))) for hz in (120,560,1440,1560,1880,2440)}
                if name in ('baseline','averaged') and key in ('impulse','guitar_clean'):
                    np.save(out/name/f'{rate}_{key}.npy',y)
            entry['rates'][str(rate)] = rows
        for label,params in {'nominal':(25,50,50),'driven_aged_slow':(100,100,0),'clean_fast':(0,0,100)}.items():
            entry['transfer'][label] = transfer(lib,*params)
        timings=np.zeros(3000,dtype=np.float64)
        lib.bias_benchmark.argtypes=[ct.POINTER(ct.c_double),ct.c_int,ct.c_float]
        lib.bias_benchmark(timings.ctypes.data_as(ct.POINTER(ct.c_double)),len(timings),44100)
        entry['native_magnetic_us_per_stereo_block'] = {'mean':float(np.mean(timings)), 'p95':float(np.percentile(timings,95)), 'p99':float(np.percentile(timings,99)), 'peak':float(np.max(timings))}
        entry['expected_aliases_hz'] = {str(rate): {str(k): abs((k*18000+rate/2)%rate-rate/2) for k in range(1,16,2)} for rate in rates}
        result['variants'][name]=entry
    np.savez_compressed(out/'spectra.npz',**spectra)
    h=np.linspace(-4,4,16001)
    table_x=np.linspace(-4,4,1025); table_y=np.tanh(table_x)
    lookup=lambda x: np.interp(x,table_x,table_y)
    nodes,weights=np.polynomial.legendre.leggauss(64)
    reference=[]
    for amplitude in (.035,.07,.105):
        exact=sum(w*lookup(h+amplitude*n) for n,w in zip(nodes,weights))/2
        approx=(lookup(h-amplitude/np.sqrt(3))+lookup(h+amplitude/np.sqrt(3)))/2
        reference.append(dict(bias_amplitude=amplitude,max_abs_quadrature_error=float(np.max(abs(exact-approx))),
                              max_abs_bias_off_difference=float(np.max(abs(exact-lookup(h))))))
    result['quadrature_reference']={'method':'64-point quadrature using same1024-interval LUT,16001 frozen-state field values [-4,4]', 'rows':reference}
    def gate(name, passed, measured):
        result['gates'].append({'name':name,'pass':bool(passed),'measured':measured})
    a=result['variants']['averaged']; base=result['variants']['baseline']
    for rate in rates:
        rows=a['rates'][str(rate)]; baseline=base['rates'][str(rate)]
        # Search entire audible band outside all 440 Hz harmonics at nominal and high drive.
        for key in ('sine440','high_drive'):
            measured=rows[key]['nonharmonic_dbfs']; gate(f'{rate}/{key}/spurious',measured<LIMITS['sine_spurious_max_dbfs'],measured)
        gate(f'{rate}/silence', rows['silence']['peak']<=LIMITS['silence_peak_max'],rows['silence']['peak'])
        gate(f'{rate}/deterministic',rows['deterministic'],rows['deterministic'])
        for key in ('guitar_clean','guitar_driven','mono_synth','poly_synth','drum_machine'):
            # Compare musical identity below 3k, excluding removed carrier and aliases.
            x=stimuli(rate)[key]
            aa=stage(ct.CDLL(libraries['averaged']),x,rate)
            bb=stage(ct.CDLL(libraries['baseline']),x,rate)
            sos=signal.butter(6,3000,fs=rate,output='sos')
            aa=signal.sosfilt(sos,aa)[rate:]; bb=signal.sosfilt(sos,bb)[rate:]
            gain=db(np.sqrt(np.mean(aa*aa))/np.sqrt(np.mean(bb*bb)))
            crest=db(np.max(np.abs(aa))/np.sqrt(np.mean(aa*aa)))-db(np.max(np.abs(bb))/np.sqrt(np.mean(bb*bb)))
            rows[key]['lowband_gain_change_db']=gain; rows[key]['lowband_crest_change_db']=crest
            gate(f'{rate}/{key}/gain',abs(gain)<LIMITS['lowband_gain_change_max_db'],gain)
            gate(f'{rate}/{key}/crest',abs(crest)<LIMITS['lowband_crest_change_max_db'],crest)
        reduction=baseline['sine440']['bands_dbfs']['6000']-rows['sine440']['bands_dbfs']['6000']
        if rate==48000: gate('48000/6k_reduction',reduction>=LIMITS['alias_reduction_db'],reduction)
    for label,t in a['transfer'].items():
        b=base['transfer'][label]
        error=float(np.max(np.abs(np.array(t['output'])-b['output'])))
        slope=float(np.max(np.abs(np.array(t['slope'])-b['slope'])))
        gate('transfer/'+label,error<=LIMITS['transfer_max_abs_error'],error)
        gate('slope/'+label,slope<=LIMITS['transfer_slope_max_abs_error'],slope)
    (out/'results.json').write_text(json.dumps(result,indent=2))
    print('Gates:',sum(g['pass'] for g in result['gates']),'/',len(result['gates']),flush=True)
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'artifacts/bias-antialiasing')
    parser.add_argument('--compiler')
    parser.add_argument('--skip-build',action='store_true')
    parser.add_argument('--rates',type=int,nargs='+',default=[44100,48000,96000])
    args=parser.parse_args(); args.output.mkdir(parents=True,exist_ok=True)
    dll_dirs=[]
    if platform.system()=='Windows':
        cache=(ROOT/'build-native/CMakeCache.txt').read_text()
        local_compiler=next(line.split('=',1)[1] for line in cache.splitlines() if line.startswith('CMAKE_CXX_COMPILER:FILEPATH='))
        dll_dirs.append(os.add_dll_directory(str(Path(args.compiler or local_compiler).parent)))
    if args.skip_build: libraries=json.loads((args.output/'libraries.json').read_text())
    else:
        compiler=args.compiler
        if not compiler:
            cache=(ROOT/'build-native/CMakeCache.txt').read_text()
            compiler=next(line.split('=',1)[1] for line in cache.splitlines() if line.startswith('CMAKE_CXX_COMPILER:FILEPATH='))
        libraries=build(args.output,compiler)
    result=analyze(args.output,libraries,args.rates)
    if not all(g['pass'] for g in result['gates']): sys.exit(1)


if __name__=='__main__': main()
