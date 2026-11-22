# Phase 6.6 — Final musical acceptance and release audit

Audit date: 2026-11-21 (America/Sao_Paulo).
Repository: `ovelhaaa/hydra-tape-engine`; branch `feature/musical-validation-phase6`;
[PR #54](https://github.com/ovelhaaa/hydra-tape-engine/pull/54) remains open, targeting `main`.

**PENDING_HUMAN_AUDITION — no merge authorized or performed.**

## Repository and source identity

Audited candidate HEAD: `a7e22022f71f670e51a77e63149148c88605d9f0`. Fetch confirmed no commits after
the supplied audited HEAD. The Phase 6.6 documentation/tooling changes must have
their own exact-head CI results before any merge decision; the following runs
belong only to the audited candidate. No DSP or ABI changes are part of this audit.

There is no Git diff from Phase 6.4.1 `e2c94972cc295f2c8ac81ea0b0ecc410868b8961`
to the audited candidate in `dsp/`, `include/`, `lib/`, CMake or final-render
scripts. Phase 6.5 firmware inputs (`src/`, `include/`, `dsp/`,
`platformio.ini`) also match measured `990e5e9bc0cb22e469705555c26ef1ef9c94ae06`.
The published final package was rendered directly at the audited HEAD; no new
render is necessary. Canonical LUT, phase-averaged bias, saturation, Reverse V2,
GTC/Tap/MIDI, feedback, smoothing and C API 1.7 remain preserved.

An existing untracked `scripts/create_phase6_pr.py` was left untouched and
unexecuted. No historical evidence was removed. Generated ZIPs, browser tests
and synthetic validator controls stay under ignored `artifacts/phase66/`.

## Exact audited HEAD CI

| Workflow | Result |
|---|---|
| [Web Demo (Build + Deploy Pages)](https://github.com/ovelhaaa/hydra-tape-engine/actions/runs/44961696825) | PASS |
| [Phase 6 Listening Lab](https://github.com/ovelhaaa/hydra-tape-engine/actions/runs/44961696878) | PASS |
| [Core Equivalence CI](https://github.com/ovelhaaa/hydra-tape-engine/actions/runs/44961696922) | PASS |
| [Phase 6.3 Reverse Real-Time](https://github.com/ovelhaaa/hydra-tape-engine/actions/runs/44961696810) | PASS |
| [Phase 6.2 Bias Anti-Aliasing](https://github.com/ovelhaaa/hydra-tape-engine/actions/runs/44961696854) | PASS |

Jobs and logs independently confirmed Native CTest 16/16, WASM CTest 16/16,
ABI and unchanged goldens, ESP32-S3 builds, full-engine feedback/transitions,
and strict Native/WASM on both Ubuntu 24.04 and 26.04:
`max_abs = RMSE = 0` over 10240 stereo frames. Uncontrolled FMA diagnostic
failures remain preserved alongside passing controlled runs.
Canonical inputs/LUT and `-ffp-contract=off` remain enforced; thresholds
1e-5 / 1e-6 were not changed. Bias spectral/whine and Reverse automated gates pass;
musical integrity remains a human decision.

## Published listening package

[Artifact 14730235810](https://github.com/ovelhaaa/hydra-tape-engine/actions/runs/44961696878/artifacts/14730235810)
was downloaded again and verified, without regeneration. Available at audit;
expiry: 2026-12-21 23:26:54 UTC.

- Outer artifact SHA-256: `a12e90c6f6a9241f3983280410b49a41d2267d40256032e4b45763ad05a287c5`.
- Inner ZIP SHA-256: `bead9ed5a3b5e3d1d685ead413c733c17958e17006b8e923a4a032c19a9ffbe0`, matches supplied checksum.
- Manifest SHA-256: `d52815ee22767fdf03d10ca4f9e51273dc514ef6119ceb51222b3a41ca0ad228`.
- Reference commit: `a7e22022f71f670e51a77e63149148c88605d9f0`; A source: `7431a6db05abfde97838721cd089f94c7fed5047`.
- Candidate library SHA-256: `d21bb0a174592a189a0f98a7a14f20c9816e29af6745694eb95a70a43b385387`.
- Linux/glibc2.39, Python3.11.18, NumPy2.4.1; actual compiler invocation/version retained in package `reports/comparison.json`.
- Ten A/B pairs, twenty stereo PCM24 WAVs at 48000 Hz, ten CSVs.
  Pairs 1–9: 288000 frames each side; pair 10: 2880000.
  All thirty hashes, actual PCM bytes and telemetry lengths/timestamps verified.
- Same generated source, gain, BPM, events and seed per pair; fixed PCM24 scale,
  no normalization. A is a processed historical reference.
- Offline player, guide and reports present. Seven interface unit tests pass.
  Real Chromium141 Web Audio test passes all ten scenes: no autoplay, shared
  A/B position, seek, loop, events, export/import, missing-file rejection,
  responsive layout. Its synthetic reviews are **not human evidence**.

Machine-readable hashes and scenario records:
[phase66-final-audit.json](evidence/phase66-final-audit.json).

## Human session — pending

| ID | Scenario | Human state | Comments |
|---|---|---|---|
| 01_final_01 | Clean guitar | NOT REVIEWED | No human record received |
| 02_final_02 | Driven guitar | NOT REVIEWED | No human record received |
| 03_final_03 | Conventional delay | NOT REVIEWED | No human record received |
| 04_final_04 | Reverse 1/4 | NOT REVIEWED | No human record received |
| 05_final_05 | Reverse 1/8 | NOT REVIEWED | No human record received |
| 06_final_06 | Reverse high feedback | NOT REVIEWED | No human record received |
| 07_final_07 | Multi-Head | NOT REVIEWED | No human record received |
| 08_final_08 | Tape speed transition (priority) | NOT REVIEWED | No human record received |
| 09_final_09 | BPM change | NOT REVIEWED | No human record received |
| 10_final_10 | Long feedback | NOT REVIEWED | No human record received |

Historical bias approval at `f0c16cd` (12 candidate scenes) and Reverse approval
at `7431a6d` (10 scenes) remain scoped to their original exports/manifests.
Neither is transferred to this package. No final reviewer JSON or overall
approval has been received.

Use the extracted, verified `artifacts/phase66/package` directory:

```powershell
python -m http.server 8000 --bind 127.0.0.1 --directory artifacts/phase66/package
```

Open http://localhost:8000/player/ and choose that package directory.
Follow [the final guide](PHASE6_FINAL_LISTENING_GUIDE.md). Explicitly evaluate all
ten scenes as PASS / NEEDS ATTENTION / FAIL / NOT REVIEWED. Prioritize scene08
clicks, abrupt tonal changes, pumping and response. Check residual whine,
Reverse windows, tails and feedback as well as timbre and dynamics.
Export reviews and provide reviewer identity plus an explicit overall decision.

Validate the actual human export with:

```powershell
python scripts/validate_final_reviews.py --package artifacts/phase66/package --reviews <actual-human-export.json> --expected-commit a7e22022f71f670e51a77e63149148c88605d9f0
```

The validator rechecks every package hash/PCM/CSV, exact final suite, commit,
ten scenario IDs, export hash triplets, schema, states, comments, criteria and
timestamped notes using the player's schema. It retains comments/qualifications.
Wrong commit/hash, missing scene, invalid state and invalid note negative
controls rejected; synthetic all-PASS still cannot grant final human approval.
FAIL/failed criteria/high-severity notes block pending assessment; NEEDS
ATTENTION, unreviewed scenes or missing overall signoff keep acceptance pending.
The actual human JSON must be archived unchanged with its hash after receipt.
No synthetic controls may be used as a signoff.

## Physical evidence retained

Firmware: `990e5e9bc0cb22e469705555c26ef1ef9c94ae06`;
production binary `0a2c42cef5921a397b95576cdcb87f589bb707195c6f73e6faeeac4f2ebf13e5`.
[Phase 6.5 record](PHASE65_PHYSICAL_HARDWARE_ACCEPTANCE.md) remains authoritative.
Offline archive validator rerun: PASS (raw/JSON/SHA, seven complete cases and
five-minute runtime). This audit did not rerun hardware.

ESP32-S3/240MHz, 44100Hz stereo, 128-frame blocks, 256 warmup +10000 measured
blocks per case; 300.2168s/103399-block sustain. Zero observed DSP/full-work
budget excesses or TX failures; heap125296B, PSRAM1663048B stable at sampled
points, stack minimum27268B. Budget2902.49us; worst P99=2215us;
full-work peak2820us leaves only82.49us (2.84%). This is limited observed margin,
not a guarantee across every operating condition; preferred P99<70% is unmet
outside Delay. No preventive DSP optimization is proposed.

Scope: internal generator and driver-level I2S TX only. Analog DAC sound/output,
external I2S RX, physical external MIDI Clock, LittleFS and OLED/I2C remain
unaccepted. No electrical clock capture or listening approval is inferred.

## Issue #48 and release recommendation

[Issue #48](https://github.com/ovelhaaa/hydra-tape-engine/issues/48) is still open.
The technical closure draft is [retained here](PHASE66_ISSUE48_CLOSURE_DRAFT.md);
no comment was posted and the issue was not closed. Technical strict equivalence
is resolved; its sound/performance scope must retain the pending musical gate.

Final classification: **PENDING HUMAN AUDITION** (`PENDING_HUMAN_AUDITION`).
Do not merge. Once a valid export, explicit overall human approval and
nonblocking assessment exist, recheck exact final-head CI and source/package
identity before changing the record to READY FOR MERGE REVIEW.
Integration into main still requires express authorization.

