#!/usr/bin/env python3
"""Verify M0 (HYDRA_PHASE74B_MOTION_MODE=0) is bit-exact with PR #58 Classic C."""

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def compile_and_run(compiler: str, repo_root: Path, runner_src: Path, out_bin: Path, extra_flags: list[str]) -> str:
    cmd = [
        compiler,
        "-std=c++17",
        "-O2",
        "-ffp-contract=off",
        "-DHYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING=1",
        *extra_flags,
        "-I",
        str(repo_root / "include"),
        "-I",
        str(repo_root / "dsp" / "core"),
        str(runner_src),
        str(repo_root / "dsp" / "core" / "tape_core.cpp"),
        str(repo_root / "dsp" / "core" / "hydra_dsp_c_api.cpp"),
        "-o",
        str(out_bin),
    ]
    subprocess.run(cmd, check=True)
    res = subprocess.run([str(out_bin)], check=True, capture_output=True, text=True)
    return res.stdout.strip()


def find_compiler() -> str:
    for cand in ("g++", "clang++", "c++"):
        path = shutil.which(cand)
        if path:
            return path
    raise RuntimeError("No C++ compiler found in PATH")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--ref-sha", default="d2d36dd")
    parser.add_argument("--report-json", default="")
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parents[2]
    runner_src = repo_root / "tests" / "core" / "m0_equivalence_runner.cpp"
    compiler = find_compiler()

    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = Path(tmp)
        ref_dir = tmp_path / "ref"
        subprocess.run(
            ["git", "worktree", "add", "--detach", str(ref_dir), args.ref_sha],
            cwd=repo_root,
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        try:
            exe_ext = ".exe" if os.name == "nt" else ""
            ref_bin = tmp_path / f"m0_ref{exe_ext}"
            cur_bin = tmp_path / f"m0_cur{exe_ext}"
            ref_out = compile_and_run(compiler, ref_dir, runner_src, ref_bin, [])
            cur_out = compile_and_run(
                compiler, repo_root, runner_src, cur_bin, ["-DHYDRA_PHASE74B_MOTION_MODE=0"]
            )
        finally:
            subprocess.run(
                ["git", "worktree", "remove", "--force", str(ref_dir)],
                cwd=repo_root,
                check=False,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )

    match = ref_out == cur_out
    report = {
        "refSha": args.ref_sha,
        "compiler": compiler,
        "refOutput": ref_out,
        "m0Output": cur_out,
        "bitExact": match,
    }
    if args.report_json:
        out_p = Path(args.report_json)
        out_p.parent.mkdir(parents=True, exist_ok=True)
        out_p.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    print(json.dumps(report, indent=2))
    if not match:
        print("ERROR: M0 output does NOT match PR #58 Classic C!", file=sys.stderr)
        return 1
    print("PASS: M0 is bit-exact with PR #58 Classic C.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
