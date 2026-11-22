# Hydra Phase 7.2A — feedback routing blind A/B guide

**PENDING HUMAN AUDITION / NOT A RELEASE.**

### Reference and conditions

- **A / Original**: previously approved Phase 7.1 DSP at merge commit
  `24977b67e1ab8441080a29d9e5d40114c966ac50`.
- **B / Processed**: compile-time isolated feedback routing experiment on the
  PR's exact `reference_commit`.
- Identical stereo float32 input, seed, dry/wet and feedback settings, automation
  schedule and 48 kHz block processing. PCM24 output is **not normalized**.
- A third invisible control (experiment OFF from candidate tree) must be bit-exact
  against A for each scene, or packaging fails.
- Experiment B is a candidate texture, *not* a mathematically proven improvement.

Verify the artifact ZIP SHA-256 and inner manifest hashes. Extract the archive,
serve its directory with `python -m http.server 8000 --bind 127.0.0.1`
and open `http://127.0.0.1:8000/player/`. Select the directory containing
`manifest.json`. Listen at moderate fixed gain and compare aligned positions
with the player's A/B control.

| Pair | Scenario | Listening question |
| --- | --- | --- |
| 01 | Clean repeat | Are repeat spacing and coloration still musical? |
| 02 | Aged and dark repeats | Is the feedback progressively warmer or duller? |
| 03 | Delay monitor sweep, 90→20→90 | Does changing monitoring level distort the underlying repeat tail? |
| 04 | Texture monitor sweep, 90→10→90 | Is the texture/feedback separation useful? |
| 05 | Hiss after input silence | Is the noise floor pleasant or artificially recirculating? |
| 06 | Driven guitar, feedback 85 | Is saturation compressed naturally, without harshness? |
| 07 | Ping-pong Multi-Head | Does the rhythmic stereo image remain clear? |
| 08 | Reverse blend on at 2 s, off at 4 s | Are transitions smoother, with no unexpected tail jump? |
| 09 | Warp negative control | The two alternatives **must** sound identical and be bit-exact |
| 10 | 60-second feedback residual, input silenced after 1 s | Which self-oscillation/decay behavior is more inspiring and controlled? |

### Acceptance

Record each pair's preference, audible defect and any comments. A/B sound
review is independent of automated PASS and eventual ESP32-S3 performance.
If the experimental route loses warmth, depth or textured degradation,
retain the existing approved topology or propose a *third, hybrid* candidate.
Do not merge experimental routing into production without explicit approval.
