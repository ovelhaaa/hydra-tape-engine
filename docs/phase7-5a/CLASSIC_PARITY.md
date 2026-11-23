# Classic parity

Reference chain:
- Classic C frozen branch d2d36dd2b5d0fe06e66aca6d309a532a13001d87.
- Organic Motion reference c1331a7d81b8b989df0ed523c9ae10c73c21a15e.
- PR #61 stabilized base c1f996b0dbe22117238252b6d5530e15d5ebe26f.

`verify_phase74c_equivalence.py` independently compiles the frozen sources and
current engine, with Classic C and identical rounding flags. All eight M0 scenes
(single/multi-head, Reverse, Ping-Pong, Spring, high feedback, wow/flutter and
automation) passed bit-exact. M1/M2/M3 each passed bit-exact versus Organic Motion.

`phase75a_studio_lab.py` additionally builds c1f996b as a separate shared library.
32 Classic scenes (4 motion modes x single/multi, Ping-Pong, Reverse/Smear,
Warp, Spring, Freeze and high feedback) passed byte-for-byte comparison. SHA256
values are recorded in metrics.json. No new output arithmetic executes at the
settled Classic endpoint. There is no Repeat Aging C1/C2/C3 change.

Native and WASM CTest suites pass, including legacy A goldens, a frozen ABI 1.2
consumer, legacy symbols, MIDI Clock and Tap Tempo. New tests build both legacy
and Classic C feedback configurations. A two-engine null test changes only the
monitor mode and trim in one engine, returns to Classic, and requires identical
samples and motion telemetry: history, feedback and motion remain unchanged.
With shared Spring/Smear, their monitoring state intentionally morphs; the null
claim applies to the tape/feedback state, not independently preserved aux tails.

Ten identical-input Studio Native/WASM listening renders also matched bit-exact
on the local toolchain. The script accepts <=1e-6 for cross-toolchain float drift,
and records exact equality separately. Studio dry has a numeric-identity contract,
not a general bit-exact contract (signed-zero behavior is explicitly measured).
