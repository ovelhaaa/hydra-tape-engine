"""Compare unchanged regression assertions using isolated baseline/candidate builds.

Only source copies receive observational prints. No thresholds/assertions, goldens,
official tests or production sources are edited. Reports include failed outcomes.
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'artifacts/bias-antialiasing/regression-diagnostic'
sys.path.insert(0, str(ROOT/'tests/core'))
import listening_review as listening


def marker_details(exe):
    scene=next(s for s in listening.TRANSITIONS if s.name=='reverse_stereo')
    with tempfile.TemporaryDirectory(dir=OUT) as temporary:
        work=Path(temporary)
        path=work/'input.f32'
        stimulus=listening.synthesize(scene,None)
        path.write_bytes(listening.little_endian(stimulus).tobytes())
        values=listening.run_scene([exe],scene,path,work,len(stimulus))
        control=listening.Scene(scene.name,scene.stimulus,scene.settings | {listening.WET:0},seconds=scene.seconds)
        dry=listening.run_scene([exe],control,path,work,len(stimulus))
        delayed=listening.delta(values,dry)
        windows={}
        for start in (1.25,1.75,2.4,3.1):
            indices=range(int(start*listening.RATE),int((start+.45)*listening.RATE))
            windows[str(start)]=[max(abs(delayed[2*i+c]) for i in indices) for c in (0,1)]
        peaks=[]
        for start in range(5):
            i=max(range(start*listening.RATE,(start+1)*listening.RATE),key=lambda i:abs(delayed[2*i]))
            peaks.append(dict(time=i/listening.RATE,amplitude=abs(delayed[2*i])))
        return dict(required_window_peaks_lr=windows,one_second_delayed_peaks=peaks)


def run(command):
    # Keep generated audio schedules accessible to Native children on Windows too.
    env = dict(os.environ, TMP=str(OUT), TEMP=str(OUT), TMPDIR=str(OUT))
    result = subprocess.run([str(x) for x in command], cwd=ROOT, env=env, capture_output=True, text=True)
    return dict(command=[str(x) for x in command], returncode=result.returncode,
                stdout=result.stdout, stderr=result.stderr)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    cache = dict(line.split('=', 1) for line in (ROOT/'build-native/CMakeCache.txt').read_text().splitlines() if '=' in line and not line.startswith('//'))
    compiler = cache['CMAKE_CXX_COMPILER:FILEPATH']
    os.environ['PATH'] = str(Path(compiler).parent) + os.pathsep + os.environ['PATH']
    flags = [compiler, '-O3', '-DNDEBUG', '-std=c++17', '-ffp-contract=off']
    flags += ['-I'+str(ROOT/p) for p in ('include', 'dsp/core', 'tests/core', 'host/native/esp32')]
    report = dict(commit=subprocess.check_output(['git','rev-parse','HEAD'], text=True).strip(), variants={})
    for variant in ('baseline','candidate'):
        folder = OUT/variant
        folder.mkdir(exist_ok=True)
        source = ROOT/'artifacts/bias-antialiasing/baseline/tape_core.cpp' if variant=='baseline' else ROOT/'dsp/core/tape_core.cpp'
        model = folder/'tape_core.cpp'
        model.write_text(source.read_text())
        builds=[]
        objects=[]
        for path in (model, ROOT/'dsp/core/hydra_dsp_c_api.cpp', ROOT/'host/native/esp32/hydra_dsp_esp32_adapter.cpp'):
            obj=folder/(path.stem+'.o')
            result=run(flags+['-c',path,'-o',obj])
            builds.append(result)
            assert result['returncode']==0,result
            objects.append(obj)
        entry=dict(source_sha256=hashlib.sha256(model.read_bytes()).hexdigest(),builds=builds,tests={})
        for name in ('warp_core_test','reverse_core_test','listen','render_trace'):
            test=ROOT/f'tests/core/{name}.cpp'
            if name == 'render_trace':
                trace_source = test.read_text(encoding='utf-8')
                if variant == 'baseline':
                    canonical = "for (float value : kFastTanhTable) { print_bits(value); std::cout << '\\n'; }"
                    historical = "for (int j=0; j<=1024; ++j) { float x=-4.0f+8.0f*(float(j)/float(1024)); print_bits(std::tanh(x)); std::cout << '\\n'; }"
                    assert trace_source.count(canonical) == 1, 'Historical LUT probe substitution did not apply'
                    trace_source = trace_source.replace(canonical, historical)
                test = folder/'render_trace.cpp'
                test.write_text(trace_source, encoding='utf-8')
                entry['trace_source_sha256'] = hashlib.sha256(test.read_bytes()).hexdigest()
                entry['lut_probe'] = 'historical std::tanh(float) initialization' if variant == 'baseline' else 'production canonical constants'
            if name=='reverse_core_test':
                copy=folder/test.name
                copy.write_text(test.read_text().replace('CHECK(energyPpR > 0.001f);',
                    'std::cerr << "DIAGNOSTIC energyPpL=" << energyPpL << " energyPpR=" << energyPpR << "\\n"; CHECK(energyPpR > 0.001f);'))
                test=copy
            exe=folder/(name+'.exe')
            result=run(flags+[test,*objects,'-o',exe])
            builds.append(result)
            assert result['returncode']==0,result
            if name in ('warp_core_test','reverse_core_test'):
                entry['tests'][name]=run([exe])
            elif name=='listen':
                entry['tests']['transition_characterization']=run([sys.executable,ROOT/'tests/core/listening_review.py','--native',exe,'--characterize-only','--output-dir',folder/'transition'])
        entry['marker_diagnostic']=marker_details(folder/'listen.exe')
        original=(ROOT/'tests/core/reverse_core_test.cpp').read_text()
        a=original.index('void test_stereo_integrity()')
        b=original.index('// E. Feedback Stability',a)
        part=original[a:b].replace('constexpr uint32_t N = 48000;', 'constexpr uint32_t N = 144000;')
        part=part.replace('CHECK(energyPpR > 0.001f);','std::cerr << "DIAGNOSTIC 3s energyPpL=" << energyPpL << " energyPpR=" << energyPpR << "\\n"; CHECK(energyPpR > 0.001f);')
        long_test=folder/'reverse_3s.cpp'
        long_test.write_text(original[:a]+part+original[b:])
        result=run(flags+[long_test,*objects,'-o',folder/'reverse_3s.exe'])
        builds.append(result)
        assert result['returncode']==0,result
        entry['three_second_pingpong_diagnostic']=run([folder/'reverse_3s.exe'])
        report['variants'][variant]=entry
        (OUT/'results.json').write_text(json.dumps(report,indent=2))
        print(variant, {k:v['returncode'] for k,v in entry['tests'].items()}, flush=True)
    empp=shutil.which('em++') or ('C:/emsdk/upstream/emscripten/em++.bat' if Path('C:/emsdk/upstream/emscripten/em++.bat').exists() else None)
    if empp:
        for variant in ('baseline','candidate'):
            folder=OUT/variant
            command=[empp,'-O3','-DNDEBUG','-std=c++17', '-ffp-contract=off','-I'+str(ROOT/'include'),'-I'+str(ROOT/'dsp/core'),folder/'tape_core.cpp',ROOT/'dsp/core/hydra_dsp_c_api.cpp',folder/'render_trace.cpp','-I'+str(ROOT/'tests/core'),'-sENVIRONMENT=node','-o',folder/'trace.js']
            for name in ('equivalence.json', 'equivalence.txt', 'equivalence-diff.csv', 'numerical-inputs.json'):
                (folder/name).unlink(missing_ok=True)
            report[variant+'_wasm_build']=run(command)
            if report[variant+'_wasm_build']['returncode']==0:
                report[variant+'_numerical_inputs'] = run([sys.executable,ROOT/'tests/core/verify_numerical_inputs.py','--native',folder/'render_trace.exe','--wasm-js',folder/'trace.js','--output',folder/'numerical-inputs.json'])
                report[variant+'_equivalence']=run([sys.executable,ROOT/'tests/core/compare_native_wasm.py','--native',folder/'render_trace.exe','--wasm-js',folder/'trace.js','--max-abs-threshold','1e-5','--rmse-threshold','1e-6','--report-json',folder/'equivalence.json','--report-txt',folder/'equivalence.txt','--diff-csv',folder/'equivalence-diff.csv'])
    if all((OUT/v/'equivalence.json').exists() for v in ('baseline','candidate')):
        comparisons={v:json.loads((OUT/v/'equivalence.json').read_text()) for v in ('baseline','candidate')}
        gaps={v:np.loadtxt(OUT/v/'equivalence-diff.csv',delimiter=',',skiprows=1)[:,1:3] for v in comparisons}
        difference=gaps['candidate']-gaps['baseline']
        report['equivalence_comparison']=dict(baseline=comparisons['baseline'],candidate=comparisons['candidate'],
            rmse_change=comparisons['candidate']['rmse']-comparisons['baseline']['rmse'],
            max_abs_change=comparisons['candidate']['max_abs']-comparisons['baseline']['max_abs'],
            error_vector_difference_rmse=float(np.sqrt(np.mean(difference**2))),
            error_vector_difference_max_abs=float(np.max(np.abs(difference))),
            limitation='Matched O3, ffp-contract=off builds within this execution; historical runtime-LUT baseline may differ across libm. Use recorded comparison statuses, not an assumed PASS or FAIL.')
    # Historical carrier-based assertions and historical libm mismatches are
    # observations. Current-production regressions remain failures of this job.
    candidate = report['variants']['candidate']
    failures = [name for name, value in candidate['tests'].items() if value['returncode'] != 0]
    if candidate['three_second_pingpong_diagnostic']['returncode'] != 0:
        failures.append('three_second_pingpong_diagnostic')
    if empp:
        for name in ('candidate_wasm_build', 'candidate_numerical_inputs', 'candidate_equivalence'):
            if report.get(name, {}).get('returncode') != 0:
                failures.append(name)
    report['current_candidate_failures'] = failures
    (OUT/'results.json').write_text(json.dumps(report,indent=2))
    print('Evidence:',OUT/'results.json')
    if failures:
        raise SystemExit('Current candidate diagnostic failures: ' + ', '.join(failures))


if __name__=='__main__':
    main()
