# Listening guide

Decision: HUMAN_LISTENING_PENDING. Synthetic source models are labeled in metrics
and manifest. No human musical verdict or claim about real instrument recordings
is inferred from objective measurements.

The artifact `hydra-phase75a1-studio-signal-path-listening` contains float32 WAVs,
source samples, ten A/B pairs, raw buffers for Native/WASM comparison, metrics,
SHA256 manifest, Windows instructions, a synchronized blind player and the real
Hydra Web UI under web-ui/. Output files are not independently normalized.

Windows, after extracting the artifact:

```
python -m http.server 8075 --bind 127.0.0.1 --directory <extracted-directory>
```

Open http://127.0.0.1:8075 for A/B, or /web-ui/ for real-time WASM controls.
Use Load Custom Audio Source to load a WAV recording of your guitar, initialize
DSP, then Play. Select SIGNAL PATH CLASSIC/STUDIO; Wet Level is visible in Studio.
The badge confirms the C-API state; no WASM rebuild is needed while switching.
Classic Texture Blend remains a historical control and is hidden in Studio.

Listen to clean guitar, driven guitar, bass, electric piano, strong attacks,
Wet Level automation, a Classic-to-Studio switch at 4 seconds in an active tail,
high feedback, texture versus Clean Dry with delay disabled, and mono compatibility.
Motion modes rotate across the ten scenes. Automation changes Wet Level at
1, 1.5013 and 2 seconds. Compare mono sum, bass definition, pick attack, apparent
loudness, modulation on repeats, comb coloration and sustained tail continuity.

Play/restart creates both decoded sources on one AudioContext clock. A/B switches
crossfade gains over 10 ms without seeking; the shared seek slider restarts both
at the same offset. Blind trial randomizes A/B mapping and hides identifying
metrics until Reveal. Supplemental fixed linear/equal-power files expose their
loudness difference; they are not a level-matched preference test.

Expected structural difference: Classic attenuates dry according to texture
blend and adds the short head. Studio leaves dry amplitude/phase intact and
keeps the existing tape character on repeats. That is a hypothesis for human
comparison, not an observed listening result. Strong overload invokes hard sample
limiting; avoid it in nominal guitar use by lowering wet/input levels.

Reproduce from source: CMake Release build with BUILD_TESTING=ON, build Web WASM,
then run `python scripts/phase75a_studio_lab.py`, `node scripts/phase75a_wasm.mjs`
and `python scripts/package_phase75a.py`. Set HYDRA_WEB_BUILD for another web build.
