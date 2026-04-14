#!/usr/bin/env python3
import argparse
import os
import subprocess
import tempfile
from pathlib import Path


SCENARIOS = {
    'delay_modes': {
        'input': 'sweep_log.csv',
        'max_abs_threshold': 1e-5,
        'rmse_threshold': 1e-6,
    },
    'reset_state': {
        'input': 'impulse.csv',
        'max_abs_threshold': 1e-5,
        'rmse_threshold': 1e-6,
    },
    'param_automation': {
        'input': 'music_excerpt.csv',
        'max_abs_threshold': 1e-5,
        'rmse_threshold': 1e-6,
    },
}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--runner', required=True, help='Path to native binary or JS bundle')
    parser.add_argument('--scenario', required=True, choices=SCENARIOS.keys())
    parser.add_argument('--fixtures-root', required=True)
    parser.add_argument('--compare-script', required=True)
    parser.add_argument('--wasm', action='store_true', help='Execute runner with node')
    args = parser.parse_args()

    fixtures_root = Path(args.fixtures_root)
    scenario_cfg = SCENARIOS[args.scenario]
    input_file = fixtures_root / 'input' / scenario_cfg['input']
    golden_file = fixtures_root / 'golden' / 'native' / f'{args.scenario}.csv'

    with tempfile.TemporaryDirectory() as tmpdir:
        candidate_file = Path(tmpdir) / f'{args.scenario}.csv'
        cmd = [args.runner, '--scenario', args.scenario, '--input', str(input_file), '--output', str(candidate_file)]
        if args.wasm:
            cmd = ['node'] + cmd

        subprocess.check_call(cmd, env={**os.environ, 'LC_ALL': 'C'})

        compare_cmd = [
            args.compare_script,
            '--reference',
            str(golden_file),
            '--candidate',
            str(candidate_file),
            '--max-abs-threshold',
            str(scenario_cfg['max_abs_threshold']),
            '--rmse-threshold',
            str(scenario_cfg['rmse_threshold']),
        ]
        return subprocess.call(compare_cmd)


if __name__ == '__main__':
    raise SystemExit(main())
