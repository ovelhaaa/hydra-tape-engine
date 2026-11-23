# Decision record

HUMAN_LISTENING_PENDING

Classic remains the default. No merge, promotion, PR #58/#59/#60 modification,
Repeat Aging integration or ESP32 flash is authorized by this experiment.

Chosen: append Signal Path Mode and output Wet Level; preserve historical mix
controls and feedback; associate Vintage Texture with Classic and Clean Dry with
Studio; use one tape engine, private small playback filters and shared auxiliary
memory; morph over 30 ms; protect the final Studio sum separately from its dry.

Rejected for this stage: equal-power default without audition, content-driven
normalization, a second TapeCore, changing Motion algorithms, silently redefining
dryWet, and claiming bit-exact transparent dry through an arithmetic sum/guard.

Integration prerequisite resolved separately on #61: follow-up c1f996b fixes
reset smoothing initialization. On 2026-11-23 its checks all completed successfully
(Web Demo, Unified Motion, Phase 6 Listening Lab and Native/WASM/ESP32 gates;
Pages deploy skipped as expected on draft PR). The experimental branch starts
at that exact stabilized HEAD. PR #61 is still open/draft and remains a dependency.

Local evidence: 22 Native + 22 WASM CTest tests passed; frozen M0 and M1/M2/M3
parity passed; 132 dry calibrations had zero numeric error; 32 additional Classic
scenes matched the stabilized reference; ten Studio Native/WASM renders matched;
real UI/AudioWorklet browser tests and experimental ESP32 build passed. Tests are
reproducible in the dedicated artifact workflow. Remote experiment checks and
artifact links belong in the PR rather than being guessed in this document.

Known limitations needing human judgment:
- Synthesized sources cannot replace actual guitar/bass/piano recordings.
- Shared auxiliary state morph is not a mathematical full-output crossfade.
- Full-scale guard is hard sample limiting under overload; 4x intersample peaks
  can exceed full scale. It is not a true-peak limiter or loudness normalizer.
- Physical ESP32 incremental cycles / sustained deadlines have not been measured.

Next: blind audition with real guitar, focusing on wet levels and high transients;
review shared Spring/Smear tails and mono behavior; then a separately authorized
physical benchmark before any promotion. Studio need not win every comparison.
