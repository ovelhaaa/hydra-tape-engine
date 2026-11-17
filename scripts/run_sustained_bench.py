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

def main():
    print(f"Connecting to ESP32-S3 on {PORT} for 5-minute sustained test...")
    s = serial.Serial(PORT, BAUD, timeout=10)
    time.sleep(1)

    print("Configuring Scenario 7: Full DSP Chain with Reverse V2 + GTC...")
    send_cmd(s, "byp 0")
    send_cmd(s, "gen 1")
    send_cmd(s, "src 1")
    send_cmd(s, "rev 1")
    send_cmd(s, "revmode 0")
    send_cmd(s, "revdiv 0")
    send_cmd(s, "wrp 1")
    send_cmd(s, "spr 1")
    send_cmd(s, "dly 500")
    send_cmd(s, "fbk 50")
    send_cmd(s, "tempo 120")
    send_cmd(s, "transport play")

    # 5 minutes = 300 seconds -> ~103,360 blocks @ 128 frames/block, 44.1 kHz
    blocks = 103360
    warmup = 100
    cmd = f'bench {blocks} {warmup}'
    print(f"Starting sustained run: {cmd} (~300 seconds)...")
    s.write(f'{cmd}\n'.encode('latin1'))

    t0 = time.time()
    output = []
    last_print = t0
    while time.time() - t0 < 330:
        line = s.readline().decode('latin1', errors='replace').strip()
        if line:
            output.append(line)
            if '===' in line or 'MeanUs=' in line or 'BudgetMeanPct=' in line or 'I2SFailDelta=' in line:
                print(line)
            if '=========================================' in line:
                break
        now = time.time()
        if now - last_print >= 30:
            print(f"  ... elapsed: {int(now - t0)}s / 300s ...")
            last_print = now

    total_time = time.time() - t0
    print(f"\nSustained test completed in {total_time:.2f} seconds.")
    print("Full result:")
    print('\n'.join(output))
    s.close()

if __name__ == '__main__':
    main()
