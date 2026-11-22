# Hydra Tape Engine — Phase 7.1 listening guide

**Status: PENDING_HUMAN_AUDITION.** The five original automated workflows passed
the Phase 7.1 initial commit, but that is not approval of the new DSP sound.

## Provenance

- **A / Original:** human-approved Phase 6.6 engine, commit
  `b3349866b3c8d6c08ccfc4b435a5d89255901686`.
- **B / Processed:** Phase 7.1 candidate at the manifest's `reference_commit`.
- Both outputs use identical input PCM, seeds, parameters, timing and gain; neither
  side is normalized. Both native libraries are built with the same compiler and
  `-ffp-contract=off` to match the Phase 6.6 numerical rounding contract.
- 48 kHz stereo PCM24, 128-frame processing blocks.
- The bundle's manifest and the per-file SHA-256 hashes identify exact audio.
- No artificial human review state is created by automation.

## Listening workflow

Extract the GitHub Actions A/B artifact, then extract its inner
`hydra-phase7-1-ab.zip`. Verify the adjacent `.zip.sha256` digest against
the inner ZIP. In the extracted package directory run:

```bash
python -m http.server 8000 --bind 127.0.0.1
```

Open `http://127.0.0.1:8000/player/` in a browser. Select the package directory
containing `manifest.json`. The player checks the files and permits A/B
synchronized comparisons. Maintain a moderate fixed monitoring level.

The word `original` in the player means **A / previous processed engine**, not
a clean dry source.

| Pair | Material and settings | Listen for |
| --- | --- | --- |
| 01 | Clean guitar | Transients, repeat tonal balance |
| 02 | Bright tape safe edge, speed 55 / age 0 / tone 50 | High-frequency softness and potential shelf difference |
| 03 | Aged tape repeats | Natural loss of brightness in feedback |
| 04 | Driven guitar | Harshness, magnetic saturation harmonics |
| 05 | Conventional delay | Repeat clarity and feedback texture |
| 06 | Feedback 95 | Compression smoothness, musical sustain |
| 07 | Ping-pong Multi-Head | Stereo/rhythm stability and dense loops |
| 08 | Reverse quarter | Window continuity, feedback transitions |
| 09 | Warp feedback | Whether the Warp timbre remains recognizable |
| 10 | Long feedback tail (60 seconds; input stops at 1s) | Limit cycle character, unwanted whistles or growth |

The **historically unstable** high-speed/young-tape setting
(speed 100 / age 0 / tone 50 at 48 kHz) is covered by the Phase 7.1 pole
regression instead of an unsafe baseline WAV: its old high-shelf frequency
is 32 kHz, beyond Nyquist. An A/B WAV with exploding/NaN reference data
would be misleading and unsafe. Package generation also aborts if a render
is non-finite or reaches the hard limiter ceiling.

## Acceptance

Record PASS/FAIL/COMMENTS for all ten comparisons. Pay particular attention
to pairs **02, 06 and 10**. Any issue with pumping, bright-edge tone, high
feedback or lost tape warmth blocks musical signoff until reviewed.

This package demonstrates the sonic delta from the approved source, not a
mathematically defined superiority. Human musical approval and physical
ESP32-S3 runtime on the Phase 7.1 binaries are separate decisions.
