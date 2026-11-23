"""Physical ESP32-S3 benchmark for Phase 7.4C Unified Tape Motion."""
import hashlib
import json
import re
import time
from datetime import datetime, timezone
from pathlib import Path

import serial

ROOT = Path(__file__).resolve().parent.parent
PORT = "COM15"
BAUD = 115200
OUT_DIR = ROOT / "artifacts" / "phase7-4c" / "physical"
FIRMWARE_BIN = ROOT / ".pio" / "build" / "esp32-s3-phase74c-unified" / "firmware.bin"

SCENARIOS = [
    ("1_standard_single_head", [
        "byp 0", "gen 1", "src 1",
        "rev 0", "wrp 0", "spr 0",
        "hds 4", "dly 340", "fbk 50",
        "wwd 25", "wwr 0.8", "ftd 25", "ftr 6.0", "tpa 40",
        "motion amt 100", "motion stereo 100",
    ]),
    ("2_multihead_spring", [
        "byp 0", "gen 1", "src 1",
        "rev 0", "wrp 0", "spr 1",
        "hds 7", "dly 340", "fbk 65",
        "wwd 30", "wwr 0.8", "ftd 28", "ftr 6.0", "tpa 50",
        "motion amt 100", "motion stereo 100",
    ]),
    ("3_multihead_reverse_spring", [
        "byp 0", "gen 1", "src 1",
        "rev 1", "revmode 0", "revdiv 0", "wrp 0", "spr 1",
        "hds 7", "dly 360", "fbk 75",
        "wwd 35", "wwr 0.95", "ftd 40", "ftr 6.8", "tpa 65",
        "tempo 120", "transport play",
        "motion amt 100", "motion stereo 100",
    ]),
]


def drain_lines(ser, wait_s=0.35):
    lines = []
    deadline = time.monotonic() + wait_s
    while time.monotonic() < deadline:
        raw = ser.readline()
        if raw:
            lines.append(raw.decode("utf-8", errors="replace").rstrip("\r\n"))
            deadline = time.monotonic() + 0.15
    return lines


def send_cmd(ser, cmd, wait_s=0.18):
    ser.write((cmd + "\n").encode("ascii"))
    ser.flush()
    time.sleep(wait_s)
    return drain_lines(ser, 0.08)


def run_bench(ser, blocks=1024, warmup=256, scheduled_events=None):
    send_cmd(ser, "bench_sched clear", 0.08)
    if scheduled_events:
        for block, param_id, value in scheduled_events:
            ack = send_cmd(ser, f"bench_sched add {block} {param_id} {value}", 0.05)
            if not any("BENCH_SCHED OK" in line for line in ack):
                raise RuntimeError(f"Failed to schedule bench event {(block, param_id, value)}: {ack}")

    ser.reset_input_buffer()
    ser.write(f"bench {blocks} {warmup}\n".encode("ascii"))
    ser.flush()

    captured = []
    deadline = time.monotonic() + 30.0
    got_result = False
    while time.monotonic() < deadline:
        raw = ser.readline()
        if not raw:
            if got_result:
                break
            continue
        line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
        captured.append(line)
        if "=========================================" in line:
            got_result = True
            deadline = time.monotonic() + 0.3

    text = "\n".join(captured)
    m_mean = re.search(
        r"MeanUs=([0-9.]+)\s+MinUs=(\d+)\s+P50Us=(\d+)\s+P95Us=(\d+)\s+P99Us=(\d+)\s+PeakUs=(\d+)",
        text,
    )
    m_ov = re.search(r"BudgetMeanPct=([0-9.]+)%\s+BudgetP99Pct=([0-9.]+)%\s+Overruns=(\d+)", text)
    m_mem = re.search(r"I2SFailDelta=(\d+)\s+HeapStart=(\d+)\s+HeapEnd=(\d+)\s+HeapMin=(\d+)\s+PsramStart=(\d+)\s+PsramEnd=(\d+)\s+MaxParamUpdUs=(\d+)", text)
    m_auto = re.search(r"AutoScheduled=(\d+)\s+AutoApplied=(\d+)", text)
    if not (m_mean and m_ov):
        raise RuntimeError(f"Failed to parse bench output:\n{text}")
    auto_scheduled = int(m_auto.group(1)) if m_auto else 0
    auto_applied = int(m_auto.group(2)) if m_auto else 0
    if scheduled_events and auto_applied != len(scheduled_events):
        raise RuntimeError(
            f"Expected {len(scheduled_events)} scheduled automation events applied during bench, got {auto_applied}/{auto_scheduled}"
        )
    return {
        "mean_us": float(m_mean.group(1)),
        "min_us": int(m_mean.group(2)),
        "p50_us": int(m_mean.group(3)),
        "p95_us": int(m_mean.group(4)),
        "p99_us": int(m_mean.group(5)),
        "peak_us": int(m_mean.group(6)),
        "budget_mean_pct": float(m_ov.group(1)),
        "budget_p99_pct": float(m_ov.group(2)),
        "overruns": int(m_ov.group(3)),
        "i2s_fail_delta": int(m_mem.group(1)) if m_mem else None,
        "heap_min": int(m_mem.group(4)) if m_mem else None,
        "psram_end": int(m_mem.group(6)) if m_mem else None,
        "max_param_upd_us": int(m_mem.group(7)) if m_mem else None,
        "auto_scheduled": auto_scheduled,
        "auto_applied": auto_applied,
        "raw": text,
    }


def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    fw_sha = hashlib.sha256(FIRMWARE_BIN.read_bytes()).hexdigest()

    results = {
        "timestamp_utc": datetime.now(timezone.utc).isoformat(),
        "port": PORT,
        "environment": "esp32-s3-phase74c-unified",
        "firmware_sha256": fw_sha,
        "block_budget_us": 2902.49,
        "modes": {},
        "transitions_and_automation": {},
    }
    raw_log = [f"Firmware SHA256: {fw_sha}", f"Port: {PORT}"]

    with serial.Serial(PORT, BAUD, timeout=0.15) as ser:
        time.sleep(1.5)
        boot_lines = drain_lines(ser, 1.0)
        raw_log.extend(boot_lines)

        # Verify motion CLI responds
        ack = send_cmd(ser, "motion")
        raw_log.extend(ack)

        for mode in (0, 1, 2, 3):
            mode_key = f"M{mode}"
            results["modes"][mode_key] = {}
            ack = send_cmd(ser, f"motion {mode}")
            raw_log.extend(ack)
            ver = send_cmd(ser, "motion")
            raw_log.extend(ver)

            for scen_name, cmds in SCENARIOS:
                for c in cmds:
                    raw_log.extend(send_cmd(ser, c, 0.06))
                time.sleep(0.25)
                res = run_bench(ser, blocks=1024, warmup=256)
                raw_log.append(f"[{mode_key} / {scen_name}]\n{res['raw']}")
                results["modes"][mode_key][scen_name] = {
                    k: v for k, v in res.items() if k != "raw"
                }
                print(
                    f"{mode_key} | {scen_name:28s} | mean={res['mean_us']:7.2f}us "
                    f"p99={res['p99_us']:4d}us peak={res['peak_us']:4d}us "
                    f"({res['budget_mean_pct']:5.2f}%) overruns={res['overruns']}"
                )

        # Transition cost + simultaneous automation under Multi-Head + Reverse + Spring
        # Scheduled directly in firmware on measured block indices so updates run inside BENCH_RUNNING.
        for c in SCENARIOS[2][1]:
            send_cmd(ser, c, 0.05)
        trans_res = run_bench(
            ser,
            blocks=1024,
            warmup=128,
            scheduled_events=[
                (64, 38, 1),     # motion 1
                (64, 39, 80),    # motion amt 80
                (64, 40, 60),    # motion stereo 60
                (256, 38, 2),    # motion 2
                (256, 1, 28),    # wowDepth 28
                (256, 0, 32),    # flutterDepth 32
                (512, 38, 3),    # motion 3
                (512, 13, 80),   # feedback 80
                (512, 12, 420),  # delayTimeMs 420
                (768, 38, 0),    # motion 0
                (768, 39, 100),  # motion amt 100
                (768, 40, 100),  # motion stereo 100
            ],
        )
        raw_log.append(f"[runtime_transitions_and_automation]\n{trans_res['raw']}")
        results["transitions_and_automation"] = {
            k: v for k, v in trans_res.items() if k != "raw"
        }
        print(
            f"TRANSITIONS+AUTO | mean={trans_res['mean_us']:7.2f}us "
            f"p99={trans_res['p99_us']:4d}us peak={trans_res['peak_us']:4d}us "
            f"overruns={trans_res['overruns']}"
        )

    (OUT_DIR / "benchmark_results.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
    (OUT_DIR / "serial_log.txt").write_text("\n".join(raw_log), encoding="utf-8")
    print(f"Saved results to {OUT_DIR}")


if __name__ == "__main__":
    main()
