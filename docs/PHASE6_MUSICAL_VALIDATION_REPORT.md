# Hydra Tape Engine 2.0 — Phase 6.1 evidence report

## Phase 6.2 candidate update — 2026-11-19

Current classification: **BLOCKED — REGRESSION / PERFORMANCE**. The [bias anti-aliasing report](PHASE6_BIAS_ANTIALIASING_REPORT.md) supersedes the earlier delivery-only scope for the magnetic stage. A phase-averaged bias candidate retains magnetic state/parameter mappings while removing sampled-carrier injection. Original20-scenario reference audio remains preserved; the separate60-pair supplement uses the same Listening Lab and starts NOT REVIEWED. Isolated gates49/49 pass, full-engine22/27, Native12/15 and strict Native/WASM remain failing; all goldens pass unchanged. Firmware builds; physical tests expose rhythmic Reverse deadline overruns in baseline and candidate. Earlier successful tests below are historical baseline evidence, not current-candidate results. Candidate human approval: 12/12 PASS for the hash-verified f0c16cd local Windows package; see [human audition record](PHASE6_BIAS_HUMAN_AUDITION.md). This does not convert automated failures to PASS. No merge.

Date: 2026-11-18. PR: [#54](https://github.com/ovelhaaa/hydra-tape-engine/pull/54), branch `feature/musical-validation-phase6`, destination `main`. No automatic merge.

Human listening: **PARTIAL HUMAN AUDITION — NEEDS ATTENTION**; final musical signoff remains pending. Automated tests and metrics do not establish musical approval. Delivery classification: **READY FOR HUMAN LISTENING**. The first complete Linux package passed CI and was published; strict equivalence remains a separate failing gate for merge review.

## Human listening update — 2026-11-19

**PARTIAL HUMAN AUDITION — NEEDS ATTENTION.** The reviewer reported no other perceived issue in the listening performed, except a recurring high-pitch whine. Eleven scenarios were explicitly marked NEEDS ATTENTION; the remaining nine retain NOT REVIEWED (scenario 10 also has a whine comment). This is not complete musical approval. Controlled native source-copy ablations isolate the bias contribution as the cause of the dominant 6 kHz component in scenario 01, with a reduction exceeding 50 dB when bias is removed. See [bias whine diagnostic, measurements and reproduction](PHASE6_BIAS_WHINE_DIAGNOSTIC.md). The official DSP, release WAVs, thresholds and goldens remain unchanged; the defect requires resolution and repeat listening before signoff.

## Baseline and scope

The initial HEAD was `9c1c8d29a0c73e2e2d416af8de4c7cc5a4458c53`; fetch/fast-forward found no later branch commits. The full diff against main contained seven files, including the existing WASM thread guard in `tests/core/tap_tempo_test.cpp`. Phase 6.1 changes delivery, tooling and evidence only. TapeCore, Reverse, GTC, PLL, Tap Tempo, MIDI Clock, thresholds and goldens are unchanged.

Existing untracked `scripts/create_phase6_pr.py` was left untouched and is not part of this delivery. The historical PR #47 interface at `tools/listening-lab/` is preserved; the new interface is `tools/phase6-listening-lab/`.

Baseline CI: [Core run 44405989345](https://github.com/ovelhaaa/hydra-tape-engine/actions/runs/44405989345) passed native, WASM golden, ESP32 build and listening jobs; strict equivalence failed. [Web Demo run 44405991326](https://github.com/ovelhaaa/hydra-tape-engine/actions/runs/44405991326) passed. These are baseline results, not Phase 6.1 runs.

## Native/WASM strict — separate environments

| Execution | Source/build | max_abs | RMSE | Status |
|---|---|---:|---:|---|
| CI Linux strict, run 44405989345, job 135161716377 | PR merge `0065d1f19dd8f8c142574cd7a9079972046fe08f` (head 9c1c8d2), Ubuntu 26.04.1, Release, Emscripten 3.1.62; 2048 samples | 3.668100000000e-05 | 1.585565032192e-05 | FAIL |
| Additional local Windows strict, rerun 2026-11-18 | Head 9c1c8d2, Release MinGW native and Emscripten 3.1.62 WASM build trees; later reproduced from a fresh WASM build, same comparator | 5.968000000002e-06 | 2.344106157418e-06 | FAIL |

The older report's 5.968e-06 / 2.344e-06 values match the Windows local execution, not the Linux CI job. This is evidence of different environments; it does not establish the exact floating-point root cause or prove that both runs are equivalent. The initial local caches were subsequently checked against a fresh `build/phase61-wasm` build with Emscripten 3.1.62, reproducing the same values. Thresholds stay `max_abs <= 1e-5`, `RMSE <= 1e-6`. [Issue #48](https://github.com/ovelhaaa/hydra-tape-engine/issues/48) remains open. No diagnostic or golden was disabled. The full local WASM CTest has 13 passes and 2 strict-related failures (equivalence and A/B null comparison); reporting only the three passing goldens as an entirely green WASM suite would be misleading.

## Reproducible native offline package — category A

`render_phase6_review.py` discovers `.dll`, `.so` or `.dylib` only in the requested build/configuration directories; `--library` overrides discovery. Windows DLL search handles remain alive. Defaults: 48 kHz, 128-frame blocks, PCM24 stereo. All 20 ordered scenarios produce original and processed WAVs (40 total) and 20 CSVs. Scenario 15 now calls genuine MIDI 0xFC/0xFB events instead of generic transport Stop/Play; telemetry confirms stopped/running states. Partial final blocks are now processed instead of leaving the last 64 frames of 4.5-second scenarios unrendered.

The manifest records actual Git HEAD, C API version, full applied parameter map, tempo source, duration/rate, seed, event timestamps, MIDI pulse frames, hashes and metrics. Seed 6100 constructs clock jitter; seed 6100+scenario number constructs sources. No wall clock drives audio. Scheduled control changes apply at the beginning of the containing block; MIDI/tap APIs receive sample timestamps. Telemetry snapshots occur every four blocks. Markers use physical audio boundaries, observed beat-phase wraps, scheduled events and observed telemetry transitions; descriptive claims such as “invariant confirmed” are not fabricated timeline evidence.

Initial local physical file validation passed: 20 scenarios, 40 WAVs, 20 CSVs, 50,837,189 audio/telemetry bytes. The verified Linux ZIP size and manifest commit are reported below; the local initial ZIP was 42,225,511 bytes and is superseded by the CI package. Exact hashes apply to that package/toolchain. Cross-platform bit identity is not asserted. Native/WASM numerical tolerances remain those of the strict comparator above.

Commands, dependencies and import instructions: [tool README](../tools/phase6-listening-lab/README.md). Validation rejects missing/empty WAVs or CSVs, bad hashes, invalid manifests/counts, mismatched rate/channels/PCM width, truncated frame counts and incomplete/non-monotonic telemetry. Analysis JSON and validation summary are generated from the real manifest, not from a hand-written result table.

## Performance classifications

| Category | Evidence | Limit |
|---|---|---|
| A — Native Offline | Actual rendering, WAV/CSV validation and analysis | Not real-time device latency |
| B — WASM / Node.js | Actual WASM via simulated AudioWorklet globals; 24,000 process blocks | Not browser scheduling or physical dropouts |
| C — Browser Real DSP benchmark | NOT RUN | Chromium tests the offline A/B player with actual AudioContext; it does not run the DSP in a real AudioWorklet |
| D — ESP32-S3 Physical | FIRMWARE BUILT / FLASHED — PHYSICAL RUNTIME NOT VERIFIED | Prior phase flash reported ROM bootloader; no new flash/runtime/I2S measurement in 6.1 |

### New local category B execution

```json
{
  "environment": {
    "node": "v24.11.1",
    "os": "win32 10.0.19045 x64",
    "cpu": "AMD Ryzen 7 7730U with Radeon Graphics         ",
    "emscripten": "3.1.62",
    "commit": "98ca3ddd359d549ca237bbd16b30bb4194ef0fef",
    "wasm_sha256": "1893eb3a595f6510a4f5b566853d17566f30db882959326e47e8acea8ba590df"
  },
  "blocks": 24000,
  "block_frames": 128,
  "simulated_seconds": {
    "rate_48000": 32,
    "rate_44100": 34.82993197278912
  },
  "limitations": "process() wall time and block-budget overruns in Node; no browser scheduler, device latency, I2S, or physical dropout measurement. No allocation instrumentation."
}
```

| Rate | Scenario | Mean µs | P50 µs | P95 µs | P99 µs | Peak µs | Budget overruns |
|---|---|---:|---:|---:|---:|---:|---:|
| 48000 | A_tape_echo_base | 35.21 | 29.30 | 60.90 | 121.00 | 404.10 | 0/3000 |
| 48000 | B_multi_head_stereo | 40.38 | 36.20 | 63.30 | 77.00 | 804.10 | 0/3000 |
| 48000 | C_reverse_v2 | 32.74 | 31.60 | 38.90 | 54.70 | 306.50 | 0/3000 |
| 48000 | D_combined_stress_chain | 31.24 | 30.30 | 36.30 | 44.90 | 328.90 | 0/3000 |
| 44100 | A_tape_echo_base | 31.20 | 29.80 | 40.80 | 51.70 | 325.30 | 0/3000 |
| 44100 | B_multi_head_stereo | 36.57 | 35.30 | 39.90 | 58.50 | 536.70 | 0/3000 |
| 44100 | C_reverse_v2 | 32.83 | 31.30 | 46.90 | 55.90 | 255.10 | 0/3000 |
| 44100 | D_combined_stress_chain | 31.74 | 30.30 | 43.80 | 57.60 | 282.10 | 0/3000 |

Simulated duration: 32.0 s at 48 kHz and approximately 34.83 s at 44.1 kHz. The script's `dropouts` means `process()` exceeded the per-block time budget in Node, not observed audio device dropouts. No allocation instrumentation was used; the previous “zero allocations” claim is not established by this tool. Older 86–120 µs mean values are historical Phase 6 measurements with incomplete environment provenance and are not reused as current evidence.

### Historical physical baseline (not produced in Phase 6.1)

The [ESP32 performance audit](ESP32_S3_PERFORMANCE_AUDIT.md), baseline `c9bc17e`, recorded 11 scenarios and a five-minute I2S-active run: 103,360 blocks, mean 1772.08 µs (61.05%), P99 1861 µs (64.12%), peak 2108 µs, zero overruns/I2S failures and +0 B heap/PSRAM. These numbers remain historical. Current local firmware build passed with RAM 25,920 B and flash 618,407 B; building or flashing alone cannot revalidate those physical measurements.

## Interface, validation and human review

The dedicated player groups all 20 scenarios and displays real metadata/metrics, original and processed waveforms (left channel), cursor, scheduled markers and telemetry transitions. Both stereo buffers use one AudioContext explicitly created at the package sample rate, identical start/offset and native loop boundaries. This avoids 44.1 kHz device-default resampling truncation; the browser test reproduces that device default. A/B changes complementary linear gains over 8 ms. No normalization, relative gain matching or additional DSP; common monitor gain defaults to 35%. Playback never starts automatically.

Seek, waveform zoom, shift-drag range selection, loop fields and previous/next event controls support repeated inspection. Each scenario has eight optional criteria, an explicit review state, comments and timestamped severity notes. Export/import JSON is independent of localStorage; import is validated against the package commit and audio/telemetry content hashes. Errors disable audition and never assign PASS.

Local validation: five Python delivery tests, four Node player tests, real Chromium functional checks for all 20 WAV/CSV sets, A/B clock/no restart, seek, loop, scenario stop, JSON roundtrip/persistence, missing-file handling and desktop/mobile layouts passed. The seven historical Listening Lab model tests also passed. Browser screenshots and machine-readable results are workflow evidence artifacts. These are functional tests, not a human listening decision.

Regression: Native 15/15 passed with `PYTHONUTF8=1` (the unmodified baseline Python test otherwise fails under Windows cp1252); WASM 13/15 passed with the two documented strict failures; Web host 40/40 passed with built WASM; Web Demo build and ESP32-S3 build passed. Native covers MIDI Clock, Reverse, GTC/transport and ABI. The Linux workflow is pinned to Ubuntu 24.04 (supported by the pinned Playwright); it reruns native reference tests and delivery/browser checks, repeats rendering to compare complete manifests, then publishes only a validated ZIP. Existing Web Demo workflow is unchanged.

## Distribution and release gates

The dedicated [Listening Lab workflow](../.github/workflows/listening-lab.yml) publishes `hydra-phase6-listening-lab` with 30-day retention. After downloading, extract the package, serve it locally and open `/player/`; artifacts are authenticated downloads, not public fetchable audio URLs. No online player deployment is required and no existing interface was overwritten.

First verified Linux artifact: [hydra-phase6-listening-lab](https://github.com/ovelhaaa/hydra-tape-engine/actions/runs/44416301599/artifacts/14478588859), rendered from commit `0eaf0485ae623328a7b881adf59cf2d3e131a758`, [successful Listening Lab run 44416301599](https://github.com/ovelhaaa/hydra-tape-engine/actions/runs/44416301599). Ubuntu 24.04, Python 3.11.18, NumPy 2.4.1, SciPy 1.17.0, Google Chrome 157.0.8103.50. The workflow verified the entire manifest was identical on repeat render, all 40 WAVs/20 CSVs were present and valid, and real browser tests passed. Linux audio/telemetry: 50,829,000 bytes. Inner ZIP: 41,692,580 bytes; downloaded Actions archive: 41,692,754 bytes, SHA-256 `a794d478a3583de1612dc9f4b5209507d0a70679fd58cacae1fe60dbcc82b579`. Retention expires 2026-12-18. The GitHub download endpoint was also successfully exercised.

Follow-up timeline metadata adds physical audio start/end and observed beat-phase wraps to every scenario, without changing WAV/CSV audio. Markers are validated for presence, labels and duration bounds. The PR links the newest verified package after its follow-up CI finishes. This report's explicit artifact URL remains an immutable reference to the first verified delivery, not a claim that it was generated by later documentation commits.

See [Listening Guide](PHASE6_LISTENING_GUIDE.md) for monitoring setup, all 20 listening targets, interpretation of intentional tape artifacts, notes and human approval criteria. The recorded partial human decisions are summarized above; unreviewed scenarios retain NOT REVIEWED and the whine remains unresolved. Issue #48 blocks claiming an entirely green strict regression suite or automatic merge readiness. Artifact delivery and human-listening readiness can be assessed separately from merge review. No merge is authorized by this report.
