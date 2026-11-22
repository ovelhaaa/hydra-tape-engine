# Phase 6.6 — Release Decision Record (historical evidence retained)

Updated: 2026-11-21. Repository `ovelhaaa/hydra-tape-engine`, existing
[PR #54](https://github.com/ovelhaaa/hydra-tape-engine/pull/54), branch
`feature/musical-validation-phase6` to `main`. No merge performed.

## Phase 6.6 current audit — 2026-11-21

**PENDING HUMAN AUDITION** (`PENDING_HUMAN_AUDITION`). Audited candidate HEAD:
`a7e22022f71f670e51a77e63149148c88605d9f0`; no later remote commits at initial audit.
All five mandatory workflows passed on that exact HEAD: Core44961696922,
Bias44961696854, Reverse44961696810, Listening44961696878, Web44961696825.
Native/WASM16/16, ABI/goldens and strict Ubuntu24/26 max_abs=RMSE=0 confirmed.

[Complete Phase6.6 audit and session](PHASE66_FINAL_MUSICAL_ACCEPTANCE.md) and
[verified package hashes](evidence/phase66-final-audit.json) supersede the older
current-status prose below; historical run links remain labeled as historical.
Published final artifact14730235810 matches this candidate commit, with ten
pairs, twenty PCM24/48kHz stereo WAVs and ten CSVs, all hashes/frames valid.
No DSP difference from Phase6.4.1; no regeneration. No human final export received:
**all ten scenes NOT REVIEWED**, including priority speed transition08.
Earlier approvals remain bound to their original packages.

Measured firmware remains990e5e9bc0cb22e469705555c26ef1ef9c94ae06; its exact
firmware inputs match the candidate. Archived hardware evidence revalidated:
PASS, seven cases and300.2168s sustain, zero observed full-work/TX failures.
Worst P99=2215us, full-work peak2820us against2902.49us budget: only82.49us
observed margin; no universal deadline guarantee. Physical acceptance excludes
analog DAC quality, external RX/MIDI, LittleFS and OLED/I2C. API1.7, Native,
WASM and ESP32 build compatibility retained. Peripheral errors and limited
runtime margin remain residual risks. No musical integrity claim before audition.

Any subsequent Phase6.6 commit needs its own five green workflows; these runs
are not transferred to another HEAD. Final classification stays pending until
validated human JSON plus explicit overall approval and exact-head CI review.
No merge authorized. Issue48 closure draft prepared, not posted/closed.

## Classification

**PHYSICAL_RUNTIME_PASS — PENDING_HUMAN_AUDITION.**

Phase6.5 production source `990e5e9bc0cb22e469705555c26ef1ef9c94ae06`
is identified by its running ELF/source hashes. Seven 10000-block/256-warmup
scenarios and a new 300.2168-second, 103399-block sustained run completed with
zero DSP/full-work overruns and zero observed I2S TX write failures. Heap/PSRAM
were stable, stack minimum 27268 bytes. Maximum whole work was 2820us against
2902.49us; worst P99 was 2215us (76.31%, above the preferred 70% target).
The full [physical record](PHASE65_PHYSICAL_HARDWARE_ACCEPTANCE.md) and
[hash-validated raw evidence](evidence/phase65-physical/manifest.json) preserve
both failed host-capture attempts instead of treating them as five-minute passes.

The measured correction is host scheduling: a per-block stack-watermark scan
cost 1143–1144us outside the former DSP timer. Querying that retained watermark
from the CLI removes the hidden deadline stall. DSP, ABI, flags and goldens are
unchanged. USB silence was reproduced after ordinary RTS reset; esptool's
watchdog reset recovered application boot without another flash.

This is internal-generator/driver-level I2S acceptance, not analog or external RX
acceptance. No monitoring or MIDI source was available. LittleFS mount and OLED/I2C
errors remain documented peripheral limitations. Human audition of the final
listening package remains pending. **No READY FOR FINAL RELEASE REVIEW or merge.**

Current Phase6.5 software CI must be read from the exact-head checks and run links
in [PR #54](https://github.com/ovelhaaa/hydra-tape-engine/pull/54/checks); it is not
inherited from Phase6.4.1. Fresh local Native/WASM suites each pass 16/16, strict
max_abs/RMSE=0, with [local logs](evidence/phase65-physical/software/).
The software run URLs retained below are explicitly the Phase6.4.1 baseline.

In the Phase6.4.1 baseline, all five required workflows passed on code HEAD
`e2c94972cc295f2c8ac81ea0b0ecc410868b8961`. Final record changes are followed by
another complete CI execution; the PR contains its latest-head links. Human
listening and physical runtime are independent pending release gates. No full
release or musical approval is inferred.

The earlier Phase 6.4 all-green claim was incorrect: Windows observations were
extrapolated to Linux. Initial local/remote HEAD was
`eaa233ff32e169a2ff720d4bdace3d636c10eb7a`, with no later commits.
[Baseline hashes/failures](evidence/phase641-baseline.json) and
[initial artifacts](evidence/phase641-initial-artifacts.json) preserve the audit.

## Gate matrix

| Gate | Result | Evidence |
|---|---|---|
| Native | PASS | [Core](https://github.com/ovelhaaa/hydra-tape-engine/actions/runs/44842365956), CTest 16/16, ABI and unchanged goldens |
| WASM | PASS | [Reverse](https://github.com/ovelhaaa/hydra-tape-engine/actions/runs/44842365989), complete CTest 16/16; Bias functional suite 14/14 plus separate strict comparator |
| Native/WASM Strict | PASS | Core clean Ubuntu 24.04 and 26.04, all five scenarios / 10240 stereo frames, max_abs=0, RMSE=0; input/LUT/state verification |
| Bias Anti-Aliasing | PASS | [Bias](https://github.com/ovelhaaa/hydra-tape-engine/actions/runs/44842366000), six variants, spectral/transfer 49/49, standalone whine, plots and 60-pair delivery |
| Reverse | PASS | Reverse current functional tests and frozen Phase 6.3 optimization comparison 10/10 bit-exact |
| Feedback | PASS | Reverse full-engine 27/27; six 60 s cases; internal windows match full-engine bits, maximum RMS growth +0.012521058212 dB |
| Transitions | PASS | Reverse, five parameters at 44.1/48 kHz: max 0.017967507243 / 0.014195829630 <= 0.03 |
| Web Demo | PASS | [Web](https://github.com/ovelhaaa/hydra-tape-engine/actions/runs/44842365973), clean WASM build and host 40/40 |
| AudioWorklet Reset | PASS | Web real Chrome 157.0.8103.78, actual AudioContext/AudioWorklet/WASM, A–E all pass |
| Listening Lab | PASS | [Listening](https://github.com/ovelhaaa/hydra-tape-engine/actions/runs/44842365950), final 10 pairs / 20 PCM24 WAVs / 10 CSVs, hashes, real Chrome player |
| ESP32-S3 Build | PASS | Core production and Reverse production/stage-profile builds; local production RAM 26304 B / flash 627743 B |
| ESP32-S3 Runtime | PASS | Phase6.5 identified production, seven complete cases, 103399-block/300.2168s sustain; [raw evidence](evidence/phase65-physical/final-results.json) |
| Whole-task work / memory | PASS | Full-work peak 2820us, zero excesses; heap125296/PSRAM1663048 stable, stack27268 bytes |
| Human Listening | PENDING | PENDING_HUMAN_AUDITION; no previous signoff transferred |
| ABI C 1.7 / goldens | PASS | Native/WASM symbol, legacy-consumer and golden checks; fixtures not regenerated |

## Numerical finding

The official discrepancy was reproduced without cache. Ubuntu 26.04 defaults to
`-march=x86-64-v3` with FMA enabled; Ubuntu 24.04 did not contract these operations.
Default GCC14 and GCC15 on Ubuntu26 produced the same failures, and Windows with
`-mfma` reproduced them exactly. Input and canonical LUT bits were already equal.
The downloaded uncontrolled executable contains 544 FMA-family instructions.

With only `-ffp-contract=off`, all five outputs and every probed filter/per-channel
state matched, without instrumented-output perturbation. CMake now applies this
policy to C++ targets; manual diagnostic/A-B builds use it too. PlatformIO ESP32
flags and production DSP source are unchanged. Thresholds remain 1e-5 / 1e-6,
individually enforced per scenario as well as in aggregate. Negative input/LUT
corruption tests fail before DSP comparison; an individual RMSE regression cannot
hide behind a passing aggregate.

Input float32 LE SHA-256:
`e629e0248b9833c0f280f3771a07b95047e89d9efb3e9afddc06d3209ea559dc`.
LUT float32 LE SHA-256:
`d1724b91fed053e18aa3674b7862e8322b9ec9efbddab337fbddc030192238e5`.
Each has zero divergent values, zero ULP and no first mismatch. Independent LUT
CTest validates all 1025 entries and 80-digit Decimal regeneration. Toolchains:
Ubuntu24 GCC13.3/glibc2.39; Ubuntu26 GCC15.2/glibc2.43; Emscripten3.1.62 in both.
CMake caches, compile commands, checkout/source/runner/WASM hashes are artifacts.
[Controlled evidence](evidence/phase641-contraction-control.json).

## Bias, feedback and transitions

Removed obsolete `lut.init()` use from modern wrappers. Historical variants
explicitly restore the frozen fa43cde LUT and initialization; their provenance
identifies the current engine plus frozen magnetic model, not an original full
engine binary. The averaged variant uses exact production source. Reports record
model/LUT source hashes, compiler, flags, commit and binary hashes.

Linux spectral/transfer gates are 49/49. At 48 kHz, 6 kHz residual reduction is
122.492178 dB; sine440 spurious level is -138.873151 dBFS and high-drive spurious
level -130.161493 dBFS. Whine ablations are deterministic, finite and valid PCM24.
An optional historical-reference check passed locally with exact preserved SHA;
Linux does not pretend to have compared that Windows reference. Historical LUT
Native/WASM differences are exposed explicitly, not hidden by a modern probe.

The old six-second engine rule still reports 23/27, retaining four named decay
observations. Those four are superseded only by the separately required measured
60-second stability gate. Other original assertions must pass. Raw spectra are
unnormalized FFT-power observations with explicit units, not an invented spectral
acceptance gate. Four tails show limited sustain; both Reverse tails decay to
zero (-240 dB numerical floor). Maximum output peak 0.90766525; zero limiter-ceiling
samples; no NaN/Inf. Probe input fixtures/128-frame blocks are shared and output
windows must match full-engine bits. All five parameter-transition scenarios remain.

## Real AudioWorklet reset

Suspended-context messages and the real DSP reset/ack were observed in Chrome.
Requests coalesce, acknowledgements carry request IDs, errors reach the host and
an absent ack rejects after 2000 ms. Failures keep audio suspended; stopped audio
stays stopped. No graph reconstruction or new DSP reset semantics were needed.

Observed Linux end-to-end times: running 3.2 ms; stopped 1.5 ms; controlled
exception 3.5 ms; repeated concurrent requests 3.1 ms; timeout 2000.7 ms.
These are host timings, not isolated buffer-wipe CPU measurements. Fault injection
uses only a test subclass. Other browsers and hardware audio latency remain outside
this Chrome evidence. See [reset contract](PHASE6_RESET_CONTRACT.md).

## Verified listening delivery

[Download final review package for the validated code HEAD](https://github.com/ovelhaaa/hydra-tape-engine/actions/runs/44842365950/artifacts/14681004034).
The artifact was downloaded again, its inner ZIP and manifest/file hashes verified,
and its physical contents counted: 20 stereo PCM24 WAVs at 48 kHz, 10 CSVs,
manifest, DSP metadata, ten-pair guide and player. Nine pairs have 288000 frames;
the feedback pair has 2880000 frames (60 s). Audio/telemetry total: 66567413 bytes.

Inner ZIP SHA-256:
`3e7daaa73d244d834a4e63a2e44b4e34879da0df541623ebb434eb41ee7b7440`.
Manifest SHA-256:
`99e551e69a650671cb564ec3d2d58fa6ce2a4a856c2a0daac96b0ec5df41e540`.
Reference commit: `e2c94972cc295f2c8ac81ea0b0ecc410868b8961`;
A-side DSP commit: `7431a6db05abfde97838721cd089f94c7fed5047`.
The PR also links any newer artifact regenerated by the final record-only CI.
Artifacts have 30-day retention; regenerate after expiration.

PCM24 correction: one normalized signed LSB is 2^-23 = 1.1920929e-7.
A 3e-5 difference is approximately 252 LSB (-90.46 dBFS peak error), not a fraction
of an LSB. Small amplitude error and automated playback do not establish human
approval. Prior Phase6.2/6.3 listening decisions remain historical only.

## Physical follow-up — historical Phase6.4.1, superseded by Phase6.5 above

COM15, VID303A:1001, serial/MAC B8:F8:62:E3:E4:A4 identified by esptool:
ESP32-S3 rev0.2, 8 MB PSRAM, USB Serial/JTAG. RTS reset succeeded at the programmer
level; firmware banner/help returned zero bytes. No firmware was flashed in this
phase. Local firmware SHA-256:
`ed68bc89e0f9bf508c2a181680aca80fec99602e6605e588b023d3e2872ddabd`.

Runtime/audio loop, benchmarks, overruns, I2S faults, stack, heap and PSRAM stability
are **not measured** for this phase. Older physical results are not reused.
Manual procedure: unplug/reconnect USB power with BOOT released for normal boot,
pulse EN if accessible, identify the reenumerated port and confirm banner/help
before benchmarks. Repeat delay, Reverse1/4,1/8,1/16,95% feedback, Multi-Head and
combined stress. Require zero overruns/I2S faults, stable heap/PSRAM, sufficient
stack and no deadline regression. External MIDI hardware also remains unavailable.
Physical access does not block CI repair; it remains a release gate.

## Reproduction and commit trail

Run the checked-in Core, Bias, Reverse, Listening and Web workflows on this branch.
The long 60-second diagnostics run once, in Reverse; Bias retains its historical
six-second measurements. Local Windows tests used an accessible workspace TMP/TEMP
directory after OS-temp file-open failures; tests were rerun, not disabled.

Commits after audit: `8b9c363` clean probes; `2dd13e6` diagnostics/reset/gates;
`866ab96` arithmetic controls and evidence; `60a9da6` FMA rounding correction;
`e2c9497` exact diagnostic inputs, standalone whine and final listening delivery.
This record commit adds verified evidence and corrects the silence classification
label without changing DSP, thresholds or acceptance outcomes. A final full CI run
is required and linked in the PR. No new effects, Reverse rewrite, ABI, GTC,
Tap Tempo, MIDI Clock, goldens or phase-averaged bias changes. No new PR or merge.
