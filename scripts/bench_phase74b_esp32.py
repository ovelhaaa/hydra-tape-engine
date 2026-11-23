import json
import os
import re
import subprocess
import time
from pathlib import Path
import serial

PIO = r"C:\.platformio\penv\Scripts\pio.exe"
PORT = "COM15"
BAUD = 115200
ENV = "esp32-s3-devkitc-1"
ROOT = Path(__file__).resolve().parent.parent

SCENARIOS = [
    ("1_standard_single_head", [
        "byp 0", "gen 1", "src 1",
        "rev 0", "wrp 0", "spr 0",
        "hds 4", "dly 340", "fbk 50",
        "wwd 25", "wwr 0.8", "ftd 25", "ftr 6.0", "tpa 40",
    ]),
    ("2_multihead_spring", [
        "byp 0", "gen 1", "src 1",
        "rev 0", "wrp 0", "spr 1",
        "hds 7", "dly 340", "fbk 65",
        "wwd 30", "wwr 0.8", "ftd 28", "ftr 6.0", "tpa 50",
    ]),
    ("3_worst_case_multihead_reverse_spring", [
        "byp 0", "gen 1", "src 1",
        "rev 1", "revmode 0", "revdiv 0", "wrp 0", "spr 1",
        "hds 7", "dly 360", "fbk 75",
        "wwd 48", "wwr 0.95", "ftd 45", "ftr 6.8", "tpa 65",
        "tempo 120", "transport play",
    ]),
]


def build_and_upload(mode: int):
    env = os.environ.copy()
    env["PLATFORMIO_BUILD_FLAGS"] = f"-DHYDRA_PHASE74B_MOTION_MODE={mode}"
    cmd = [PIO, "run", "-e", ENV, "-t", "upload", "--upload-port", PORT]
    print(f"\n=== Building & Uploading M{mode} ({ENV}) ===")
    proc = subprocess.run(cmd, cwd=ROOT, env=env, capture_output=True, text=True)
    if proc.returncode != 0:
        print(proc.stdout[-2000:])
        print(proc.stderr[-2000:])
        raise RuntimeError(f"PlatformIO build/upload failed for M{mode}")
    print(f"Uploaded M{mode} successfully.")


def wait_for_serial(port: str, timeout_s: float = 20.0):
    t0 = time.time()
    while time.time() - t0 < timeout_s:
        try:
            s = serial.Serial(port, BAUD, timeout=2)
            time.sleep(2.0)
            s.reset_input_buffer()
            return s
        except serial.SerialException:
            time.sleep(0.5)
    raise RuntimeError(f"Timed out waiting for serial port {port}")


def send_cmd(s, cmd: str, wait: float = 0.08):
    s.write((cmd + "\n").encode("latin1"))
    time.sleep(wait)
    while s.in_waiting:
        s.read(s.in_waiting)
        time.sleep(0.02)


def run_bench(s, blocks: int = 1000, warmup: int = 100, timeout: float = 20.0):
    s.reset_input_buffer()
    s.write(f"bench {blocks} {warmup}\n".encode("latin1"))
    t0 = time.time()
    lines = []
    seen_header = False
    while time.time() - t0 < timeout:
        raw = s.readline()
        if not raw:
            continue
        line = raw.decode("latin1", errors="replace").strip()
        if line:
            lines.append(line)
            if "BENCH RESULT" in line:
                seen_header = True
            elif seen_header and line.startswith("========================================="):
                break
    return "\n".join(lines)


def parse_bench(text: str):
    res = {}
    for line in text.splitlines():
        line = line.strip()
        if line.startswith("MeanUs="):
            for part in line.split():
                if "=" in part:
                    k, v = part.split("=", 1)
                    res[k] = float(v)
        elif line.startswith("BudgetMeanPct="):
            m = re.search(r"BudgetMeanPct=([\d\.]+)%\s+BudgetP99Pct=([\d\.]+)%\s+Overruns=(\d+)", line)
            if m:
                res["BudgetMeanPct"] = float(m.group(1))
                res["BudgetP99Pct"] = float(m.group(2))
                res["Overruns"] = int(m.group(3))
        elif "HeapStart=" in line:
            for part in line.split():
                if "=" in part:
                    k, v = part.split("=", 1)
                    try:
                        res[k] = int(v)
                    except ValueError:
                        pass
    return res


def main():
    results = {}
    # Run M1, M2, M3 first, then M0 last so Classic C remains flashed on the board.
    for mode in [1, 2, 3, 0]:
        build_and_upload(mode)
        s = wait_for_serial(PORT)
        try:
            mode_res = {}
            for sc_name, cmds in SCENARIOS:
                for c in cmds:
                    send_cmd(s, c)
                time.sleep(0.25)
                raw = run_bench(s, blocks=1000, warmup=100)
                parsed = parse_bench(raw)
                if "MeanUs" not in parsed:
                    raise RuntimeError(f"Failed to parse bench output for M{mode} {sc_name}:\n{raw}")
                mode_res[sc_name] = parsed
                print(
                    f"  M{mode} | {sc_name}: Mean={parsed['MeanUs']:.2f} us ({parsed['BudgetMeanPct']:.2f}%), "
                    f"P99={parsed['P99Us']:.0f} us ({parsed['BudgetP99Pct']:.2f}%), Peak={parsed['PeakUs']:.0f} us, "
                    f"Overruns={parsed['Overruns']}"
                )
            results[f"M{mode}"] = mode_res
        finally:
            s.close()

    out_path = ROOT / "artifacts/phase7-4b/esp32_s3_benchmark.json"
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    print(f"\nSaved ESP32-S3 hardware benchmark results to {out_path}")


if __name__ == "__main__":
    main()
