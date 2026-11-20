# Phase 6.3 — human Reverse A/B approval

Received 2026-11-19 from the responsible reviewer as the Listening Lab export
`hydra-reviews-7431a6d (1).json`. The exported file is the review record; no separate
free-text statement was supplied. It is archived at
[phase6-reverse-human-reviews-7431a6d.json](evidence/phase6-reverse-human-reviews-7431a6d.json),
SHA-256 `9e04193c046737cd8c7829f19c7724eec0a3a368f0d0d7f7b93f2bb779c5e740`.

**Candidate human audition: PASS — 10/10 scenarios. Technical release: BLOCKED — REGRESSION
(issue #48 and complete-engine criteria). No merge.**

## Scope of the approval

- Reference commit: `7431a6db05abfde97838721cd089f94c7fed5047`.
- Ten ordered pairs (`01_reverse_01` … `10_reverse_10`), each explicitly `PASS`.
- All 30 exported hashes (20 WAV + 10 CSV) match the reviewed manifest and the actual
  package files on disk; verified with
  `python scripts/validate_reverse_reviews.py --reviews <json> --manifest <manifest> --package <dir>`.
- The per-criterion dictionaries are **empty** and no comments or notes were supplied.
  No individual criterion PASS (magnetic compression, harmonics, transients, stereo, etc.)
  is inferred or invented. The approval is scenario-level.
- The package's before/after audio is bit-exact (`max_abs = RMSE = 0`), so this approval
  confirms the reviewer perceived no difference between the Phase 6.2 and Phase 6.3 sides
  in this package; it is not evidence of a sound change.

| Review ID | Scenario | State |
|---|---|---|
| 01_reverse_01 | Reverse 1/4 | PASS |
| 02_reverse_02 | Reverse 1/8 | PASS |
| 03_reverse_03 | Reverse 1/16 | PASS |
| 04_reverse_04 | Reverse high feedback | PASS |
| 05_reverse_05 | Reverse with BPM change | PASS |
| 06_reverse_06 | Reverse with MIDI clock | PASS |
| 07_reverse_07 | Multi-Head | PASS |
| 08_reverse_08 | Conventional delay | PASS |
| 09_reverse_09 | Warp | PASS |
| 10_reverse_10 | Stress combined | PASS |

## What this does and does not clear

It clears the human audition for this exact package/commit and hash set. It does **not**
clear issue #48 strict Native/WASM equivalence, the four complete-engine tail-growth
criteria, the 44.1 kHz parameter-transition criterion, or the preferred P99 < 70% target.
Those remain explicit blockers in the [Phase 6.3 report](PHASE6_REVERSE_REALTIME_OPTIMIZATION.md).

Fresh renders and later commits start `NOT REVIEWED`; this approval is not automatically
extended to them. The Phase 6.2 approval remains valid only for its own `f0c16cd` package
and is not a substitute for this review.

No merge is authorized by this record.
