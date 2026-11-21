# Phase 6.4.1 — final ten-pair listening review

Human decision: **PENDING_HUMAN_AUDITION**. Earlier approvals do not transfer.
This package compares two processed DSP outputs; A is not an unprocessed source.

- **A / Original:** approved Phase 6.3 DSP at `before_commit` in the manifest.
- **B / Processed:** current candidate at `reference_commit` in the manifest.
- Identical source, seed, gain, parameters and events within each pair; no normalization.
- 48 kHz, stereo PCM24. Pairs 1–9 last six seconds; pair 10 lasts sixty seconds,
  with input silenced after the first second to expose the feedback tail.

Download the `hydra-phase6-final-review` Actions artifact linked in the PR/release
record. Extract the artifact, then extract its inner `hydra-phase6-final-ab.zip`.
Check the inner ZIP against the adjacent `.sha256` file. The manifest records
SHA-256 for each WAV/CSV; `reports/validation-summary.md` records frame/file checks.
Artifacts expire after 30 days; rerun the workflow if needed.

From the extracted inner ZIP directory run `python -m http.server 8000 --bind 127.0.0.1`,
open `http://localhost:8000/player/`, and select the directory containing
`manifest.json`. The player verifies and decodes the local files; there is no autoplay.
Start at a moderate listening level with familiar headphones or monitors. The same
monitor gain applies to both sides. A/B preserves playback position; use seek and
loop selection to compare the same passage. Review both channels.

| Pair | Scenario | Listening focus |
|---|---|---|
| 01 | Clean guitar | transients, low-level detail, tape texture |
| 02 | Driven guitar | compression, harmonics, harshness |
| 03 | Conventional delay | repeat clarity, stereo image, decay |
| 04 | Reverse 1/4 | window boundaries and musical phrasing |
| 05 | Reverse 1/8 | short-window continuity and clicks |
| 06 | Reverse high feedback | sustained texture and bounded output |
| 07 | Multi-Head | rhythm, separation and stereo coherence |
| 08 | Tape speed transition | automation continuity and tonal movement |
| 09 | BPM change | timing transition and preserved tails |
| 10 | Long feedback | sixty-second residual energy, tone and unwanted growth |

Use event markers and telemetry to locate programmed changes. Telemetry is block
sampled, not a measurement of every audio sample. Numeric validation does not
establish musical approval. The Phase 6.4 canonical LUT and playback-filter smoothing
changed audio relative to Phase 6.3; Phase 6.4.1 preserves that DSP and corrects build
rounding, diagnostics and host reset handling. A normalized error of `3e-5` is about
252 PCM24 LSB, not a fraction of one LSB.

Set each review state explicitly and record observations, then export the review JSON.
Retain the package manifest/commit and hashes with the review. An agent's browser
playback check is not a human listening signoff. Physical ESP32-S3 runtime validation
is a separate pending gate; see `reports/PHASE6_RELEASE_DECISION.md`.
