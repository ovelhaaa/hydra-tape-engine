import serial
import time
import re

PORT = 'COM15'
BAUD = 115200

def send_cmd(s, cmd, wait=0.2):
    s.write(f'{cmd}\n'.encode('latin1'))
    time.sleep(wait)
    resp = b''
    while s.in_waiting:
        resp += s.read(s.in_waiting)
        time.sleep(0.05)
    return resp.decode('latin1', errors='replace')

def run_bench(s, blocks=1000, warmup=100, timeout=15):
    cmd = f'bench {blocks} {warmup}'
    s.write(f'{cmd}\n'.encode('latin1'))
    t0 = time.time()
    output = []
    while time.time() - t0 < timeout:
        line = s.readline().decode('latin1', errors='replace').strip()
        if line:
            output.append(line)
            if '===' in line and 'BENCH RESULT' in line:
                pass
            if '=========================================' in line:
                break
    return '\n'.join(output)

def parse_bench(text):
    res = {}
    for line in text.split('\n'):
        line = line.strip()
        if line.startswith('MeanUs='):
            # MeanUs=1387.23 MinUs=1366 P50Us=1375 P95Us=1429 P99Us=1442 PeakUs=1654
            for part in line.split():
                if '=' in part:
                    k, v = part.split('=', 1)
                    res[k] = float(v)
        elif line.startswith('BudgetMeanPct='):
            # BudgetMeanPct=47.79% BudgetP99Pct=49.68% Overruns=0 (0.00%) SatBins=0
            m = re.search(r'BudgetMeanPct=([\d\.]+)%\s+BudgetP99Pct=([\d\.]+)%\s+Overruns=(\d+)', line)
            if m:
                res['BudgetMeanPct'] = float(m.group(1))
                res['BudgetP99Pct'] = float(m.group(2))
                res['Overruns'] = int(m.group(3))
        elif 'HeapStart=' in line:
            # I2SFailDelta=0 HeapStart=126236 HeapEnd=126236 HeapMin=126236 PsramStart=1667736 PsramEnd=1667736 MaxParamUpdUs=34
            for part in line.split():
                if '=' in part:
                    k, v = part.split('=', 1)
                    try:
                        res[k] = int(v)
                    except ValueError:
                        pass
    return res

def main():
    print(f"Connecting to ESP32-S3 on {PORT}...")
    s = serial.Serial(PORT, BAUD, timeout=2)
    time.sleep(1)
    send_cmd(s, "byp 0") # not bypassed
    send_cmd(s, "gen 1") # generator on
    send_cmd(s, "src 1") # sine source

    scenarios = [
        ("1. Standard Delay, GTC Active", [
            ("rev 0", 0.1),
            ("wrp 0", 0.1),
            ("spr 0", 0.1),
            ("dly 500", 0.1),
            ("fbk 40", 0.1),
            ("tempo 120", 0.1),
            ("transport play", 0.1)
        ]),
        ("2. Reverse V2 Rhythmic Synchronized", [
            ("rev 1", 0.1),
            ("revmode 0", 0.1), # Rhythmic
            ("revdiv 0", 0.1),  # 1/4 note
            ("dly 500", 0.1),
            ("fbk 40", 0.1),
            ("tempo 120", 0.1),
            ("transport play", 0.1)
        ]),
        ("3. Reverse V2 Free Mode", [
            ("rev 1", 0.1),
            ("revmode 1", 0.1), # Free
            ("dly 500", 0.1),
            ("fbk 40", 0.1),
            ("tempo 120", 0.1),
            ("transport play", 0.1)
        ]),
        ("4. Reverse V2 Rhythmic (95% Feedback)", [
            ("rev 1", 0.1),
            ("revmode 0", 0.1),
            ("revdiv 0", 0.1),
            ("fbk 95", 0.1),
            ("tempo 120", 0.1),
            ("transport play", 0.1)
        ]),
        ("5. Reverse with BPM changes (60 -> 180)", [
            ("rev 1", 0.1),
            ("revmode 0", 0.1),
            ("revdiv 3", 0.1), # 1/8 note
            ("fbk 50", 0.1),
            ("tempo 180", 0.1),
            ("transport play", 0.1)
        ]),
        ("6. Full DSP Chain with GTC (Delay + Warp + Spring)", [
            ("rev 0", 0.1),
            ("wrp 1", 0.1),
            ("spr 1", 0.1),
            ("dly 500", 0.1),
            ("fbk 40", 0.1),
            ("tempo 120", 0.1),
            ("transport play", 0.1)
        ]),
        ("7. Full DSP Chain with Reverse V2 + GTC (Full Load)", [
            ("rev 1", 0.1),
            ("revmode 0", 0.1),
            ("revdiv 0", 0.1),
            ("wrp 1", 0.1),
            ("spr 1", 0.1),
            ("dly 500", 0.1),
            ("fbk 50", 0.1),
            ("tempo 120", 0.1),
            ("transport play", 0.1)
        ])
    ]

    results = []
    for name, setup_cmds in scenarios:
        print(f"\n--- Running: {name} ---")
        for cmd, w in setup_cmds:
            send_cmd(s, cmd, w)
        raw = run_bench(s, blocks=1000, warmup=100, timeout=10)
        parsed = parse_bench(raw)
        print(f"  Mean: {parsed.get('MeanUs', 0):.2f} us ({parsed.get('BudgetMeanPct', 0):.2f}% budget)")
        print(f"  P99:  {parsed.get('P99Us', 0):.2f} us ({parsed.get('BudgetP99Pct', 0):.2f}% budget)")
        print(f"  Peak: {parsed.get('PeakUs', 0):.2f} us | Overruns: {parsed.get('Overruns', 0)}")
        print(f"  Heap: min={parsed.get('HeapMin', 0)} delta={parsed.get('HeapStart', 0) - parsed.get('HeapEnd', 0)}")
        results.append((name, parsed))

    # Summary table
    print("\n" + "="*80)
    print("ESP32-S3 PHYSICAL BENCHMARK SUMMARY (N16R8 @ 240MHz, Budget: 2902.49 us/blk)")
    print("="*80)
    print(f"{'Scenario':<42} | {'Mean (us)':<10} | {'Budget %':<9} | {'P99 (us)':<9} | {'Overruns'}")
    print("-" * 80)
    for name, p in results:
        mean_us = f"{p.get('MeanUs', 0.0):.2f}"
        bud_pct = f"{p.get('BudgetMeanPct', 0.0):.2f}%"
        p99_us = f"{p.get('P99Us', 0.0):.2f}"
        ovr = f"{p.get('Overruns', 0)}"
        print(f"{name:<42} | {mean_us:<10} | {bud_pct:<9} | {p99_us:<9} | {ovr}")
    print("="*80)

    s.close()

if __name__ == '__main__':
    main()
