#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<USAGE
Usage: $0 --overwrite [--build-dir <dir>]

Regenerates tests/fixtures/golden/native/*.csv from deterministic fixtures.
Refuses to run unless --overwrite is provided.
USAGE
}

OVERWRITE=0
BUILD_DIR="build-golden"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --overwrite)
      OVERWRITE=1
      shift
      ;;
    --build-dir)
      BUILD_DIR="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage
      exit 2
      ;;
  esac
done

if [[ "$OVERWRITE" -ne 1 ]]; then
  echo "Refusing to overwrite golden files without --overwrite." >&2
  exit 2
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

python3 tests/core/generate_fixtures.py --output-dir tests/fixtures/input

cmake -S . -B "$BUILD_DIR" -DBUILD_TESTING=ON
cmake --build "$BUILD_DIR" --target hydra_golden_regression_native

mkdir -p tests/fixtures/golden/native

"$BUILD_DIR"/hydra_golden_regression_native --scenario delay_modes --input tests/fixtures/input/sweep_log.csv --output tests/fixtures/golden/native/delay_modes.csv
"$BUILD_DIR"/hydra_golden_regression_native --scenario reset_state --input tests/fixtures/input/impulse.csv --output tests/fixtures/golden/native/reset_state.csv
"$BUILD_DIR"/hydra_golden_regression_native --scenario param_automation --input tests/fixtures/input/music_excerpt.csv --output tests/fixtures/golden/native/param_automation.csv

echo "Goldens updated in tests/fixtures/golden/native"
