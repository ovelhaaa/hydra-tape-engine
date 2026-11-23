# Phase 7.4C.2 reset follow-up

Baseline: 503688c18ffe61bac5221ef169f64ba915b6d4d1, PR #61.
On 2026-11-23 Web Demo build and Unified Motion Lab both failed in
webUnifiedMotion.test.mjs: Stereo Motion=0 must link L/R. Phase 6 Listening
Lab passed. Reproduced locally using freshly rebuilt WASM.

Cause: explicit reset initialized motion control smoothing to 100%, recording
30 ms of unintended stereo motion before reaching the requested zero. Magnetic
state retains the resulting channel differences. Reset now initializes the two
control states from the already computed targets. Motion algorithms unchanged.
Fresh WASM webUnifiedMotion test: 2 passed, 0 skipped. No flash or merge.
