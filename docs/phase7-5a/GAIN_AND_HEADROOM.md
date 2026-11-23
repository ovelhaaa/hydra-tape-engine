# Gain and headroom

Studio's static endpoint is `out = guard(input + wetTrim * aux(cleanTapeWet))`.
Dry gain is 1; trim is linear 0..2 (0% mute, 100% unity, 200% +6.0206 dB).
The historical delayWet gain remains upstream of playback. There is no automatic
normalization, envelope-following gain, ducking of the direct instrument, or
new feedback gain. Existing tape dynamics remain on the wet.

The separate guard passes every finite sum within [-1,1] numerically unchanged,
clamps samples outside full scale, and converts a nonfinite sum to zero. It is an
emergency sample limiter, with **hard limiting under overload**, not a mastering
limiter. The original wet Spring limiter remains inside the wet processing.
Choose input level and Wet Level so nominal combined peaks leave headroom;
transparent dry cannot coexist with an unrestricted loud wet sum at full scale.
No fixed attenuation was imposed on dry to hide that tradeoff.

Dry-only values are numerically identical in the tested range. We do not claim
bit-exact Studio passthrough: floating-point addition can change signed zero,
and overload protection necessarily changes over-range samples. Settled trim=0
removes wet, hiss and auxiliary tails from monitoring while the engine continues.
A mode/trim transition is intentionally a temporary blend, not unity dry yet.

## Objective calibration

The generated metrics contain 132 source/level/rate cases, at 44.1 and 48 kHz:
100/1000/8000 Hz sines, pink noise, impulse, guitar transient, synthetic clean
and driven guitar, bass, electric piano and synth chords; peaks -24/-18/-12/
-6/-3/-0.01 dBFS. Each case measures Studio dry, Classic dry and Studio wet mix.
These are deterministic instrument approximations, not recorded performances.

Studio wet-muted maximum numerical error, RMS difference, gain error, introduced
DC and relative latency are zero in the calibration. Sine phase and spectral gain
are unchanged; measured THD floor is approximately 2.7e-8 (source float rounding).
No new combing or pitch change can occur on that identical dry signal. Measurements
of DC distinguish source mean from introduced DC. Full-scale samples, nonfinite
samples, sample peaks, gain, sine THD/phase and 4x true peaks are in metrics.json;
tail RMS and mono/stereo comparisons are in the listening scenarios.

The full wet calibration deliberately overloads: 25/132 cases hit the guard,
with no sample beyond full scale and no nonfinite samples. A measured 4x true
peak reached 1.2144: **sample protection does not guarantee intersample headroom**.
The ten moderate-level listening scenarios did not hit full scale (strong attack
peak .9700). Dry-only .9988 peaks remain intact. Overload is bounded but can sound
harsh; audition the strong-peak and sustained high-level cases before adoption.
No claim of universally distortion-free mixing or true-peak limiting is made.

Linear versus equal-power supplemental WAVs use the identical dry/wet stem,
respectively .5/.5 and sqrt(.5)/sqrt(.5). This exposes their energy difference;
files are not individually normalized. Neither is imposed on the additive Studio
contract. Compare at comfortable volume and account for the ~3 dB difference.
