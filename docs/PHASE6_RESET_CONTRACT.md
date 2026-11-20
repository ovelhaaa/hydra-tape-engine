# Phase 6.4 — Explicit reset contract and real-time safety

Date: 2026-11-20. This document formalises when `hydra_dsp_reset()` may be called, what it
costs, which hosts may call it, and the one host-level defect found and fixed.

## 1. What reset is

`hydra_dsp_reset(handle)` (`dsp/core/hydra_dsp_c_api.cpp`) calls `TapeCore::reset()`
(`dsp/core/tape_core.cpp`). It is a **destructive, synchronous** reset:

```cpp
std::memset(impl_->delayLine,  0, sizeof(float) * impl_->bufferCapacity);
std::memset(impl_->delayLineR, 0, sizeof(float) * impl_->bufferCapacity);
impl_->writeHead = 0;
impl_->framesSinceDelayEnable = 0xFFFFFFFFu;
// ... filters, magnetics, spring, reverse state, delay ramp ...
```

It allocates and frees nothing. The cost is two sequential `memset`s over
`2 × (bufferCapacity)` floats, plus small state clears.

| Configuration | Frames/channel | Bytes touched |
|---|---:|---:|
| 48 kHz, 2 s max delay (`hydra_dsp_create`) | 96 000 | 768 032 |
| 48 kHz, 8.1 s history (ESP32, `src/main.cpp`) | 388 800 | 3 110 432 |
| 44.1 kHz, 8.1 s history | 357 210 | 2 857 712 |
| 48 kHz, 16.1 s history (web, `hydra-processor.js`) | 772 800 | **6 182 432** |

The Phase 6.3 measurements recorded the equivalent wipe at **82 237 µs** on the ESP32-S3
for the ~3 MB configuration. The 6.18 MB web configuration is larger and is not measured.

This is deliberately different from delay **reactivation**, which uses the logical
`framesSinceDelayEnable` epoch and never erases history
([Phase 6.3 report](PHASE6_REVERSE_REALTIME_OPTIMIZATION.md)).

## 2. Call sites

| Site | Context | Class |
|---|---|---|
| `tests/core/smoke_core.cpp:46` | `main` | test |
| `tests/core/golden_regression.cpp:149` | reset-state scenario | test |
| `tests/core/regression_core.cpp:226,322` | state tests | test |
| `tests/core/reverse_core_test.cpp:230,403,651` | state tests | test |
| `tests/core/warp_core_test.cpp:684` | bias test render | test |
| `tests/core/tap_tempo_test.cpp:545` | direct `TapeCore::reset()` | test |
| `host/web/hydra-processor.js:267` | `onMessage`, `msg.type === 'reset'` | **AudioWorklet rendering thread** |
| `src/main.cpp`, `host/native/esp32/*` | none | — |
| `tools/**`, `scripts/**` | ctypes bindings only | — |

`HYDRA_DSP_PARAM_RESET` (id 29) also triggers reset, but neither host sends it: the web
parameter list explicitly excludes `reset`, and the ESP32 has no reset command.

## 3. Is reset reachable from the audio callback?

* **Native / ESP32 audio task: no.** `src/main.cpp` → `HydraDspEsp32Adapter::processStereoBlock`
  → `drainCommandQueue` → `hydra_dsp_process`. The command switch has no reset case, and the
  adapter exposes no reset method. The ESP32 "transport reset" commands reset the clock and
  estimators, not the DSP buffers.
* **Web: not inside `process()`, but on the same rendering thread.** The chain is
  `main.js` Reset button → `postMessage({type:'reset'})` → `hydra-processor.js`
  `port.onmessage` → `onMessage` → `api.reset(handle)` → `TapeCore::reset()`. AudioWorklet
  message handlers run in `AudioWorkletGlobalScope`, i.e. on the audio rendering thread.
  The wipe therefore runs between render quanta and can miss a deadline.

That is a genuine contract violation: the C API comment says the reset must be scheduled
outside the audio callback, but the only shipping caller did the opposite.

## 4. Contract

| Rule | Statement |
|---|---|
| R1 | `hydra_dsp_reset()` is **forbidden** while an audio callback or AudioWorklet render quantum is executing. |
| R2 | It is **forbidden** on any thread that shares the engine with a running audio thread without quiescing it. |
| R3 | It is **allowed** before the first `hydra_dsp_process()` call, and after the audio graph is stopped or suspended. |
| R4 | It is **allowed** on the control thread of an offline renderer where no real-time deadline exists. |
| R5 | A full reset is a product action (explicit "clear"), not a transport action. Transport stop/pause/locate must never reset the engine. |
| R6 | Delay reactivation must keep using the logical epoch; it must not call reset. |
| R7 | If a host needs a reset during performance, it must stop or suspend audio, perform the reset, and resume. |
| R8 | The ABI is unchanged: `hydra_dsp_reset` and parameter id 29 keep their destructive meaning. |

## 5. Fix applied to the web host

`host/web/main.js` no longer posts `reset` while audio is running. The reset button now:

1. suspends the `AudioContext` if it is running;
2. posts `{type:'reset'}`;
3. waits for the existing `stateAck` with `key === 'reset'` (2 s timeout);
4. resumes the context.

While the context is suspended no render quantum is due, so the 6.18 MB wipe cannot miss a
deadline, and the acknowledgement proves the wipe finished before audio resumes. If no
acknowledgement arrives the context is still resumed, and the UI reports
"clear sent, no acknowledgement" instead of claiming success.

No DSP, ABI or worklet message-format change is required; the worklet already replies with
`stateAck`. Browser-level verification of this flow (headless Chromium with an
`AudioWorkletNode`) is **pending**; the change is host-only and does not alter audio.

## 6. Residual risk

* The destructive semantics are retained, so a future host that calls reset from a render
  thread reintroduces the hazard. R1–R8 are the reference and should be enforced in review.
* A non-destructive reset (for example, a buffer generation tag) would remove the
  constraint, but it is a design change to an approved buffer model and is explicitly out
  of scope for Phase 6.4.
