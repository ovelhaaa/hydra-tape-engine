#!/usr/bin/env python3
import argparse
import subprocess
import sys

EXPECTED = {
    "hydra_dsp_get_api_version",
    "hydra_dsp_get_param_count",
    "hydra_dsp_get_param_specs",
    "hydra_dsp_get_param_spec",
    "hydra_dsp_get_param_spec_for_handle",
    "hydra_dsp_create",
    "hydra_dsp_destroy",
    "hydra_dsp_prepare",
    "hydra_dsp_reset",
    "hydra_dsp_set_parameter",
    "hydra_dsp_get_parameter",
    "hydra_dsp_set_params",
    "hydra_dsp_commit",
    "hydra_dsp_process",
}


def parse_symbols(nm_output: str) -> set[str]:
    symbols: set[str] = set()
    for line in nm_output.splitlines():
        parts = line.strip().split()
        if not parts:
            continue
        symbol = parts[-1]
        symbols.add(symbol)
        if symbol.startswith("_"):
            symbols.add(symbol[1:])
    return symbols


def main() -> int:
    parser = argparse.ArgumentParser(description="Sanity-check C ABI symbols exposed by hydra_dsp library")
    parser.add_argument("--nm", required=True)
    parser.add_argument("--library", required=True)
    args = parser.parse_args()

    proc = subprocess.run(
        [args.nm, "-g", "--defined-only", args.library],
        capture_output=True,
        text=True,
        check=False,
    )
    if proc.returncode != 0:
        sys.stderr.write(proc.stderr)
        return proc.returncode

    symbols = parse_symbols(proc.stdout)
    missing = sorted(s for s in EXPECTED if s not in symbols)
    if missing:
        sys.stderr.write("Missing ABI symbols:\n")
        for sym in missing:
            sys.stderr.write(f"  - {sym}\n")
        return 1

    print("ABI symbol sanity check passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
