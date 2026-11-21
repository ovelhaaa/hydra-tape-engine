"""Complete-engine Phase 6.2 checks, fixed limits imported from experiment suite."""
import ctypes as ct
import json
import os
from pathlib import Path
import sys
import time

import numpy as np
from scipy import signal
import bias_antialiasing as bias
import render_phase6_review as render

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT/'artifacts/bias-antialiasing'


def benchmark(runner, rate, params):
    handle=runner.create_instance(rate)
    try:
        for key,value in params.items(): runner.lib.hydra_dsp_set_parameter(handle,key,float(value))
        runner.lib.hydra_dsp_transport_play(handle)
        inp=(ct.c_float*128)(*(.1*np.sin(2*np.pi*440*np.arange(128)/rate)))
        left=(ct.c_float*128)(); right=(ct.c_float*128)()
        times=[]
        for block in range(3100):
            begin=time.perf_counter_ns()
            code=runner.lib.hydra_dsp_process(handle,inp,inp,left,right,128)
            elapsed=(time.perf_counter_ns()-begin)/1000
            if code: raise RuntimeError(code)
            if block>=100: times.append(elapsed)
        return dict(mean=float(np.mean(times)),p95=float(np.percentile(times,95)),
                    p99=float(np.percentile(times,99)),peak=float(np.max(times)),
                    pc_deadline_exceedances=int(np.sum(np.array(times)>128/rate*1e6)),
                    allocation_measurement='not instrumented',heap=None,psram=None,stack=None,
                    i2s_failures=None,physical_esp32=False)
    finally: runner.destroy_instance(handle)


def main():
    dll_dirs=[]
    if sys.platform=='win32':
        cache=(ROOT/'build-native/CMakeCache.txt').read_text()
        compiler=next(s.split('=',1)[1] for s in cache.splitlines() if s.startswith('CMAKE_CXX_COMPILER:FILEPATH='))
        dll_dirs.append(os.add_dll_directory(str(Path(compiler).parent)))
    libs=json.loads((OUT/'libraries.json').read_text())
    runners={name:render.HydraEngineRunner(Path(path)) for name,path in libs.items()}
    # Identity gate: the selected source must be the implementation under test.
    selected=(OUT/'averaged/tape_core.cpp').read_text()
    official=(ROOT/'dsp/core/tape_core.cpp').read_text()
    result={'limits':bias.LIMITS,'official_source_sha256':bias.sha(ROOT/'dsp/core/tape_core.cpp'),
            'candidate_matches_diagnostic_source':selected==official,
            'benchmark_method':'perf_counter around C API only; ctypes overhead included; Native host only, 3000 measured/100 warmup stereo blocks128',
            'rates':{},'gates':[]}
    def gate(name,passed,value): result['gates'].append(dict(name=name,pass_=bool(passed),measured=value))
    gate('official_candidate_identity',selected==official,selected==official)
    for rate in (44100,48000):
        print('Engine',rate,flush=True)
        old_rate=render.RATE; render.RATE=rate  # helper event/telemetry times use module RATE
        try:
            t=np.arange(6*rate)/rate
            tone=np.repeat((.1*np.sin(2*np.pi*440*t))[:,None],2,axis=1).astype(np.float32)
            params={0:0,1:0,2:0,3:25,4:0,5:50,6:50,7:0,8:0,11:1,12:125,13:35,14:100,15:100,16:1,19:0,20:100,23:0,25:0,30:0}
            cases={'sine440':(tone,{}), 'high_drive':(tone*7,{3:100,6:100,5:0}),
                   'silence':(np.zeros_like(tone),{}),'silence_hiss':(np.zeros_like(tone),{4:30}),
                   'feedback95':(tone*(t<1)[:,None],{13:95}),
                   'reverse':(tone*(t<1)[:,None],{13:75,23:1}),
                   'multihead':(tone*(t<1)[:,None],{13:75,16:7})}
            rows={}
            for case,(stimulus,overrides) in cases.items():
                entries={}
                for name in ('baseline','averaged'):
                    config=dict(stimulus=stimulus,params=params|overrides,sample_rate=rate)
                    y=render.run_simulation(runners[name],config)['output']
                    entries[name]={'channels':[bias.metrics(y[:,ch],rate,440 if case in ('sine440','high_drive') else None)[0] for ch in range(2)],
                                   'peak':float(np.max(np.abs(y))), 'finite':bool(np.all(np.isfinite(y))),
                                   'rms_by_second_dbfs':[bias.db(np.sqrt(np.mean(y[i*rate:(i+1)*rate].astype(float)**2))) for i in range(6)]}
                    if case=='sine440':
                        repeat=render.run_simulation(runners[name],config)['output']
                        entries[name]['deterministic']=bool(np.array_equal(y,repeat))
                    if name=='averaged':
                        gate(f'{rate}/{case}/finite_peak',np.all(np.isfinite(y)) and np.max(np.abs(y))<=bias.LIMITS['finite_peak_max'],entries[name]['peak'])
                        if case=='silence': gate(f'{rate}/zero_input',entries[name]['peak']<=bias.LIMITS['silence_peak_max'],entries[name]['peak'])
                        if case=='sine440': gate(f'{rate}/deterministic',entries[name]['deterministic'],entries[name]['deterministic'])
                        if case in ('feedback95','reverse','multihead'):
                            seconds=entries[name]['rms_by_second_dbfs']
                            gate(f'{rate}/{case}/no_tail_growth',seconds[-1]<=seconds[2],seconds[-1]-seconds[2])
                    if case in ('feedback95','reverse','sine440','silence_hiss'):
                        render.write_wav_24bit(OUT/name/f'engine_{rate}_{case}.wav',y,rate)
                rows[case]=entries
            # Same input/events, continuous low-level110Hz so ordinary waveform slope is small.
            inp=np.repeat((.02*np.sin(2*np.pi*110*t))[:,None],2,axis=1).astype(np.float32)
            events=[dict(time_sec=2,type='param',id=3,value=100),dict(time_sec=3,type='param',id=5,value=0),dict(time_sec=4,type='param',id=6,value=100)]
            steps={}
            for name in ('baseline','averaged'):
                y=render.run_simulation(runners[name],dict(stimulus=inp,params=params,sample_rate=rate,events=events))['output']
                excess=[]
                for second in (2,3,4):
                    center=int(second*rate); window=y[center-256:center+512]
                    excess.append(float(np.max(np.abs(np.diff(window,axis=0)))))
                steps[name]=excess
            gate(f'{rate}/parameter_steps',max(steps['averaged'])<=bias.LIMITS['parameter_excess_step_max'],max(steps['averaged']))
            rows['parameter_max_steps']=steps
            rows['native_total_us_per_block']={name:benchmark(runner,rate,params|{13:95,16:7}) for name,runner in runners.items()}
            result['rates'][str(rate)]=rows
        finally: render.RATE=old_rate
    (OUT/'engine-results.json').write_text(json.dumps(result,indent=2))
    print('Full-engine gates:',sum(g['pass_'] for g in result['gates']),'/',len(result['gates']))
    if not all(g['pass_'] for g in result['gates']): raise SystemExit(1)


if __name__=='__main__': main()
