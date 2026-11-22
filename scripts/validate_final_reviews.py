"""Validate an actual final Listening Lab export; never infer final human approval."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

from package_listening_lab import validate

ROOT = Path(__file__).resolve().parents[1]


def validate_review(package, review_path, expected_commit):
    manifest, _ = validate(package)
    if manifest.get('suite') != 'Hydra Phase 6.4 Final A/B':
        raise ValueError('Not the final ten-pair package')
    if manifest['reference_commit'] != expected_commit:
        raise ValueError('Candidate commit mismatch')
    expected = {f'{i:02}_final_{i:02}' for i in range(1, 11)}
    if set(manifest['scenarios']) != expected or manifest['sample_rate_hz'] != 48000:
        raise ValueError('Unexpected final scenario IDs/sample rate')
    text = review_path.read_text(encoding='utf-8-sig')
    data = json.loads(text)
    if not isinstance(data, dict) or not isinstance(data.get('reviews'), dict):
        raise ValueError('Invalid review object')
    if set(data['reviews']) != expected or set(data.get('package_hashes', {})) != expected:
        raise ValueError('Exactly ten scenario reviews and hash triplets required')
    for item in data['reviews'].values():
        if (not isinstance(item, dict) or not isinstance(item.get('criteria'), dict)
                or not isinstance(item.get('notes'), list)
                or not isinstance(item.get('comments'), str)):
            raise ValueError('Invalid scenario review schema')
    # Reuse the player schema, including criteria and timestamped note validation.
    module = (ROOT / 'tools/phase6-listening-lab/engine.mjs').as_uri()
    script = (f"import {{importReviews}} from {json.dumps(module)};"
              "let s='';for await(const c of process.stdin)s+=c;"
              "const d=JSON.parse(s);importReviews(d.review,d.manifest);")
    subprocess.run(['node', '--input-type=module', '-e', script],
                   input=json.dumps({'review': text, 'manifest': manifest}),
                   text=True, check=True, capture_output=True)
    states = {key: item['state'] for key, item in data['reviews'].items()}
    blocked = any(item['state'] == 'FAIL' or 'FAIL' in item['criteria'].values()
                  or any(note['severity'] == 'high' for note in item['notes'])
                  for item in data['reviews'].values())
    return dict(reference_commit=expected_commit,
                manifest_sha256=hashlib.sha256((package / 'manifest.json').read_bytes()).hexdigest(),
                review_sha256=hashlib.sha256(review_path.read_bytes()).hexdigest(),
                states=states, reviews=data['reviews'], hashes_verified=True,
                all_scenarios_pass=all(s == 'PASS' for s in states.values()),
                classification='BLOCKED — MUSICAL REGRESSION' if blocked else 'PENDING_HUMAN_AUDITION',
                final_approval='Requires explicit reviewer identification and overall human approval; export alone does not establish it')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', type=Path, required=True)
    parser.add_argument('--reviews', type=Path, required=True)
    parser.add_argument('--expected-commit', required=True)
    args = parser.parse_args()
    try:
        print(json.dumps(validate_review(args.package, args.reviews, args.expected_commit), indent=2))
    except (ValueError, KeyError, TypeError, OSError, subprocess.CalledProcessError) as exc:
        raise SystemExit(f'INVALID REVIEW: {exc}')
