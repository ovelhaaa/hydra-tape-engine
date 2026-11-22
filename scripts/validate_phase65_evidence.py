"""Validate archived physical evidence offline; this never claims to rerun hardware."""
import argparse
import hashlib
import json
from pathlib import Path
import re

from benchmark_reverse_hardware import parse_bench


SCENARIOS = ('delay', 'reverse-quarter', 'reverse-eighth', 'reverse-sixteenth',
             'reverse-feedback95', 'multihead', 'stress')


def validate(directory):
    manifest = json.loads((directory / 'manifest.json').read_text(encoding='utf-8'))
    actual_files = {str(p.relative_to(directory)).replace('\\', '/')
                    for p in directory.rglob('*') if p.is_file() and p.name != 'manifest.json'}
    if actual_files != set(manifest['sha256']):
        raise ValueError('Manifest file set mismatch')
    for name, expected in manifest['sha256'].items():
        target = (directory / name).resolve()
        if not target.is_relative_to(directory.resolve()):
            raise ValueError('Manifest path escapes evidence directory')
        if hashlib.sha256(target.read_bytes()).hexdigest() != expected:
            raise ValueError('SHA-256 mismatch: ' + name)
    r = json.loads((directory / 'final-results.json').read_text(encoding='utf-8'))
    if r['status'] != 'MEASURED_GATES_PASS_AUDIO_CAPTURE_PENDING' or r.get('cleanup_errors'):
        raise ValueError('Capture did not finish successfully')
    if set(r['scenarios']) != set(SCENARIOS):
        raise ValueError('Scenario set mismatch')
    for name in SCENARIOS:
        measured = r['scenarios'][name]
        raw = (directory / 'benchmarks' / (name + '.txt')).read_text(encoding='utf-8')
        parsed = parse_bench(raw)
        for key in ('mean_us', 'p50_us', 'p95_us', 'p99_us', 'peak_us',
                    'overruns', 'i2s_fail_delta', 'heap_start', 'heap_end',
                    'psram_start', 'psram_end', 'blocks'):
            if measured[key] != parsed[key]:
                raise ValueError(name + ': raw/JSON mismatch for ' + key)
        if parsed['blocks'] < 10000 or not re.search(r'Warmup=256\b', raw):
            raise ValueError(name + ': incomplete acquisition')
        if (not measured['pass'] or parsed['overruns'] or parsed['i2s_fail_delta']
                or parsed['peak_us'] >= 128 / 44100 * 1e6):
            raise ValueError(name + ': deadline/I2S gate failed')
        health = r['audio_health'][name]
        if not health['NonZeroWords'] or health['StackScanUs'] or health['FullWorkUs'] >= 128 / 44100 * 1e6:
            raise ValueError(name + ': invalid PCM/work snapshot')
    samples = r['sustain']
    if len(samples) < 2 or samples[-1]['elapsed_seconds'] < 300:
        raise ValueError('Sustain shorter than five minutes')
    for sample in [r['initial_perf'], r['stress_perf'], *samples]:
        if any(sample[k] for k in ('overruns', 'i2s_failures', 'full_work_overruns')):
            raise ValueError('Runtime counter failure')
        if sample['stack_min_bytes'] < 4096:
            raise ValueError('Insufficient stack margin')
    for previous, current in zip(samples, samples[1:]):
        if current['blocks'] <= previous['blocks'] or current['elapsed_seconds'] <= previous['elapsed_seconds']:
            raise ValueError('Stopped/reset/disordered capture')
    if r['sustain_blocks'] != samples[-1]['blocks'] - samples[0]['blocks']:
        raise ValueError('Sustain block count mismatch')
    if (samples[-1]['heap_free'] < samples[0]['heap_free']
            or samples[-1]['psram_free'] < samples[0]['psram_free']):
        raise ValueError('Sustain memory loss')
    fw = json.loads((directory / 'firmware.json').read_text(encoding='utf-8'))
    for label, expected in [('FirmwareCommit', fw['commit']),
                            ('SourceSHA256', fw['source_sha256']),
                            ('ELFSHA256', fw['elf_sha256'])]:
        if label + '=' + expected + '\n' not in r['identity']:
            raise ValueError('Running firmware identity mismatch: ' + label)
    if r['firmware_sha256'] != fw['binary_sha256']:
        raise ValueError('Firmware file identity mismatch')
    for attempt in r.get('previous_capture_attempts', []):
        name = attempt['file'].replace('\\', '/').split('/')[-1]
        original = directory / 'attempts' / name
        if hashlib.sha256(original.read_bytes()).hexdigest() != attempt['sha256']:
            raise ValueError('Prior failed acquisition was not preserved: ' + name)
    if r['generator_control']['off']['NonZeroWords'] != 0:
        raise ValueError('Disabled generator not silent in bypass')
    if not any(s['NonZeroWords'] for s in r['generator_control']['on']):
        raise ValueError('Enabled generator never produced PCM')
    for entry in r['commands']:
        if entry['command'] == 'audiohealth':
            if not re.search(r'TxBytes=1024 TxError=0\b', entry['response']) or 'SaturatedWords=0 ' not in entry['response']:
                raise ValueError('PCM snapshot transmission/clipping failure')
    print('PASS: archived physical evidence, seven complete scenarios, raw/JSON/SHA integrity, five-minute runtime')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('directory', type=Path)
    p.add_argument('--write-manifest', action='store_true', help='Package evidence before validation; never used by CI')
    a = p.parse_args()
    if a.write_manifest:
        sha = {str(f.relative_to(a.directory)).replace('\\', '/'): hashlib.sha256(f.read_bytes()).hexdigest()
               for f in sorted(a.directory.rglob('*')) if f.is_file() and f.name != 'manifest.json'}
        (a.directory / 'manifest.json').write_text(json.dumps({'sha256': sha}, indent=2), encoding='utf-8')
    validate(a.directory)


if __name__ == '__main__':
    main()
