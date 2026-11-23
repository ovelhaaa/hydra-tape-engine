"""Reproducible, opt-in physical ESP32-S3 benchmark for Phase 7.4B.

Default invocation only verifies PlatformIO definitions. Flashing the board
requires --flash and an explicit connected serial port.
"""
import argparse
import configparser
import hashlib
import json
import os
import re
import subprocess
import time
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_PIO = r"C:\.platformio\penv\Scripts\pio.exe"
BAUD = 115200
SR = 44100
BLOCK = 128

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
    ("3_multihead_reverse_spring", [
        "byp 0", "gen 1", "src 1",
        "rev 1", "revmode 0", "revdiv 0", "wrp 0", "spr 1",
        "hds 7", "dly 360", "fbk 75",
        "wwd 48", "wwr 0.95", "ftd 45", "ftr 6.8", "tpa 65",
        "tempo 120", "transport play",
    ]),
]


def env_name(mode):
    if mode not in (0, 1, 2, 3):
        raise ValueError(f"Unsupported mode M{mode}")
    return f"esp32-s3-phase74b-m{mode}"


def verify_build_config():
    """Check that all M0-M3 configurations extend the production Classic C flags."""
    cfg = configparser.ConfigParser(interpolation=None)
    if not cfg.read(ROOT / "platformio.ini", encoding="utf-8"):
        raise RuntimeError("platformio.ini not found")
    common = cfg.get("hydra_s3_common", "build_flags")
    production = cfg.get("env:esp32-s3-devkitc-1", "build_flags")
    for flag in ("-O2", "-ffast-math", "-DBOARD_HAS_PSRAM", "-DARDUINO_USB_MODE=1"):
        if flag not in common:
            raise RuntimeError(f"Missing common production build flag: {flag}")
    if "${hydra_s3_common.build_flags}" not in production:
        raise RuntimeError("Production flags do not inherit hydra_s3_common")
    if "-DHYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING=1" not in production:
        raise RuntimeError("Production Classic C routing flag is absent")
    defs = {}
    for mode in range(4):
        name = env_name(mode)
        if cfg.get("env:" + name, "extends") != "env:esp32-s3-devkitc-1":
            raise RuntimeError(f"{name} does not extend the production environment")
        flags = cfg.get("env:" + name, "build_flags")
        if "${env:esp32-s3-devkitc-1.build_flags}" not in flags:
            raise RuntimeError(f"{name} does not inherit the complete production build_flags")
        if f"-DHYDRA_PHASE74B_MOTION_MODE={mode}" not in flags:
            raise RuntimeError(f"{name} is not explicitly selecting M{mode}")
        defs[name] = flags.strip()
    return defs


def build_and_upload(mode, pio, port, out_dir):
    name = env_name(mode)
    environ = os.environ.copy()
    # No override of PLATFORMIO_BUILD_FLAGS: that would replace build_flags
    # instead of appending to the production flags.
    environ.pop("PLATFORMIO_BUILD_FLAGS", None)
    # Clean separately so the verbose log includes compiler commands for inspection.
    cmds = [
        [pio, "run", "-e", name, "-t", "clean"],
        [pio, "run", "-e", name, "-t", "upload", "--upload-port", port, "-v"],
    ]
    outputs = []
    for cmd in cmds:
        res = subprocess.run(cmd, cwd=ROOT, env=environ,
                             capture_output=True, text=True, errors="replace")
        outputs.append("$ " + " ".join(cmd) + "\n" + res.stdout + "\n" + res.stderr)
        if res.returncode != 0:
            (out_dir / f"{name}-build-upload.log").write_text(
                "\n".join(outputs), encoding="utf-8")
            raise RuntimeError(f"{name}: PlatformIO failed (exit {res.returncode})")
    log = "\n".join(outputs)
    log_path = out_dir / f"{name}-build-upload.log"
    log_path.write_text(log, encoding="utf-8")
    # Enforce effective compiler flags, not just INI inheritance.
    for flag in ("-DHYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING=1",
                 f"-DHYDRA_PHASE74B_MOTION_MODE={mode}", "-DBOARD_HAS_PSRAM",
                 "-O2", "-ffast-math"):
        if flag not in log:
            raise RuntimeError(f"{name}: compiler log missing effective flag {flag}; see {log_path}")
    firmware = ROOT / ".pio" / "build" / name / "firmware.bin"
    if not firmware.is_file():
        raise RuntimeError(f"Missing firmware binary for {name}")
    return {
        "environment": name,
        "build_log": str(log_path.relative_to(ROOT)),
        "build_log_sha256": hashlib.sha256(log_path.read_bytes()).hexdigest(),
        "firmware_sha256": hashlib.sha256(firmware.read_bytes()).hexdigest(),
    }


def wait_for_serial(port, timeout_s=20.0):
    import serial
    started = time.monotonic()
    while time.monotonic() - started < timeout_s:
        try:
            stream = serial.Serial(port, BAUD, timeout=2)
            time.sleep(2)
            stream.reset_input_buffer()
            return stream
        except serial.SerialException:
            time.sleep(0.5)
    raise RuntimeError(f"Serial port unavailable: {port}")


def send_cmd(stream, cmd):
    stream.write((cmd + "\n").encode("latin1"))
    time.sleep(0.08)
    while stream.in_waiting:
        stream.read(stream.in_waiting)
        time.sleep(0.02)


def run_bench(stream, blocks=1000, warmup=100, timeout=20.0):
    stream.reset_input_buffer()
    stream.write(f"bench {blocks} {warmup}\n".encode("latin1"))
    lines = []
    header = False
    started = time.monotonic()
    while time.monotonic() - started < timeout:
        raw = stream.readline()
        if not raw:
            continue
        line = raw.decode("latin1", errors="replace").strip()
        if line:
            lines.append(line)
            if "BENCH RESULT" in line:
                header = True
            elif header and line.startswith("========================================="):
                return "\n".join(lines)
    raise RuntimeError("Timed out without a complete BENCH RESULT")


def parse_bench(output):
    parsed = {}
    for line in output.splitlines():
        line = line.strip()
        if line.startswith(("MeanUs=", "BudgetMeanPct=")):
            for k, v in re.findall(r"([A-Za-z0-9]+)=([0-9.]+)%?", line):
                parsed[k] = float(v)
        if line.startswith("BudgetMeanPct="):
            over = re.search(r"Overruns=(\d+)", line)
            if over:
                parsed["Overruns"] = int(over.group(1))
        if "HeapStart=" in line:
            for k, v in re.findall(r"(Heap[A-Za-z0-9]+)=(\d+)", line):
                parsed[k] = int(v)
    required = {"MeanUs", "P99Us", "PeakUs", "BudgetMeanPct", "BudgetP99Pct", "Overruns"}
    missing = required - parsed.keys()
    if missing:
        raise RuntimeError(f"Incomplete bench output: missing {sorted(missing)}")
    return parsed


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--flash", action="store_true", help="Explicit permission to flash and benchmark all four variants")
    ap.add_argument("--pio", default=os.environ.get("HYDRA_PIO", DEFAULT_PIO))
    ap.add_argument("--port", default=os.environ.get("HYDRA_SERIAL_PORT", "COM15"))
    args = ap.parse_args()

    configs = verify_build_config()
    print("PASS: M0-M3 PlatformIO configurations inherit Classic C and all common flags")
    for name in configs:
        print("  " + name)
    if not args.flash:
        print("No upload performed. Re-run with --flash only when hardware flashing is authorized.")
        return

    session = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    out_dir = ROOT / "artifacts" / "phase7-4b" / "physical-logs" / session
    out_dir.mkdir(parents=True, exist_ok=True)
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    result = {
        "_metadata": {
            "utc": session,
            "git_sha": revision,
            "serial_port": args.port,
            "sample_rate": SR,
            "block_frames": BLOCK,
            "deadline_us": BLOCK / SR * 1e6,
            "blocks": 1000,
            "warmup_blocks": 100,
            "build_envs": configs,
        }
    }
    # Keep original Classic C firmware on the device after the experiment.
    for mode in (1, 2, 3, 0):
        build = build_and_upload(mode, args.pio, args.port, out_dir)
        stream = wait_for_serial(args.port)
        try:
            tests = {}
            for scenario, commands in SCENARIOS:
                for cmd in commands:
                    send_cmd(stream, cmd)
                time.sleep(0.25)
                raw = run_bench(stream)
                raw_path = out_dir / f"m{mode}-{scenario}-serial.log"
                raw_path.write_text(raw + "\n", encoding="utf-8")
                metrics = parse_bench(raw)
                metrics["raw_serial_log"] = str(raw_path.relative_to(ROOT))
                metrics["raw_serial_sha256"] = hashlib.sha256(raw_path.read_bytes()).hexdigest()
                tests[scenario] = metrics
                print(f"M{mode} / {scenario}: mean={metrics['MeanUs']:.2f} us, "
                      f"p99={metrics['P99Us']:.0f} us, peak={metrics['PeakUs']:.0f} us, "
                      f"overruns={metrics['Overruns']}")
            result[f"M{mode}"] = {"build": build, "scenarios": tests}
        finally:
            stream.close()
    dest = ROOT / "artifacts" / "phase7-4b" / "esp32_s3_benchmark_verified.json"
    dest.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(f"Saved verified physical benchmark and raw serial evidence to {dest}")


if __name__ == "__main__":
    main()
