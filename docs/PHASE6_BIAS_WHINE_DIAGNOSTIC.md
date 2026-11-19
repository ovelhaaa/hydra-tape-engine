# High-pitch whine — isolated bias diagnostic

Reference commit: `11c1c5d02b65f5a716772c7a2c825dfb4699b842`.

Four separately compiled native libraries from source copies. The official DSP source and release WAVs were not modified. Control musical WAV is byte-identical to the official local scenario 01 WAV.

## Observed 6 kHz energy (musical input, seconds 1–4)

| Variant | Left dBFS | Right dBFS |
|---|---:|---:|
| baseline | -49.03 | -49.18 |
| bias_off | -106.36 | -102.73 |
| bias_sine_18k | -104.16 | -102.13 |
| bias_triangle_17k | -106.12 | -102.55 |

Metric: RMS energy within 5990–6010 Hz; Welch PSD with 48000-sample segments. These values use a different segment/window from the earlier quick diagnostic and are not directly interchangeable with it.

## Conclusions

- The bias contribution is causally responsible for the strong 6 kHz line in scenario 01: removing only this term reduces its band energy by 53.6–57.3 dB across the two channels.
- Substituting a sinusoid at the same 18 kHz frequency reduces the 6 kHz line by roughly 53–55 dB, while an 18 kHz carrier remains. This supports harmonics/aliasing from the triangular bias as the dominant 6 kHz mechanism; nonlinear residuals are not entirely removed.
- Moving the triangular bias to 17 kHz removes the dominant 6 kHz line and produces lines at 17, 11, 5, 7 and 3 kHz in the 440 Hz control. The 3 kHz line is weak, not a dominant one-to-one shifted whistle. The changed spectral pattern is consistent with folded triangular harmonics, but this experiment alone does not quantify each downstream filter/nonlinear contribution.
- The sine440 control reproduces the issue without musical-source high-frequency content. Silence with the configured hiss also reproduces it; removing bias suppresses the carrier. Hence neither WAV export nor the web player is needed to produce the issue.
- A sine replacement is a diagnostic ablation, not a validated production fix: 18 kHz leakage remains and magnetic/tape behavior must be preserved and revalidated.

## Reproduce

On the recorded Windows/MinGW environment, first build `hydra_abi_test_library` in `build-native` and render the Phase 6 package as described in [the Listening Lab README](../tools/phase6-listening-lab/README.md). Then run from repository root:

```text
python scripts/diagnose_bias_whine.py
```

Output directory: `artifacts/bias-isolation/`. Each variant directory contains musical.wav, sine440.wav and silence.wav. results.json records exact source/library/WAV SHA-256, compiler command, environment and per-channel metrics. All files in this directory are diagnostic artifacts; none replace the published listening package.

## Human listening evidence and release consequence

On 2026-11-19 the reviewer reported no other perceived problem in the listening performed, except the high-pitch whine, also noticed in earlier rounds. The supplied `hydra-reviews-11c1c5d.json` references commit `11c1c5d02b65f5a716772c7a2c825dfb4699b842`: scenarios 01–09, 11 and 12 are NEEDS ATTENTION with “High pitch whine”; scenario 10 contains that comment but remains NOT REVIEWED; scenarios 13–20 remain NOT REVIEWED without comments. No unreviewed scenario is inferred to be approved.

This establishes a reproducible audible defect requiring investigation/correction before musical signoff. The bias ablations are diagnostic builds only; neither removing bias nor substituting a sine has been approved as the production solution. A correction must preserve intended tape character, address residual 18 kHz leakage, and undergo native/WASM/firmware regression and new human A/B listening. Issue #48 remains a separate numerical-equivalence gate; no thresholds or goldens were changed.

Machine-readable measurements and source/library/WAV hashes: [phase6-bias-whine.json](evidence/phase6-bias-whine.json). Build-command machine-specific roots are replaced by `{repo}` and the compiler executable name; measured values and hashes are unchanged. No diagnostic DLLs or WAVs are committed.
