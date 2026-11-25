#pragma once

// F5 musical-timing experiment policies.
//
// The Global Transport Clock is never smoothed: it keeps the exact musical
// tempo. Only the delay-line read position used by the conventional heads is
// allowed to glide. This header holds constants only; the per-sample update
// lives in TapeCore::Impl and is compiled out when the variant is 0.
//
//   variant 0: Vintage F3-A behaviour (bit-exact control)
//   variant 1: Musical Capstan Glide
//   variant 2: Clock-Focused Timing
//
// Units: `maxPitch` is |d(delay)/dt| in samples per sample. For the third
// (longest) head a value of 0.12 is a transient playback-speed deviation of
// about 12 %, i.e. roughly 192 cents. `deadband`/`hold` are in samples and
// scale with the sample rate when converted to the musical domain.

#ifndef HYDRA_DSP_REFORM_F5_TIMING_VARIANT
#define HYDRA_DSP_REFORM_F5_TIMING_VARIANT 0
#endif

#if HYDRA_DSP_REFORM_F5_TIMING_VARIANT < 0 || HYDRA_DSP_REFORM_F5_TIMING_VARIANT > 2
#error "HYDRA_DSP_REFORM_F5_TIMING_VARIANT must be 0, 1 or 2"
#endif

namespace hydra::dsp::experimental::f5 {

struct TimingPolicy {
  float tauSeconds;         // one-pole time constant of the glide
  float maxPitch;           // samples/sample cap on the read-position speed
  float deadbandSamples;    // snap when the remaining error is this small
  float holdSamples;        // do not move while the error stays this small
};

// Variant 1 and the Internal/manual part of variant 2. Reuses the approved
// ~200 ms smoothing time constant and adds only a speed cap.
inline constexpr TimingPolicy kCapstan = {0.200f, 0.120f, 0.050f, 0.0f};

// Variant 2, Tap/MIDI source, small residual error (<= kSmallErrorBeats).
// Fast settling with a sub-semitone transient deviation.
inline constexpr TimingPolicy kClockFast = {0.030f, 0.050f, 0.0f, 0.050f};

// Variant 2, Tap/MIDI source, large musical displacement. Gentler than the
// capstan policy, because a large single-reader move is audible by nature.
inline constexpr TimingPolicy kClockLarge = {0.150f, 0.085f, 0.0f, 0.050f};

// Event-size classification for the clock-focused policy, in beats:
//   <= 1 %  : capstan smoothing (jitter / micro phase corrections)
//   <= 5 %  : fast clock policy
//   >  5 %  : large clock policy (gentle, single-reader limit)
inline constexpr float kMicroStepBeats = 0.01f;
inline constexpr float kSmallStepBeats = 0.05f;

// Clock-focused target hysteresis: ignore applied-BPM moves smaller than this
// so the read head does not chase per-pulse PLL breathing.
inline constexpr float kClockTargetDeadbandMs = 2.0f;

inline constexpr int kVariant = HYDRA_DSP_REFORM_F5_TIMING_VARIANT;

}  // namespace hydra::dsp::experimental::f5
