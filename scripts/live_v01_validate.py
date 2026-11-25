#!/usr/bin/env python3
"""Live matrix static preservation and frozen F3-A reference; no golden updates."""
import argparse
import json
from pathlib import Path
import sys
import numpy as np
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'docs/evidence/dsp-reform-f5'))
import f5bench as B

def main():
    p = argparse.ArgumentParser()
    p.add_argument('--build', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    engines = {}
    for pair in ('00', '10', '01', '11'):
        suffix = '.dll' if sys.platform == 'win32' else '.so'
        engines[pair] = B.Engine((a.build / ('libhydra_live_' + pair + suffix)).resolve(), hooks=False)
    frozen = json.loads((ROOT / 'docs/evidence/dsp-reform-f3/F3-A-manifest.json').read_text())
    scenes = json.loads((ROOT / 'docs/evidence/dsp-reform-f0/baseline_renders_manifest.json').read_text())['scenarios']
    result = {'scenarios': {}, 'static_matrix_bit_exact': True, 'frozen_F3_A_bit_exact': True}
    for name, scene in scenes.items():
        params = B.b.PARAMS.copy()
        for k, v in scene['params_overrides'].items(): params[int(k)] = v
        x = B.b.stimulus(scene)
        ys = {pair: B.b.process(e, x, params)[0] for pair, e in engines.items()}
        hashes = {pair: B.sha(y.astype('<f4').tobytes()) for pair, y in ys.items()}
        exact = all(np.array_equal(ys['00'], y) for y in ys.values())
        historical = hashes['00'] == frozen['scenarios'][name]['F3_A_output_sha256'] and B.sha(x.astype('<f4').tobytes()) == frozen['scenarios'][name]['input_sha256']
        result['scenarios'][name] = dict(output_sha256=hashes, matrix_bit_exact=exact, frozen_F3_A_bit_exact=historical)
        result['static_matrix_bit_exact'] &= exact
        result['frozen_F3_A_bit_exact'] &= historical
        print(name, exact, historical, flush=True)
    # F5 static musical head positioning, also compared without automation.
    x = B.bed(3)
    ys = {pair: B._render(e, x, B.BASE, 128, [], per_block_diag=False)[0] for pair,e in engines.items()}
    result['musical_static_bit_exact'] = all(np.array_equal(ys['00'], y) for y in ys.values())
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps(result, indent=2)+'\n')
    assert result['static_matrix_bit_exact'] and result['frozen_F3_A_bit_exact'] and result['musical_static_bit_exact'], result
if __name__ == '__main__': main()
