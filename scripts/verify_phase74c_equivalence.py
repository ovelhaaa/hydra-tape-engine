#!/usr/bin/env python3
"""Independent M0 bit-exactness and M1/M2/M3 parity verification for Phase 7.4C."""

import argparse
import json
import math
import os
import shutil
import struct
import subprocess
import sys
import wave
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

RUNNER_CPP = r'''
#include "hydra_dsp.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static constexpr float kTwoPi = 6.28318530718f;

static void writeF32(const std::string& path, const std::vector<float>& L, const std::vector<float>& R) {
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) {
    std::fprintf(stderr, "Failed to open output: %s\n", path.c_str());
    std::exit(2);
  }
  const uint32_t n = static_cast<uint32_t>(L.size());
  std::fwrite(&n, sizeof(uint32_t), 1, f);
  std::fwrite(L.data(), sizeof(float), n, f);
  std::fwrite(R.data(), sizeof(float), n, f);
  std::fclose(f);
}

int main(int argc, char** argv) {
  if (argc < 4) {
    std::fprintf(stderr, "Usage: %s <scenario> <motion_mode_or_-1> <out.f32>\n", argv[0]);
    return 1;
  }
  const std::string scenario = argv[1];
  const int motionMode = std::atoi(argv[2]);
  const std::string outPath = argv[3];

  constexpr float kSampleRate = 48000.0f;
  constexpr uint32_t kBlock = 128;
  const uint32_t kBlocks = (scenario == "m1" || scenario == "m2" || scenario == "m3") ? 1500u : 240u; // 4.0s for parity, 0.64s for M0

  hydra_dsp_handle* h = nullptr;
  if (hydra_dsp_create_v1_4(kSampleRate, 2000.0f, 4000.0f, &h) != 0) return 3;
  if (hydra_dsp_prepare(h, kBlock, HYDRA_DSP_CHANNELS_STEREO) != 0) return 4;

  // Base parameters
  hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_DELAY_ACTIVE, 1.0f);
  hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_DELAY_MS, 180.0f);
  hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_FEEDBACK, 45.0f);
  hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_DRY_WET, 80.0f);
  hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_DELAY_WET, 80.0f);
  hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_ACTIVE_HEADS, 1.0f);
  hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_WOW_DEPTH, 22.0f);
  hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_WOW_RATE, 0.8f);
  hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_FLUTTER_DEPTH, 26.0f);
  hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_FLUTTER_RATE, 6.0f);
  hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_TAPE_AGE, 55.0f);
  hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_DRIVE, 35.0f);
  hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_NOISE, 0.0f);
  hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_AZIMUTH, 15.0f);
  hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_DROPOUT, 0.0f);

  if (scenario == "single_head") {
    hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_ACTIVE_HEADS, 1.0f);
  } else if (scenario == "multi_head") {
    hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_ACTIVE_HEADS, 7.0f);
    hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_HEADS_MUSICAL, 1.0f);
    hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_BPM, 120.0f);
  } else if (scenario == "reverse") {
    hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_REVERSE, 1.0f);
    hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_REVERSE_SMEAR, 1.0f);
  } else if (scenario == "ping_pong") {
    hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_PING_PONG, 1.0f);
  } else if (scenario == "spring") {
    hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_SPRING, 1.0f);
    hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_SPRING_DECAY, 70.0f);
    hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_SPRING_DAMPING, 45.0f);
    hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_SPRING_MIX, 55.0f);
  } else if (scenario == "high_feedback") {
    hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_FEEDBACK, 92.0f);
  } else if (scenario == "wow_flutter_mod") {
    hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_WOW_DEPTH, 32.0f);
    hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_WOW_RATE, 1.4f);
    hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_FLUTTER_DEPTH, 38.0f);
    hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_FLUTTER_RATE, 8.5f);
    hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_AZIMUTH, 45.0f);
    hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_TAPE_AGE, 75.0f);
  } else if (scenario == "m1" || scenario == "m2" || scenario == "m3") {
    hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_ACTIVE_HEADS, 4.0f);
    hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_DELAY_MS, 220.0f);
    hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_FEEDBACK, 50.0f);
    hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_DRY_WET, 100.0f);
    hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_DELAY_WET, 100.0f);
    hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_AZIMUTH, 0.0f);
  }

#ifdef HAVE_UNIFIED_MOTION
  if (motionMode >= 0) {
    hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_MOTION_MODE, static_cast<float>(motionMode));
    hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_MOTION_AMOUNT, 100.0f);
    hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_STEREO_MOTION, 100.0f);
    hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_MOTION_INERTIA, 50.0f);
    hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_MOTION_DRIFT_CHAR, 50.0f);
    hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_MOTION_TEXTURE, 50.0f);
    hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_MOTION_IRREGULARITY, 50.0f);
    hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_MOTION_INSTABILITY, 50.0f);
    hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_MOTION_TENSION_REC, 50.0f);
  }
#endif

  hydra_dsp_commit(h);
  hydra_dsp_reset(h);

  std::vector<float> inL(kBlock), inR(kBlock), outL(kBlock), outR(kBlock);
  std::vector<float> allL(kBlock * kBlocks), allR(kBlock * kBlocks);
  uint32_t sampleIdx = 0;

  for (uint32_t b = 0; b < kBlocks; ++b) {
    if (scenario == "automation") {
      const float frac = static_cast<float>(b) / static_cast<float>(kBlocks);
      hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_DELAY_MS, 120.0f + 180.0f * frac);
      hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_FEEDBACK, 30.0f + 50.0f * frac);
      hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_WOW_DEPTH, 10.0f + 20.0f * frac);
      hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_FLUTTER_DEPTH, 15.0f + 20.0f * frac);
      hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_TONE, 35.0f + 40.0f * frac);
      hydra_dsp_commit(h);
    }

    for (uint32_t i = 0; i < kBlock; ++i) {
      const float t = static_cast<float>(sampleIdx) / kSampleRate;
      const float env = (t < 2.5f) ? 0.35f : 0.0f;
      const float sL = env * (0.65f * std::sin(kTwoPi * 220.0f * t) +
                              0.25f * std::sin(kTwoPi * 330.0f * t) +
                              0.10f * std::sin(kTwoPi * 660.0f * t));
      const float sR = env * (0.65f * std::sin(kTwoPi * 220.0f * t + 0.1f) +
                              0.25f * std::sin(kTwoPi * 330.0f * t - 0.1f) +
                              0.10f * std::sin(kTwoPi * 660.0f * t));
      inL[i] = sL;
      inR[i] = sR;
      ++sampleIdx;
    }

    if (hydra_dsp_process(h, inL.data(), inR.data(), outL.data(), outR.data(), kBlock) != 0) return 5;
    std::memcpy(allL.data() + b * kBlock, outL.data(), kBlock * sizeof(float));
    std::memcpy(allR.data() + b * kBlock, outR.data(), kBlock * sizeof(float));
  }

  hydra_dsp_destroy(h);
  writeF32(outPath, allL, allR);
  return 0;
}
'''


def read_f32(path: Path):
    data = path.read_bytes()
    (n,) = struct.unpack_from('<I', data, 0)
    l = struct.unpack_from(f'<{n}f', data, 4)
    r = struct.unpack_from(f'<{n}f', data, 4 + n * 4)
    return l, r


def write_stereo_wav(path: Path, l, r, sample_rate=48000):
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), 'wb') as wf:
        wf.setnchannels(2)
        wf.setsampwidth(2)
        wf.setframerate(sample_rate)
        frames = bytearray()
        for sl, sr in zip(l, r):
            il = max(-32768, min(32767, int(round(max(-1.0, min(1.0, sl)) * 32767.0))))
            ir = max(-32768, min(32767, int(round(max(-1.0, min(1.0, sr)) * 32767.0))))
            frames.extend(struct.pack('<hh', il, ir))
        wf.writeframes(bytes(frames))


def extract_branch(ref: str, dest: Path):
    if dest.exists():
        shutil.rmtree(dest)
    dest.mkdir(parents=True, exist_ok=True)
    proc = subprocess.run(
        ['git', 'archive', ref, 'include', 'dsp'],
        cwd=str(REPO_ROOT),
        capture_output=True,
        check=True,
    )
    import io
    import tarfile
    with tarfile.open(fileobj=io.BytesIO(proc.stdout), mode='r:*') as tf:
        tf.extractall(dest, filter='data')


def compile_runner(src_root: Path, runner_cpp: Path, out_exe: Path, extra_defs=None):
    defs = ['-DHYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING=1'] + (extra_defs or [])
    cmd = [
        'g++',
        '-std=c++17',
        '-O2',
        '-ffp-contract=off',
        *defs,
        f'-I{src_root / "include"}',
        f'-I{src_root / "dsp" / "core"}',
        str(runner_cpp),
        str(src_root / 'dsp' / 'core' / 'tape_core.cpp'),
        str(src_root / 'dsp' / 'core' / 'hydra_dsp_c_api.cpp'),
        '-o',
        str(out_exe),
    ]
    subprocess.run(cmd, check=True)


def compute_metrics(ref_l, ref_r, uni_l, uni_r):
    n = len(ref_l)
    def stats(a, b):
        sum_a2 = sum(x * x for x in a)
        sum_b2 = sum(x * x for x in b)
        sum_d2 = sum((x - y) ** 2 for x, y in zip(a, b))
        sum_ab = sum(x * y for x, y in zip(a, b))
        rms_a = math.sqrt(sum_a2 / n)
        rms_b = math.sqrt(sum_b2 / n)
        rms_d = math.sqrt(sum_d2 / n)
        peak_a = max(abs(x) for x in a)
        peak_b = max(abs(x) for x in b)
        max_abs_d = max(abs(x - y) for x, y in zip(a, b))
        denom = math.sqrt(sum_a2 * sum_b2)
        corr = (sum_ab / denom) if denom > 1e-18 else 1.0
        return {
            'ref_rms': rms_a,
            'uni_rms': rms_b,
            'rms_diff': rms_d,
            'ref_peak': peak_a,
            'uni_peak': peak_b,
            'max_abs_diff': max_abs_d,
            'correlation': corr,
        }

    sl = stats(ref_l, uni_l)
    sr = stats(ref_r, uni_r)
    # Stereo width / L-R difference energy
    ref_lr = math.sqrt(sum((x - y) ** 2 for x, y in zip(ref_l, ref_r)) / n)
    uni_lr = math.sqrt(sum((x - y) ** 2 for x, y in zip(uni_l, uni_r)) / n)
    return {
        'L': sl,
        'R': sr,
        'ref_stereo_diff_rms': ref_lr,
        'uni_stereo_diff_rms': uni_lr,
        'max_abs_diff': max(sl['max_abs_diff'], sr['max_abs_diff']),
        'rms_diff': max(sl['rms_diff'], sr['rms_diff']),
        'min_correlation': min(sl['correlation'], sr['correlation']),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--classic-ref', default='origin/feat/phase7-3-classic-c-web-default')
    parser.add_argument('--organic-ref', default='origin/experiment/phase7-4b-organic-motion')
    parser.add_argument('--parity-wav-dir', default='')
    parser.add_argument('--json-out', default='')
    args = parser.parse_args()

    work = REPO_ROOT / 'build' / 'equiv-work'
    work.mkdir(parents=True, exist_ok=True)
    runner_cpp = work / 'runner.cpp'
    runner_cpp.write_text(RUNNER_CPP, encoding='utf-8')

    classic_src = work / 'classic_c'
    organic_src = work / 'organic_74b'
    extract_branch(args.classic_ref, classic_src)
    extract_branch(args.organic_ref, organic_src)

    exe_ext = '.exe' if os.name == 'nt' else ''
    classic_exe = work / f'classic_runner{exe_ext}'
    unified_exe = work / f'unified_runner{exe_ext}'
    compile_runner(classic_src, runner_cpp, classic_exe)
    compile_runner(REPO_ROOT, runner_cpp, unified_exe, ['-DHAVE_UNIFIED_MOTION=1'])

    m0_scenarios = [
        'single_head',
        'multi_head',
        'reverse',
        'ping_pong',
        'spring',
        'high_feedback',
        'wow_flutter_mod',
        'automation',
    ]

    m0_results = {}
    m0_bit_exact = True
    for sc in m0_scenarios:
        ref_f32 = work / f'm0_ref_{sc}.f32'
        uni_f32 = work / f'm0_uni_{sc}.f32'
        subprocess.run([str(classic_exe), sc, '-1', str(ref_f32)], check=True)
        subprocess.run([str(unified_exe), sc, '0', str(uni_f32)], check=True)
        ref_b = ref_f32.read_bytes()
        uni_b = uni_f32.read_bytes()
        exact = (ref_b == uni_b)
        rl, rr = read_f32(ref_f32)
        ul, ur = read_f32(uni_f32)
        metrics = compute_metrics(rl, rr, ul, ur)
        metrics['bit_exact'] = exact
        m0_results[sc] = metrics
        if not exact:
            m0_bit_exact = False
        print(f"[M0] {sc:16s} bit_exact={exact} max_abs_diff={metrics['max_abs_diff']:.3e}")

    print('M0_BIT_EXACT_PASS' if m0_bit_exact else 'M0_BIT_EXACT_FAIL')

    parity_results = {}
    parity_pass = True
    for mode_idx, name in [(1, 'm1'), (2, 'm2'), (3, 'm3')]:
        org_exe = work / f'organic_{name}{exe_ext}'
        compile_runner(organic_src, runner_cpp, org_exe, [f'-DHYDRA_PHASE74B_MOTION_MODE={mode_idx}'])
        ref_f32 = work / f'parity_ref_{name}.f32'
        uni_f32 = work / f'parity_uni_{name}.f32'
        subprocess.run([str(org_exe), name, '-1', str(ref_f32)], check=True)
        subprocess.run([str(unified_exe), name, str(mode_idx), str(uni_f32)], check=True)

        rl, rr = read_f32(ref_f32)
        ul, ur = read_f32(uni_f32)
        metrics = compute_metrics(rl, rr, ul, ur)
        bit_exact = (ref_f32.read_bytes() == uni_f32.read_bytes())
        metrics['bit_exact'] = bit_exact
        parity_results[name] = metrics

        if args.parity_wav_dir:
            wdir = Path(args.parity_wav_dir)
            write_stereo_wav(wdir / f'{name}-reference.wav', rl, rr)
            write_stereo_wav(wdir / f'{name}-unified.wav', ul, ur)

        ok = metrics['rms_diff'] < 1e-6 and metrics['min_correlation'] > 0.99999
        if not ok:
            parity_pass = False
        print(
            f"[PARITY] {name.upper()} bit_exact={bit_exact} "
            f"rms_diff={metrics['rms_diff']:.3e} corr={metrics['min_correlation']:.6f} "
            f"stereo_ref={metrics['ref_stereo_diff_rms']:.6f} stereo_uni={metrics['uni_stereo_diff_rms']:.6f}"
        )

    print('M1_M2_M3_PARITY_PASS' if parity_pass else 'M1_M2_M3_PARITY_FAIL')

    report = {
        'm0_bit_exact_pass': m0_bit_exact,
        'm0_scenarios': m0_results,
        'parity_pass': parity_pass,
        'parity': parity_results,
    }
    if args.json_out:
        Path(args.json_out).parent.mkdir(parents=True, exist_ok=True)
        Path(args.json_out).write_text(json.dumps(report, indent=2), encoding='utf-8')

    return 0 if (m0_bit_exact and parity_pass) else 1


if __name__ == '__main__':
    raise SystemExit(main())
