#!/usr/bin/env python3
"""Independent bit-exact equivalence check: Phase 7.4A C0 vs PR #58 Classic C."""

import argparse
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


DEFAULT_REF_SHA = "e81511707d4205221cbd634da0d9b90c0b0c9d64"


def find_compiler(explicit: str | None) -> str:
    if explicit:
        return explicit
    for candidate in ("g++", "clang++", "c++"):
        found = shutil.which(candidate)
        if found:
            return found
    raise RuntimeError("No C++ compiler found (tried g++, clang++, c++)")


def run(cmd: list[str], cwd: Path | None = None) -> str:
    res = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if res.returncode != 0:
        raise RuntimeError(
            f"Command failed ({res.returncode}): {' '.join(cmd)}\n"
            f"STDOUT:\n{res.stdout}\nSTDERR:\n{res.stderr}"
        )
    return res.stdout.strip()


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--ref-sha", default=DEFAULT_REF_SHA)
    parser.add_argument("--compiler", default=None)
    parser.add_argument("--report-json", default=None)
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parents[2]
    compiler = find_compiler(args.compiler)
    compiler_ver = run([compiler, "--version"]).splitlines()[0]
    ref_sha = run(["git", "rev-parse", args.ref_sha], cwd=repo_root)
    exp_sha = run(["git", "rev-parse", "HEAD"], cwd=repo_root)

    common_flags = [
        "-O2",
        "-std=c++17",
        "-ffp-contract=off",
        "-DHYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING=1",
        "-DHYDRA_PHASE74_REPEAT_AGING_MODE=0",
    ]

    with tempfile.TemporaryDirectory(prefix="hydra_c0_bitexact_") as tmp:
        tmp_path = Path(tmp)
        ref_src = tmp_path / "ref_src"
        ref_src.mkdir()
        tar_path = tmp_path / "ref.tar"
        run(["git", "archive", "--format=tar", f"--output={tar_path}", ref_sha, "dsp", "include"], cwd=repo_root)
        run(["tar", "-xf", str(tar_path), "-C", str(ref_src)])

        exe_suffix = ".exe" if os.name == "nt" else ""
        ref_exe = tmp_path / f"ref_runner{exe_suffix}"
        exp_exe = tmp_path / f"exp_runner{exe_suffix}"
        runner_cpp = repo_root / "tests" / "core" / "c0_equivalence_runner.cpp"

        ref_cmd = [
            compiler,
            *common_flags,
            f"-I{ref_src / 'include'}",
            f"-I{ref_src / 'dsp' / 'core'}",
            str(runner_cpp),
            str(ref_src / "dsp" / "core" / "tape_core.cpp"),
            str(ref_src / "dsp" / "core" / "hydra_dsp_c_api.cpp"),
            "-o",
            str(ref_exe),
        ]
        exp_cmd = [
            compiler,
            *common_flags,
            f"-I{repo_root / 'include'}",
            f"-I{repo_root / 'dsp' / 'core'}",
            str(runner_cpp),
            str(repo_root / "dsp" / "core" / "tape_core.cpp"),
            str(repo_root / "dsp" / "core" / "hydra_dsp_c_api.cpp"),
            "-o",
            str(exp_exe),
        ]

        run(ref_cmd)
        run(exp_cmd)

        ref_out = tmp_path / "ref_out"
        exp_out = tmp_path / "exp_out"
        ref_out.mkdir()
        exp_out.mkdir()

        run([str(ref_exe), str(ref_out)])
        run([str(exp_exe), str(exp_out)])

        ref_files = sorted(ref_out.glob("*.f32"))
        exp_files = sorted(exp_out.glob("*.f32"))
        if [p.name for p in ref_files] != [p.name for p in exp_files] or len(ref_files) != 10:
            raise RuntimeError("Mismatch in scenario output files")

        scenarios_report = []
        all_bit_exact = True
        for rf, ef in zip(ref_files, exp_files):
            rb = rf.read_bytes()
            eb = ef.read_bytes()
            exact = rb == eb
            if not exact:
                all_bit_exact = False
            max_abs = 0.0
            diff_samples = 0
            if not exact:
                count = min(len(rb), len(eb)) // 4
                r_vals = struct.unpack(f"<{count}f", rb[: count * 4])
                e_vals = struct.unpack(f"<{count}f", eb[: count * 4])
                for a, b in zip(r_vals, e_vals):
                    d = abs(a - b)
                    if a != b:
                        diff_samples += 1
                    if d > max_abs:
                        max_abs = d
            scenarios_report.append(
                {
                    "scenario": rf.stem,
                    "bitExact": exact,
                    "bytes": len(rb),
                    "refSha256": sha256_file(rf),
                    "expSha256": sha256_file(ef),
                    "diffSamples": diff_samples,
                    "maxAbsDiff": max_abs,
                }
            )

        report = {
            "status": "C0_BIT_EXACT_PASS" if all_bit_exact else "C0_BIT_EXACT_FAIL",
            "referenceSha": ref_sha,
            "experimentalSha": exp_sha,
            "compiler": compiler_ver,
            "effectiveFlags": common_flags,
            "sampleRate": 48000,
            "blockSize": 128,
            "seeds": {
                "noiseGenL": 123456789,
                "noiseGenR": 987654321,
                "mechStateR": ["0x2468ACE1u", "0x56473829u"],
            },
            "scenarios": scenarios_report,
        }

        if args.report_json:
            Path(args.report_json).parent.mkdir(parents=True, exist_ok=True)
            Path(args.report_json).write_text(json.dumps(report, indent=2), encoding="utf-8")

        print(json.dumps(report, indent=2))
        if not all_bit_exact:
            return 1

    print("C0 BIT-EXACT PASS: All 10 scenarios match PR #58 Classic C byte-for-byte.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
