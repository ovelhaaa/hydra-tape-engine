#!/usr/bin/env python3
"""Create a deterministic, offline TapeCore-versus-ESP32 TapeModel review package."""
import argparse, array, json, math, pathlib, subprocess, wave

SIGNALS = {"impulse": 2, "linear_sweep": 2, "log_sweep": 2, "white_noise": 5,
           "guitar_di_synthetic": 3, "silence": 3}
SCENARIOS = ["saturator", "feedback_20", "feedback_50", "feedback_85", "mod_max", "reverse", "reverse_smear", "spring"]

def capture(runner, engine, signal, scenario, rate, seconds):
    text = subprocess.check_output([runner, "--engine", engine, "--signal", signal, "--scenario", scenario,
                                    "--sample-rate", str(rate), "--frames", str(rate * seconds)], text=True)
    return [tuple(map(float, line.split(','))) for line in text.splitlines()]
def wav(path, samples, rate):
    with wave.open(str(path), 'wb') as f:
        f.setparams((2, 2, rate, 0, 'NONE', 'not compressed'))
        raw=array.array('h')
        for l,r in samples:
            raw.extend((max(-32768,min(32767,round(l*32767))),max(-32768,min(32767,round(r*32767)))))
        f.writeframes(raw.tobytes())
def metrics(a,b,rate):
    diffs=[x-y for paira,pairb in zip(a,b) for x,y in zip(paira,pairb)]
    rmse=math.sqrt(sum(x*x for x in diffs)/len(diffs)); maximum=max(map(abs,diffs))
    # Windowed RMS is a portable loudness proxy; it intentionally avoids an external LUFS dependency.
    rms_a=math.sqrt(sum(x*x for p in a for x in p)/(2*len(a))); rms_b=math.sqrt(sum(x*x for p in b for x in p)/(2*len(b)))
    # Deterministic DFT probes at third-octave centres (first 4096 frames); this
    # is deliberately stdlib-only so the package has no optional DSP dependency.
    n=min(4096,len(a)); bands={}
    for f in (63,125,250,500,1000,2000,4000,8000,16000):
        if f >= rate/2 or not n: continue
        def magnitude(data):
            re=im=0.0
            for i in range(n):
                x=.5*(data[i][0]+data[i][1]); phase=2*math.pi*f*i/rate; re+=x*math.cos(phase); im-=x*math.sin(phase)
            return math.sqrt(re*re+im*im)/n
        ma,mb=magnitude(a),magnitude(b); bands[str(f)]=20*math.log10(max(ma,1e-12)/max(mb,1e-12))
    return {"max_abs": maximum, "rmse": rmse, "rms_db_difference": 20*math.log10(max(rms_a,1e-12)/max(rms_b,1e-12)), "spectral_delta_db_by_third_octave_center_hz":bands}
def main():
    ap=argparse.ArgumentParser(); ap.add_argument('--runner', default='build/hydra_core_vs_firmware_runner'); ap.add_argument('--output-dir', default='artifacts/ab_review_core_vs_firmware'); ap.add_argument('--quick', action='store_true', help='render one second per signal for CI/smoke use'); args=ap.parse_args()
    root=pathlib.Path(args.output_dir); root.mkdir(parents=True,exist_ok=True); rows=[]
    rates = (48000,) if args.quick else (44100,48000,96000)
    scenarios = ("feedback_50",) if args.quick else SCENARIOS
    signals = {"impulse": 1, "silence": 1} if args.quick else SIGNALS
    for rate in rates:
      for scenario in scenarios:
       for signal, seconds in signals.items():
        seconds=1 if args.quick else seconds; d=root/f'{rate}hz' / scenario / signal; d.mkdir(parents=True,exist_ok=True)
        core=capture(args.runner,'core',signal,scenario,rate,seconds); firmware=capture(args.runner,'firmware',signal,scenario,rate,seconds)
        diff=[(a[0]-b[0],a[1]-b[1]) for a,b in zip(core,firmware)]; summary={"sample_rate":rate,"scenario":scenario,"signal":signal,"frames":len(core),**metrics(core,firmware,rate),"threshold_reference":{"max_abs":1e-5,"rmse":1e-6}}
        wav(d/'native.wav',core,rate); wav(d/'firmware.wav',firmware,rate); wav(d/'diff.wav',diff,rate); (d/'summary.json').write_text(json.dumps(summary,indent=2)+'\n'); rows.append((summary,d.relative_to(root)))
    rows.sort(key=lambda item:item[0]['rmse'],reverse=True); (root/'package_summary.json').write_text(json.dumps([r[0] for r in rows],indent=2)+'\n')
    lines=['# TapeCore vs firmware TapeModel A/B package','', 'Generated deterministically by `tests/core/build_ab_review_core_vs_firmware.py`.', '', '## Listening order (descending RMSE)','', '| Package path | RMSE | max_abs | RMS delta (dB) |','|---|---:|---:|---:|']
    lines += [f'| `{path}` | {s["rmse"]:.6e} | {s["max_abs"]:.6e} | {s["rms_db_difference"]:.3f} |' for s,path in rows]
    lines += ['', '## Desktop firmware stubs', '', '`Arduino.h` supplies a deterministic `micros() == 0`, no-op `Serial`, `constrain`, and empty `IRAM_ATTR`; `esp_heap_caps.h` maps both ESP heap capabilities to `malloc/free`. This tests DSP math and not PSRAM allocation policy or clock-seeded noise. The guitar DI is a documented deterministic synthetic pluck because no DI fixture is present.']
    (root/'README.md').write_text('\n'.join(lines)+'\n')
if __name__=='__main__': main()
