#!/usr/bin/env python3
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
header = (ROOT / 'include' / 'hydra_dsp.h').read_text()
main_js = (ROOT / 'host' / 'web' / 'main.js').read_text()
processor_js = (ROOT / 'host' / 'web' / 'hydra-processor.js').read_text()

name_map = {
    'FLUTTER_DEPTH': 'flutterDepth',
    'WOW_DEPTH': 'wowDepth',
    'DROPOUT': 'dropoutSeverity',
    'DRIVE': 'drive',
    'NOISE': 'noise',
    'TAPE_SPEED': 'tapeSpeed',
    'TAPE_AGE': 'tapeAge',
    'HEAD_BUMP': 'headBumpAmount',
    'AZIMUTH': 'azimuthError',
    'FLUTTER_RATE': 'flutterRate',
    'WOW_RATE': 'wowRate',
    'DELAY_ACTIVE': 'delayActive',
    'DELAY_MS': 'delayTimeMs',
    'FEEDBACK': 'feedback',
    'DRY_WET': 'dryWet',
    'DELAY_WET': 'delayWet',
    'ACTIVE_HEADS': 'activeHeads',
    'BPM': 'bpm',
    'HEADS_MUSICAL': 'headsMusical',
    'GUITAR_FOCUS': 'guitarFocus',
    'TONE': 'tone',
    'PING_PONG': 'pingPong',
    'FREEZE': 'freeze',
    'REVERSE': 'reverse',
    'REVERSE_SMEAR': 'reverseSmear',
    'SPRING': 'spring',
    'SPRING_DECAY': 'springDecay',
    'SPRING_DAMPING': 'springDamping',
    'SPRING_MIX': 'springMix',
    'RESET': 'reset',
}

enum_pairs = re.findall(r'HYDRA_DSP_PARAM_([A-Z_]+)\s*=\s*(\d+)', header)
expected = {name_map[k]: int(v) for k, v in enum_pairs if k in name_map}
param_block_match = re.search(r'const PARAM = \{(?P<body>.*?)\};', main_js, re.S)
if not param_block_match:
    raise SystemExit('Could not find PARAM block in host/web/main.js')
actual = {k: int(v) for k, v in re.findall(r'([A-Za-z][A-Za-z0-9_]*)\s*:\s*(\d+)', param_block_match.group('body'))}

errors = []
for name, value in sorted(expected.items(), key=lambda item: item[1]):
    if actual.get(name) != value:
        errors.append(f'{name}: header={value} host/web/main.js={actual.get(name)}')
    if name != 'reset' and f'p_{value}' not in processor_js and name not in ('activeHeads', 'delayActive', 'headsMusical', 'guitarFocus', 'pingPong', 'freeze', 'reverse', 'reverseSmear', 'spring'):
        errors.append(f'{name}: p_{value} missing from hydra-processor.js AudioParam descriptors')

if errors:
    print('\n'.join(errors), file=sys.stderr)
    raise SystemExit(1)
print('web_param_ids_check: ok')
