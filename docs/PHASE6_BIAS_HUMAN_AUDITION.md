# Phase 6.2 — human candidate approval

Received 2026-11-19 from the responsible user: **“o candidato foi aprovado em todos os testes”**.

**Candidate human audition: PASS — 12/12 scenarios. Technical release: BLOCKED — REGRESSION / PERFORMANCE. No merge.** The statement is recorded as approval of the listening scenarios, not a claim that failing automated or physical tests have passed.

## Evidence and scope

[Original review JSON](evidence/phase6-bias-human-reviews-f0c16cd.json), [reviewed manifest](evidence/phase6-bias-human-reviewed-manifest-f0c16cd.json), [signoff summary](evidence/phase6-bias-human-signoff.json).

Reference commit: `f0c16cddb891ba9079fa4dbbf98f29132ed5c9f0`. Review SHA256: `16ed04f9d3e8c6b73c3f272e3d728bbfba65009e91a4e2576783ad41b6f1038d`. All 60 exported WAV/CSV hash triplets match the local Windows package manifest. This binds the approval to the exact content heard; it is not an assertion of byte identity with the separately rendered Linux artifact or later commits. Subsequent documentation commits do not alter the approved DSP algorithm, but approval is not automatically assigned to newly rendered files.

All 12 `averaged` comparisons explicitly contain state PASS:

| Review ID | Scenario |
|---|---|
| 05_01_averaged | Clean guitar isolated notes |
| 10_02_averaged | Clean guitar chords |
| 15_03_averaged | Driven guitar |
| 20_04_averaged | Monophonic synthesizer |
| 25_05_averaged | Polyphonic synthesizer |
| 30_06_averaged | Drum machine |
| 35_07_averaged | Moderate feedback |
| 40_08_averaged | High feedback |
| 45_09_averaged | Reverse V2 |
| 50_10_averaged | Multi-Head |
| 55_11_averaged | Digital silence with hiss |
| 60_12_averaged | 440 Hz sine |

The first candidate entry includes **“No audible whine”**. Other candidate comments are empty. Per-criterion dictionaries are empty, so individual scores for magnetic compression, harmonics, stereo or the remaining criteria are not invented. Scenario-level PASS and the user's explicit overall approval are preserved.

Across the complete 60-pair matrix: 24 PASS, 4 FAIL and 32 NOT REVIEWED, counting absent review records as NOT REVIEWED. The four FAIL entries are oversampled comparisons 04, 09, 14 and 19, with comments that the whine remains audible. These rejected diagnostic alternatives do not negate the candidate's 12/12 approval, and unreviewed alternatives are not inferred to pass.

## Remaining release gates

Human audition is cleared for this candidate/package scope. Existing automated regression assertions, strict Native/WASM equivalence, parameter-transition/feedback criteria and physical Reverse deadline overruns remain as documented in the [technical report](PHASE6_BIAS_ANTIALIASING_REPORT.md) and [physical follow-up](PHASE6_BIAS_HARDWARE_FOLLOWUP.md). No threshold, golden, DSP source or firmware was changed to record this approval. PR #54 remains open; no merge authorization is inferred.
