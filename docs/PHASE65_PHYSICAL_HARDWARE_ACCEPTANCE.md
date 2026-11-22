# Phase 6.5 â€” ESP32-S3 physical hardware acceptance

Record date: 2026-11-21 (America/Sao_Paulo; raw captures retain UTC timestamps). Existing [PR #54](https://github.com/ovelhaaa/hydra-tape-engine/pull/54),
branch `feature/musical-validation-phase6`, target `main`. No merge.

## Recovery and identity

The first serial open already received actual Hydra telemetry, before any reset or
flash. COM15 matched USB VID/PID `303A:1001`, serial/MAC `B8:F8:62:E3:E4:A4`.
Windows reported usbser, WinUSB and usbccgp interfaces healthy, configuration error
code zero. Opening the port succeeded; another process was not preventing access
at that instant. No process-wide handle census was performed. DTR false and true
both delivered telemetry. A 100 ms RTS pulse did **not** demonstrate a reset:
the processed-block counter continued increasing and the port remained COM15.

| Port | VID/PID | Interface/driver | Before | After RTS | Response |
|---|---|---|---|---|---|
| COM15 | 303A:1001 | USB Serial/JTAG, usbser; JTAG WinUSB | Enumerated, status OK | Same identity/port | Real perf responses, increasing blocks |
| COM6/COM7 | Bluetooth | Bluetooth serial | Enumerated | Unchanged | Not addressed |

The prior silent-console cause remains **unproven**. Success in the current
session does not establish a historical boot, driver or power fault. The valid
help command is `?`; `help` falls through the old generic parameter CLI and its
`updated` reply is not proof that help was executed.

esptool 5.1.0 identified ESP32-S3 rev0.2, 240 MHz, embedded 8 MB PSRAM,
USB Serial/JTAG, MAC b8:f8:62:e3:e4:a4. Reading 628144 bytes from application offset
0x10000 preserved the original app before any write. Its SHA-256 was
`fbd35b0ee4f3a4f1f103f75c0000756923602679298d83980769a5cdf5aa9e25`.
The Phase6.4.1 preserved production file was
`ed68bc89e0f9bf508c2a181680aca80fec99602e6605e588b023d3e2872ddabd`.
Both passed ESP image validation; only 76 bytes differed, including Nov19 versus
Nov20 compilation metadata and ELF/image checksums. This does not prove which
source commit produced the initial image. It was not silently labelled current.

The correct production app was written once, verified by esptool and observed
responding after boot. Bootloader, partition table, NVS and filesystem were not
written. The original app and explicit app-only restoration command are preserved
locally under `artifacts/phase65-physical/`. A first readback failed on the Windows
cp1252 progress display; setting PYTHONIOENCODING=utf-8 corrected the host tool,
without changing the board or firmware.

### Controlled reset recovery

The final image reproduced the symptom after esptool's ordinary RTS reset:
USB still enumerated and the programmer still answered, but serial queries
returned zero bytes with either DTR state. Without writing flash again, the same
tool's `--after watchdog-reset read-mac` restored the application and captured
`BOOT START`, `SYSTEM READY`, all audio allocations, and matching runtime identity.
This is an intentional tool-controlled hardware reset, not an application watchdog
failure during a benchmark. It establishes that this reset path recovers the
observed symptom; the exact earlier retained boot/USB state was not read out and
must not be called proven. See Espressif's
[USB reset limitations](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/troubleshooting.html)
and the archived before/after reset logs.

Reproduction (first reidentify the MAC/USB serial and port):

```powershell
$env:PYTHONIOENCODING='utf-8'
& C:\.platformio\penv\Scripts\python.exe C:\.platformio\packages\tool-esptoolpy\esptool.py --chip esp32s3 --port COM15 --before usb-reset --after watchdog-reset read-mac
```

No manual pin manipulation, fuse writes, full erase, filesystem formatting or
bootloader change was used. If software recovery fails on a later session, close
the monitor, unplug USB, release BOOT, reconnect, pulse EN if available, reidentify
the port, then require the banner/identity before any benchmark.

Boot also exposed peripheral limitations: LittleFS mount reports a
corrupt directory pair and continues without a filesystem; OLED/I2C initialization
reports errors although its library announces success. These are not I2S errors.
Neither storage nor display was repaired or declared accepted; the measured
source is the internal generator, not MP3/filesystem playback or external I2S RX.

## Firmware and instrumentation boundary

Local platform: pioarduino espressif32 55.03.36, Arduino3.3.6,
ESP-IDF5.5.2.260116, Xtensa GCC14.2.0 esp-14.2.0_20251107, esptool5.1.0.
Production flags, DSP, ABI, goldens and musical parameters are preserved.
USB uses ARDUINO_USB_MODE=1 and ARDUINO_USB_CDC_ON_BOOT=1. Serial and Serial0
start after the initial 3-second delay and mutex creation; no wait-for-Serial loop.
Console/Arduino runs on core0; audio remains priority5/core1 with 32768-byte stack;
UI priority2/core0, LED priority1/core0. I2S remains master TX/RX, 44100 Hz,
stereo32-bit, 4 DMA buffers of 512 frames, pins BCLK15/LRCK16/DOUT17/DIN18.
The configured TX write timeout remains 50ms; failures yield and are counted.
No task affinity, priority or watchdog policy change is made.

### Measured host scheduling defect and correction

The original production firmware completed all narrow DSP-timer scenarios without
an overrun, but whole-run throughput was wrong. Delay took 30077.0ms for 10000
blocks; Multi-Head took 32790.1ms, rather than approximately 29025ms at 44100Hz.
The measured timing-control image left the same per-block stack scan in place
and added explicit timers. Ten real snapshots measured 1143â€“1144us just for
`uxTaskGetStackHighWaterMark(nullptr)`; Multi-Head full work was 3285â€“3415us.
Cumulative full-work peak reached 3723us with 1835 budget excesses while the old
DSP counter still read zero. Zero TX errors did not establish uninterrupted
sample-rate throughput. Audible starvation was not measured and is not asserted.

The contained correction retains the audio task handle and queries its persistent
stack watermark from the CLI when reporting perf/bench results. The expensive scan
is removed from the audio loop entirely. Priorities, core placement, audio/DSP
processing and buffers do not change. ESP-IDF documents the supported task-handle
query and its **byte**, rather than word, units in its
[FreeRTOS API](https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32s3/api-reference/system/freertos_idf.html#_CPPv426uxTaskGetStackHighWaterMark12TaskHandle_t).
The legacy `StackMinWords` serial key is retained for old parsers but documented
and parsed as bytes. Full-work peak/excess counters now include parameter updates,
stack-independent housekeeping, benchmark bookkeeping and requested snapshots,
up to the TX write. Blocking I2S waits are excluded. Source-I2S RX waits are inside
this interval; no RX-source acceptance is claimed here.

The preliminary control image and its measurements are explicitly diagnostic;
they do not replace final production measurements. Both the original production
run and the corrected production run are preserved separately. The DSP source,
adapter, ABI, platform flags and goldens are byte-unchanged from Phase6.4.1.

The application formerly exposed neither its ELF/commit nor PCM transmission
contents. The contained host instrumentation adds `identity` and `audiohealth`.
Identity stamps commit (including dirty marker), a hash of actual tracked firmware
inputs, and the runtime ESP app ELF hash. `audiohealth` requests one subsequent
PCM block, counts nonzero/full-scale words and peak magnitude, and reports actual
I2S return code/byte count. Requests/snapshots use the existing telemetry lock;
the CLI may wait with a timeout, while the audio task never waits for the CLI.
No sample log, allocation, DSP call or buffer reset is added. Snapshot scans occur
only on request, outside benchmark captures. A snapshot is not an analog recording
or proof of every sample in the whole run.

## Evidence and outstanding acquisition

The initial original-production seven-scenario run and five-minute capture are
kept separately from instrumented acceptance. Its DSP timer excludes stack
inspection, benchmark bookkeeping, parameter updates and I2S waits. Wall-time
throughput is therefore also examined; the narrow DSP counter alone cannot
establish whole-task deadline safety.

Classification: **PHYSICAL_RUNTIME_PASS** for production internal-source/driver-level I2S runtime.
Human final-package audition remains pending; no READY FOR FINAL RELEASE REVIEW or merge.
Current exact-head CI runs are maintained in [PR #54 checks](https://github.com/ovelhaaa/hydra-tape-engine/pull/54/checks), separately from the immutable physical firmware commit.

## Baseline comparability and unavailable equipment

Historical raw scripts used `revdiv 1/2` for cases labelled eighth/sixteenth;
the published C ABI enum maps those to whole/half. Correct eighth/sixteenth are
3/4 and are used now. The historical numbers remain historical, with that
comparability defect explicit. Each current scenario reapplies all base parameters
instead of inheriting previous stress/feedback state. Master volume is reduced to
10%; output-volume scaling is downstream of the DSP. Warp masks Reverse in actual
routing; stress with both UI flags set is Warp+spring, not concurrent Warp+Reverse.

The responsible user explicitly confirmed no monitoring and no external MIDI.
Physical analog audition is NOT_TESTED. MIDI is
**PHYSICAL_MIDI_CLOCK_NOT_TESTED**. Oscilloscope/logic-analyzer LRCK/BCLK and DAC
output capture were not available; configured/driver timing and successful I2S
writes must not be described as externally measured wire clocks or analog output.
Final Phase6.4.1 package audition remains **PENDING_HUMAN_AUDITION** independently.

## Corrected production measurements

Physically running source commit: `990e5e9bc0cb22e469705555c26ef1ef9c94ae06`. Binary SHA-256:
`0a2c42cef5921a397b95576cdcb87f589bb707195c6f73e6faeeac4f2ebf13e5`. Runtime ELF SHA-256:
`5069e445d11390370f33bd4e650830a389ee6619db48c70c6c72ce9373999f6e`. Firmware-input SHA-256:
`54c302c03f9d8ea43a22ea072d8d0e13c5318d4d8780814a09990618932f05f4`.

Each scenario has 256 warmup + 10000 measured blocks, stereo/44100Hz/128 frames, CPU240MHz. Times are microseconds and use the retained on-device histogram, not reconstructed truncated serial data.

| Scenario | Mean | P50 | P95 | P99 | Peak | DSP overruns | I2S failures |
|---|---:|---:|---:|---:|---:|---:|---:|
| delay | 1851.19 | 1834 | 1897 | 1921 | 2171 | 0 | 0 |
| reverse-quarter | 1916.53 | 1898 | 1964 | 2082 | 2347 | 0 | 0 |
| reverse-eighth | 1915.21 | 1895 | 1965 | 2101 | 2397 | 0 | 0 |
| reverse-sixteenth | 1914.52 | 1891 | 2015 | 2102 | 2391 | 0 | 0 |
| reverse-feedback95 | 1915.59 | 1897 | 1962 | 2080 | 2346 | 0 | 0 |
| multihead | 2128.78 | 2109 | 2176 | 2215 | 2468 | 0 | 0 |
| stress | 1988.36 | 1972 | 2046 | 2076 | 2320 | 0 | 0 |

All seven benchmark heap endpoints were 125200 bytes; PSRAM endpoints 1663048 bytes. Stack watermark was 27268 **bytes**, not words. Maximum cumulative parameter update in the seven scenarios was 433us. General perf heap values are 125296 bytes because CLI temporary allocation differs from the bench reporting context. Zero endpoint drift does not prove a per-block allocation count.

The highest P99 is 2215us (76.31% of the 2902.49us budget, 23.69% P99 margin). The preferred 70% target is not achieved outside Delay; it is not turned into a changed DSP threshold. Whole-work counters independently cover the formerly excluded housekeeping and parameter-update interval. CPU frequency and DSP budget ratios are measured; per-core idle utilization was not measured.

The four comparable historical P99 changes are Delay +0.42%, quarter -0.62%, high-feedback quarter -1.56%, Multi-Head +1.00%. No timing regression in those cases justifies a DSP change. Eighth/sixteenth are not same-division comparisons; stress resets to four heads/120BPM while the historical sequence inherited seven heads/90BPM. Their apparent deltas (+3.45%/-0.43%/+1.32%) are explicitly not isolated regressions.

The corrected benchmark wall durations are 29019.5–29025.4ms, giving 44099.3–44108.3 frames/s from counters divided by wall time. This includes DMA buffering/window edge effects; it is not an oscilloscope measurement of LRCK.


## Sustained test, capture failures and integrity

The completed new run lasted **300.2168358 seconds**, 103399 additional audio
blocks, Reverse quarter/95% feedback, tone90/head-bump90 and the recorded base
parameters. All 31 observed samples reported zero DSP overruns, zero full-work
budget excesses and zero I2S TX write failures. No reset/crash/watchdog was observed
during acquisition; the intentional esptool recovery reset preceded it. Full-work
peak remained 2820us, leaving 82.49us at that observed worst case. Heap stayed
125296 bytes, PSRAM1663048 bytes, stack watermark27268 bytes. PCM snapshots
were nonzero, unsaturated, and transmitted 1024 bytes with ESP_OK; stack-scan cost
in the audio task was zero. Snapshots are not a continuous floating-point NaN/Inf
counter, continuous saturation detector, DAC recording or electrical clock capture.

The original-production five-minute run is archived separately: it had 97828
blocks and no narrow-timer/TX-error flags, but its slower throughput plus the
measured stack-scan control invalidates using those narrow flags as whole-task
acceptance. Historical Phase6.3 97241-block/2808us results are not reused as current.

Two final-host capture attempts were rejected and preserved. The first perf reply
exceeded the one-second serial-response window: its remaining bytes arrived in
cleanup. Its one sustain sample was discarded. A resume attempt then rejected a
raw identity string because a leftover help line prefixed it, despite identical
commit/source/ELF fields. The parser now consumes the complete help reply and
compares identity from its explicit field boundary. Perf capture waits up to five
seconds, still requiring the full terminator and all fields. This changes host
communication framing, not DSP deadlines or acceptance thresholds. JSON replacement
is atomic. Both failed files and SHA links remain in the final evidence. The seven
complete, raw-verified benchmarks were retained; a new uninterrupted five minutes
was collected from zero elapsed time. No missing values were filled with zero.

The instrumented capture script verifies USB serial/VID/PID, firmware binary SHA,
running commit/source/ELF, configuration, actual generator off/on PCM, all seven
raw configurations/counts and snapshot TX success, monotonically advancing task
counters, stress controls, complete five-minute sampling and cleanup. It does not
flash. Its resume option accepts only independently complete raw/JSON-matching
benchmarks and the same running firmware. Missing data, a wrong identity, reset,
short capture, failed TX or deadline excess rejects the run. The offline validator
checks all file hashes, raw/JSON metrics, preserved failed attempts and gates; its
CI job explicitly validates an archive rather than pretending to rerun hardware.
Negative incomplete-capture and corrupted-hash checks both rejected their inputs.

Reproduce after independently verifying/uploading the identified firmware, with
matching hashes from `identity` and the local generated header:

```powershell
python scripts/capture_phase65.py --serial B8:F8:62:E3:E4:A4 --firmware-sha256 <binary-sha256> --verified-upload-log <actual-esptool-log> --expected-commit <running-commit> --expected-source-sha256 <running-source-sha256> --output artifacts/phase65-physical/new-run
python scripts/validate_phase65_evidence.py docs/evidence/phase65-physical
```

The board is left with generator off, volume10%, feedback40%, Reverse/Warp/spring
off. No final human listening approval, external MIDI result, filesystem/display
acceptance or analog quality claim is inferred. Fresh local Native16/16,
WASM16/16 and strict Native/WASM0/0 logs are archived. Mandatory final-head GitHub
workflows and the published physical-evidence artifact are linked in PR#54.


## Phase 6.6 audit addendum - 2026-11-21

Archived raw/JSON/SHA evidence revalidated with validate_phase65_evidence.py:
PASS. No new measurement or physical audition performed. Firmware identity and
original acquisition results above are preserved. Audited candidate a7e2202 has
identical firmware inputs to measured 990e5e9. Full-work 2820us vs 2902.49us leaves
82.49us (2.84%) observed margin, not a universal deadline guarantee. Analog DAC,
external I2S RX/MIDI Clock, LittleFS and OLED/I2C remain outside acceptance.
Final package human audition remains PENDING_HUMAN_AUDITION; see
[Phase 6.6 audit](PHASE66_FINAL_MUSICAL_ACCEPTANCE.md).


### Phase 6.6 human-signoff follow-up - 2026-11-21

Alexandre supplied ten final scenario PASS decisions and explicit overall musical
approval. This is the verified offline A/B package acceptance, not physical DAC
audition. Original Phase 6.5 firmware, measurements, observed 82.49us margin and
analog/RX/external MIDI/LittleFS/OLED exclusions remain unchanged.
