import serial
import time
import re
import sys

def parse_perf(text):
    data = {}
    patterns = {
        'blocks': r'Blocks Processed\s*:\s*(\d+)',
        'avg_us': r'Avg DSP Block Time\s*:\s*(\d+)',
        'last_us': r'Last DSP Block Time\s*:\s*(\d+)',
        'peak_us': r'Peak DSP Block Time\s*:\s*(\d+)',
        'budget_load': r'DSP Budget Use \(EMA\)\s*:\s*([\d\.]+)',
        'overruns': r'DSP Budget Overruns\s*:\s*(\d+)',
        'i2s_failures': r'I2S TX Write Failures\s*:\s*(\d+)',
        'heap_free': r'Internal Free Heap\s*:\s*(\d+)',
        'psram_free': r'SPIRAM \(PSRAM\) Free\s*:\s*(\d+)'
    }
    for key, pat in patterns.items():
        m = re.search(pat, text)
        if m:
            val = float(m.group(1)) if '.' in m.group(1) else int(m.group(1))
            data[key] = val
    return data

def parse_bench(text):
    data = {}
    m1 = re.search(r'MeanUs=([\d\.]+)\s+MinUs=(\d+)\s+P50Us=(\d+)\s+P95Us=(\d+)\s+P99Us=(\d+)\s+PeakUs=(\d+)', text)
    if m1:
        data['mean_us'] = float(m1.group(1))
        data['min_us'] = int(m1.group(2))
        data['p50_us'] = int(m1.group(3))
        data['p95_us'] = int(m1.group(4))
        data['p99_us'] = int(m1.group(5))
        data['peak_us'] = int(m1.group(6))
    m2 = re.search(r'BudgetMeanPct=([\d\.]+)%\s+BudgetP99Pct=([\d\.]+)%\s+Overruns=(\d+)', text)
    if m2:
        data['budget_mean_pct'] = float(m2.group(1))
        data['budget_p99_pct'] = float(m2.group(2))
        data['overruns'] = int(m2.group(3))
    m3 = re.search(r'I2SFailDelta=(\d+)\s+HeapStart=(\d+)\s+HeapEnd=(\d+)\s+HeapMin=(\d+)\s+PsramStart=(\d+)\s+PsramEnd=(\d+)', text)
    if m3:
        data['i2s_fail_delta'] = int(m3.group(1))
        data['heap_start'] = int(m3.group(2))
        data['heap_end'] = int(m3.group(3))
        data['psram_start'] = int(m3.group(5))
        data['psram_end'] = int(m3.group(6))
    return data

def main():
    port = 'COM15'
    baud = 115200
    print(f"Connecting to ESP32-S3 on {port} @ {baud}...")
    ser = serial.Serial(port, baud, timeout=1)
    time.sleep(1)
    ser.reset_input_buffer()

    def send_cmd(cmd, delay=0.2):
        ser.write((cmd + '\n').encode('utf-8'))
        time.sleep(delay)
        out = ser.read_all().decode('utf-8', errors='ignore')
        return out

    print("Configuring DSP full chain: Generator + Delay + Reverse V2 (Rhythmic 1/4) + Freeverb...")
    send_cmd("byp 0")
    send_cmd("gen 1")
    send_cmd("dly 1")
    send_cmd("rev 1")
    send_cmd("revmode 0")
    send_cmd("revdiv 0")
    send_cmd("frv 1")
    send_cmd("dly 500")
    send_cmd("fbk 45")
    send_cmd("wet 50")
    time.sleep(1)

    print("Running initial benchmark (bench 1000 100)...")
    bench_out = send_cmd("bench 1000 100", delay=5.0)
    initial_bench = parse_bench(bench_out)
    print("Initial bench parsed:", initial_bench)

    initial_perf_text = send_cmd("perf", delay=0.5)
    initial_perf = parse_perf(initial_perf_text)
    print("Initial perf parsed:", initial_perf)

    print("\nStarting 5-minute sustained test (300 seconds)...")
    start_time = time.time()
    last_sample_time = start_time
    samples = []
    
    # Store initial blocks and heap
    start_blocks = initial_perf.get('blocks', 0)
    initial_heap = initial_perf.get('heap_free', 0)
    initial_psram = initial_perf.get('psram_free', 0)

    while True:
        now = time.time()
        elapsed = now - start_time
        if elapsed >= 300.0:
            break

        # Dynamic automation during sustained test
        if 99.0 <= elapsed <= 102.0 and not getattr(main, 'step1', False):
            print(f"[{elapsed:.1f}s] Automating: Switching reverse subdivision to 1/8 (revdiv 3)...")
            send_cmd("revdiv 3")
            main.step1 = True

        if 199.0 <= elapsed <= 202.0 and not getattr(main, 'step2', False):
            print(f"[{elapsed:.1f}s] Automating: Switching BPM to 140 and mode to Free (revmode 1)...")
            send_cmd("bpm 140")
            send_cmd("revmode 1")
            main.step2 = True

        if 249.0 <= elapsed <= 252.0 and not getattr(main, 'step3', False):
            print(f"[{elapsed:.1f}s] Automating: Switching back to 120 BPM, Rhythmic 1/4 (revmode 0, revdiv 0)...")
            send_cmd("bpm 120")
            send_cmd("revmode 0")
            send_cmd("revdiv 0")
            main.step3 = True

        if now - last_sample_time >= 15.0:
            perf_text = send_cmd("perf", delay=0.3)
            p = parse_perf(perf_text)
            p['elapsed_s'] = round(elapsed, 1)
            samples.append(p)
            print(f"[{elapsed:5.1f}s] Blocks: {p.get('blocks')} | Avg: {p.get('avg_us')}us | Peak: {p.get('peak_us')}us | Overruns: {p.get('overruns')} | I2S Fail: {p.get('i2s_failures')} | Heap: {p.get('heap_free')} B | PSRAM: {p.get('psram_free')} B")
            last_sample_time = now

        time.sleep(1.0)

    print("\n5-minute sustained test completed! Running final benchmark (bench 1000 100)...")
    final_bench_out = send_cmd("bench 1000 100", delay=5.0)
    final_bench = parse_bench(final_bench_out)
    print("Final bench parsed:", final_bench)

    final_perf_text = send_cmd("perf", delay=0.5)
    final_perf = parse_perf(final_perf_text)
    print("Final perf parsed:", final_perf)

    end_blocks = final_perf.get('blocks', 0)
    delta_blocks = end_blocks - start_blocks

    print("\n================== SUSTAINED TEST REPORT ==================")
    print(f"Total Duration        : {time.time() - start_time:.2f} seconds")
    print(f"Blocks Processed      : {delta_blocks} (from {start_blocks} to {end_blocks})")
    print(f"Initial Avg DSP Time  : {initial_perf.get('avg_us')} us")
    print(f"Final Avg DSP Time    : {final_perf.get('avg_us')} us")
    print(f"Overall Peak DSP Time : {final_perf.get('peak_us')} us (Budget: 2902.5 us)")
    print(f"Total Overruns        : {final_perf.get('overruns')} (Must be 0)")
    print(f"Total I2S Failures    : {final_perf.get('i2s_failures')} (Must be 0)")
    print(f"Heap Delta            : {final_perf.get('heap_free', 0) - initial_heap} bytes (Initial: {initial_heap}, Final: {final_perf.get('heap_free')})")
    print(f"PSRAM Delta           : {final_perf.get('psram_free', 0) - initial_psram} bytes (Initial: {initial_psram}, Final: {final_perf.get('psram_free')})")
    print(f"Final Benchmark P50   : {final_bench.get('p50_us')} us")
    print(f"Final Benchmark P95   : {final_bench.get('p95_us')} us")
    print(f"Final Benchmark P99   : {final_bench.get('p99_us')} us")
    print(f"Final Benchmark Mean  : {final_bench.get('mean_us')} us")
    print("===========================================================\n")

    ser.close()

if __name__ == '__main__':
    main()
