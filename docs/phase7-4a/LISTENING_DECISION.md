# Phase 7.4A.1 — Listening Decision & Validation Summary

## Status Tokens

- `DSP_REFINEMENT_PASS`
- `C0_BIT_EXACT_PASS`
- `WEB_LISTENING_READY`
- `HUMAN_LISTENING_APPROVED` (C1, C2, and C3 approved across all 10 scenarios on 2026-11-22)
- `ESP32_PHYSICAL_NOT_EXECUTED`

Human A/B listening completed on the regenerated Web Audio lab (`artifacts/phase7-4a/comparisons/index.html`): **all 3 experimental candidates (C1, C2, C3) are approved across all 10 scenarios**, with C0 preserved bit-exact. Promotion to production firmware/web default remains gated on on-device ESP32-S3 profiling and explicit phase promotion.

---

## Integration Note (Phase 7.3 / PR #58)

- **PR #58 (`feat/phase7-3-classic-c-web-default`)** remains untouched and is still the production candidate.
- **C0 Bit-Exact Equivalence:** Verified byte-for-byte across all 10 deterministic scenarios (`48 kHz`, `128`-frame blocks, `-ffp-contract=off`) against PR #58 HEAD (`e81511707d4205221cbd634da0d9b90c0b0c9d64`).
- **Pending Phase 7.3 Dependency:** `CMakeLists.txt` currently writes `build/web/hydra-build-info.json` via `file(GENERATE ...)` at CMake configure/generate time (before `cmake --build` succeeds). That fix belongs to Phase 7.3 and was intentionally **not** altered in Phase 7.4A.

---

## How to Audition Locally (Windows / macOS / Linux)

1. Serve the `artifacts/phase7-4a/` directory (or extracted CI artifact `hydra-phase74a-repeat-aging-listening`):
   ```powershell
   python -m http.server 8080
   ```
2. Open `http://localhost:8080/comparisons/index.html` in a browser.
3. Use the Web Audio API A/B player (preloaded `AudioBuffer`s, synchronized `AudioBufferSourceNode`s, 8 ms crossfade, unity gain, loop toggle, `[1-4]` / `[Space]` shortcuts) to compare **C0 Original**, **C1 Wear**, **C2 Transient**, **C3 Combined**, and **Dry** without playback interruption.

---

## C3 Cascade Order & Balance

In **C3 (`HYDRA_PHASE74_REPEAT_AGING_MODE == 3`)**, `frame.feedbackSource` passes through:
1. `processTransientSoftening` (C2, `depthScale = 0.70f`) — detects raw attack transients before spectral wear alters the envelope.
2. `processDynamicSpectralWear` (C1, `depthScale = 0.70f`) — applies smooth energy/HF-dependent wear to the post-transient signal.

Because both stages now return `x` identically when inactive, sustained low-to-moderate signals avoid double static filtering while high-energy transient repeats receive controlled combined aging.
