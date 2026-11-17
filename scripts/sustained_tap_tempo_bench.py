import serial
import time
import re
import sys

PORT = 'COM15'
BAUD = 115200

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

def send_cmd(s, cmd, wait=0.15):
    s.write(f'{cmd}\n'.encode('latin1'))
    time.sleep(wait)
    resp = b''
    while s.in_waiting:
        resp += s.read(s.in_waiting)
        time.sleep(0.02)
    return resp.decode('latin1', errors='replace')

def main():
    print(f"Connecting to ESP32-S3 on {PORT} for 5-minute Tap Tempo sustained test...")
    s = serial.Serial(PORT, BAUD, timeout=2)
    time.sleep(1.0)
    s.reset_input_buffer()

    print("Configuring full active chain: Tape + Reverse V2 + Warp + Spring...")
    send_cmd(s, "byp 0")
    send_cmd(s, "gen 1")
    send_cmd(s, "src 1")
    send_cmd(s, "rev 1")
    send_cmd(s, "revmode 0")
    send_cmd(s, "revdiv 0")
    send_cmd(s, "wrp 1")
    send_cmd(s, "spr 1")
    send_cmd(s, "dly 500")
    send_cmd(s, "fbk 45")
    send_cmd(s, "transport play")
    send_cmd(s, "perf r") # Reset perf counters

    print("\nStarting 300s (5-minute) sustained test with periodic Tap Tempo events...")
    t_start = time.time()
    next_tap_time = t_start + 5.0
    cadences = [120, 140, 90, 110, 130] # Target BPMs to cycle
    cadence_idx = 0
    total_taps = 0

    while time.time() - t_start < 300:
        now = time.time()
        if now >= next_tap_time:
            target_bpm = cadences[cadence_idx % len(cadences)]
            interval = 60.0 / target_bpm
            # Send sequence of 4 taps
            for _ in range(4):
                send_cmd(s, "tap", wait=0.01)
                total_taps += 1
                time.sleep(interval)
            cadence_idx += 1
            next_tap_time = time.time() + 15.0 # Wait 15s between sequences (exercises timeout too)

            elapsed = int(time.time() - t_start)
            perf_text = send_cmd(s, "perf")
            perf = parse_perf(perf_text)
            overruns = perf.get('overruns', 0)
            i2s_fails = perf.get('i2s_failures', 0)
            load = perf.get('budget_load', 0.0)
            avg_us = perf.get('avg_us', 0)
            print(f"[{elapsed:3d}s/300s] Taps sent: {total_taps} | Target BPM: {target_bpm} | Load: {load:.1f}% ({avg_us} us) | Overruns: {overruns} | I2S Fails: {i2s_fails}")
        else:
            time.sleep(0.5)

    print("\n--- 5-Minute Run Completed. Final Telemetry ---")
    final_perf = send_cmd(s, "perf")
    print(final_perf)
    final_tap = send_cmd(s, "tap status")
    print(final_tap)

    s.close()

if __name__ == "__main__":
    main()
