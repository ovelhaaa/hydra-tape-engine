# Hydra Phase 7.2C — Classic Tape Feedback listening guide

**EXPERIMENTAL / HUMAN AUDITION PENDING.** The objective is a musical choice,
not proof of exact RE-201 or Echoplex circuit emulation.

## Three architectures, two synchronized comparison packages

- **A / approved:** Phase 7.1 `24977b67e1ab8441080a29d9e5d40114c966ac50`.
  The feedback is partly driven by the monitor mix and recirculates its hiss.
- **B / clean independent:** isolated `delaySig` read-head source, independent
  of monitor gain, but without playback-head tonal degradation.
- **C / classic-style independent:** the same already forward/reverse-blended
  `delaySig` passes through a dedicated reproduction chain: dropout/gap-loss,
  azimuth (if enabled), high-shelf rolloff, head bump and playback gap-loss
  lowpass. Add 20% of already synthesized hiss before the unchanged feedback
  electronics and magnetic write. Do not share the monitor's biquad states.

The existing player is strictly A/B. Use **A/B package** to compare original
and clean-independent, **A/C package** to compare original and classic-style.
Input, seeds, parameters, frame rate and order are equal between packages.
Their A WAVs should therefore be identical (compare the manifest hashes).
For a B/C comparison, toggle between the processed versions of the same
scene from the two packages at a fixed listening level.

## How to listen

Download and unpack both GitHub Actions ZIP artifacts:
`hydra-phase7-2-feedback-ab.zip` and
`hydra-phase7-2-feedback-ac.zip`. Verify each matching SHA-256 sidecar
and manifest checksums. Serve each extracted directory with
`python -m http.server 8000 --bind 127.0.0.1` and open
`http://127.0.0.1:8000/player/`. Select the corresponding package directory.

No normalization, loudness matching or artificial fade is applied. If one
variant sounds louder, record the difference rather than compensating
in the source files. Start at a moderate headphone/speaker level.

| Pair | Listening priority |
| --- | --- |
| 01 Clean repeat | Note clarity and evolving harmonics |
| 02 Aged and dark | Natural HF decay versus excessive dulling |
| 03 Delay monitor sweep | Feedback lifetime unchanged by Echo Level |
| 04 Texture monitor sweep | Monitor texture does not alter tape recirculation |
| 05 Hiss after silence | Pleasant floor; no hiss buildup or cycling whine |
| 06 High feedback 85 | Musical self-oscillation, no pumping or abrupt peaks |
| 07 Ping-pong Multi-Head | Rhythmic clarity, balanced stereo |
| 08 Reverse transition | Single crossfade, natural return to normal repeats |
| 09 Warp negative control | A/B/C should be identical, bit-exact |
| 10 Long feedback, 60 s | Controlled self-oscillation and tonal evolution |

## Acceptance record

For each pair, record preference A / B / C / tie; note whether the other
versions are still musically useful; describe transient sharpness, noise,
pitch wobble, spectral movement and feedback stability. A technically
correct model may sound worse for performance; retain A if preferable.

Important: the C hiss blend (0.20) is a **deliberately uncalibrated
experiment**, not a historical component specification. The real-world
physics model remains simplified. C adds six biquad filters, two one-pole
filters and two allpasses across the stereo channels; evaluate ESP32-S3
callback time, stack and internal memory **before enabling it on-device**.
This package tests Native audio, not physical firmware acceptance.

Only explicit human musical acceptance and separate physical real-time
acceptance may authorize choosing a new production architecture. There
is no merge authorization at this stage.
