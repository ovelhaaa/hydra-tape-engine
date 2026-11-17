import urllib.request
import json
import subprocess

proc = subprocess.run(['git', 'credential', 'fill'], input='protocol=https\nhost=github.com\n', capture_output=True, text=True)
token = None
for line in proc.stdout.splitlines():
    if line.startswith('password='):
        token = line.split('=', 1)[1]

if not token:
    raise RuntimeError("Could not find GitHub token in git credential manager")

body = """# HYDRA TAPE ENGINE 2.0 — PHASE 4: TAP TEMPO V1 & REAL-TIME HANDOFF HARDENING

## 1. Executive Summary & Objective

This PR delivers the complete, performance-ready implementation of **Tap Tempo V1** integrated into the **Global Transport Clock (GTC)**, hardened with a lock-free real-time safe handoff architecture across ESP32-S3 FreeRTOS tasks, Web AudioWorklet, and Native desktop environments.

- **PR Status:** `READY FOR MERGE REVIEW / READY FOR HUMAN LISTENING` (Do NOT auto-merge)
- **Target Branch:** `main` (Merge of PR #50 / GTC 1.5 baseline)
- **Feature Branch:** `feature/tap-tempo-v1`
- **Head Commit:** `4cd10d9`

---

## 2. Real-Time Safe Handoff & Concurrency Architecture

### Concurrency Audit & Resolution
- **Identified Risk:** In previous revisions, serial CLI commands executed on Core 0 (`loopTask`) called `tape->tapTempoEvent(0)`, which invoked `TapeCore::updateParams()` directly on Core 0 while Core 1 (`audioTask`) was actively reading and mutating DSP state during `processStereoBlock()`.
- **Architectural Solution (Single-Owner DSP Mutability):**
  - **Single Execution Owner:** The Core 1 audio callback is the SOLE owner authorized to mutate DSP structures (`TapeCore`, `GlobalTransportClock`, `ReverseWindows`, filter/delay coefficients).
  - **Lock-Free SPSC Command Queue (`dsp/core/spsc_queue.hpp`):** 32-slot ring buffer with cache-line separation (`alignas(64)`) separating `head_`, `tail_`, and buffer storage to prevent cache thrashing between Xtensa LX7 dual cores.
  - **Zero Locks in Audio Path:** Audio callback drains the queue via non-blocking `pop()` at block boundaries ($128\\text{ frames} / 44.1\\text{ kHz} \\approx 2.90\\text{ ms}$ max delivery latency).
  - **Overflow Protection:** Non-blocking rejection (`push()` returns `false`) if queue exceeds 32 items, safely dropping excess events without locking or memory reallocation.
  - **Triple Buffer State Publication:** `StatePublisher<T>` lock-free triple buffer completely eliminates data races under C++17 memory model, allowing Core 0 (CLI/UI) to query `tap status` and `transport status` with 100% tear-free reads without locking Core 1. Producer is 100% wait-free O(1).

---

## 3. Parameter Update Elimination & State Machine Hardening

- **Structured Tap Result:** `TapTempo::recordTap()` returns `TapResult { bool accepted; bool tempoUpdated; double newBpm; TapTempoStatus status; TapRejectionReason rejection; }`.
- **Elimination of Superfluous `updateParams()` Calls:**
  - **Tap 1 (Sequence Start):** Establishes time base reference only (`tempoUpdated = false`). `updateParams()` is NEVER invoked.
  - **Isolated Outliers (>20% deviation):** Marked as candidate without updating tempo (`tempoUpdated = false`). GTC remains locked on established cadence.
  - **Debounce Bounces (<80 ms):** Rejected with `rejection = Bounce`. `updateParams()` is NEVER invoked.
  - **Out-of-Range Taps (<30 or >300 BPM):** Rejected with `rejection = OutOfRange`. `updateParams()` is NEVER invoked.
  - **Session Timeout (>3.0 s):** Resets detection sequence; current GTC BPM is preserved intact.
  - **Tap Reset (`tap reset`):** Resets estimator state only; does NOT reset GTC BPM.
  - **Confirmed Cadence Change / Tap 2 / Inliers:** Updates tempo only when `newBpm != currentBpm`.

---

## 4. Timestamp Policy & Frame Zero Disambiguation

- **Sentinel Disambiguation:** Introduced `kTapTempoUseCurrentTime = UINT64_MAX` and `TapeCore::tapTempoEventNow()`. Frame 0 is treated as a valid temporal coordinate rather than an ambiguous sentinel.
- **Physical Arrival Capture (ESP32-S3 CLI & Future Footswitch):**
  - CLI captures hardware microsecond timer (`esp_timer_get_time()`) immediately upon receiving `tap` before parsing or printing.
  - Audio task computes $\\Delta t = \\text{nowUs} - \\text{arrivalUs}$ and converts to audio frames $\\Delta \\text{frames} = (\\Delta t \\cdot f_s) / 10^6$, precisely subtracting out CLI task scheduling jitter.
  - Prepares drop-in compatibility for physical GPIO footswitch ISR.
- **Web AudioWorklet:** Tap events are processed at block boundaries, with arrival time aligned to worklet quantum frame position.

---

## 5. Investigation & Fix: 5-Minute Benchmark Block Discrepancy

- **Discrepancy:** Initial 5-minute sustained test recorded 113,981 blocks instead of expected 103,359 blocks (~330.82s vs 300.0s).
- **Root Cause:** In `src/main.cpp`, `cmd == "perf"` previously did not inspect `valStr`, so `perf r` was ignored and `g_dsp_perf` was never reset at benchmark start. The counter accumulated the board's uptime since cold boot (~30.8s / 10,622 blocks of boot and chain setup commands).
- **Fix:** Implemented `perf r` / `perf reset` in `src/main.cpp`.
- **Hardware Verification:** 20.47s wall-clock test on physical ESP32-S3 (COM15) yielded exactly 7,062 blocks (20.50s audio time, +0.03s difference / <0.15% clock drift).

---

## 6. Comprehensive Test & Benchmark Validation

- **Native CTest Suite (14/14 Suites Passed, 100%):**
  - All 20 Tap Tempo unit and concurrency tests passed in `hydra_tap_tempo_test`:
    - Tests A–L: Core estimation, debounce, outlier rejection, intentional cadence change, timeout, sample rate independence, timestamp validation, transport integration, reverse synchronization, block independence, C-API state query, legacy ABI structs (116, 140, 148 bytes).
    - **Test M:** Lock-free SPSC queue FIFO order, overflow protection, and Triple Buffer state publisher.
    - **Test N:** Verification that Tap 1, isolated outliers, debounce bounces, and out-of-range taps never trigger `updateParams()`.
    - **Test O:** Multi-threaded concurrency stress test (Core 0 producer thread sending 200 taps vs Core 1 consumer thread running 2,000 blocks with concurrent telemetry polling).
    - **Test P:** Interleaved transport commands (`play`, `pause`, `reset`, manual BPM) and tap tempo under continuous audio processing.
    - **Test Q:** StatePublisher Producer/Consumer Stress & High-Frequency (100k publishes, zero corruption, consistent reads).
    - **Test R:** Snapshot Consistency Invariants (`inverseGeneration == ~generation`, zero torn reads).
    - **Test S:** Slow Reader Protection (Writer safely cycled without corrupting reader's buffer).
    - **Test T:** SPSC Queue Overflow & FIFO Order (Capacity 32 exact, deterministic non-blocking drop).
- **Web AudioWorklet Suite (24/24 Passed, 100%):**
  - All Node.js tests in `host/web/tests/` passed, covering UI controls, worklet routing, schema serialization, and telemetry.
- **ESP32-S3 Physical Benchmark (COM15, 240 MHz, 44.1 kHz / 128 frames):**
  - Average DSP block time: 1461 µs (50.8% load vs 2902.5 µs budget).
  - Peak DSP block time: 1787 µs.
  - DSP Budget Overruns: **0**.
  - I2S TX Write Failures: **0**.
  - Internal Heap & SPIRAM (PSRAM): **0 bytes leaked** (perfect stability).
- **Hydra Listening Lab Review Package:**
  - 10 WAV scenarios preserved in `artifacts/listening-lab/tap-tempo-v1/` with manifest and SHA-256 hashes.
"""

req = urllib.request.Request('https://api.github.com/repos/ovelhaaa/hydra-tape-engine/pulls/51',
    data=json.dumps({'body': body}).encode('utf-8'),
    headers={
        'Authorization': f'Bearer {token}',
        'Accept': 'application/vnd.github.v3+json',
        'User-Agent': 'Hydra-Agent'
    },
    method='PATCH')

with urllib.request.urlopen(req) as resp:
    data = json.loads(resp.read().decode())
    print('PR 51 Updated Successfully!')
    print('URL:', data['html_url'])
    print('Title:', data['title'])
