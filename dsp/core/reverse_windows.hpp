#pragma once

#include <algorithm>
#include <cstdint>

namespace hydra::dsp {

// Delay relative to the write head grows by two frames per output frame.
// One frame compensates for the moving write head and one plays backwards.
class ReverseWindows {
 public:
  struct Frame {
    float delay;
    float previousDelay;
    float incoming;
  };

  void configure(float sampleRate, int32_t historyFrames, float bpm,
                 int subdivision, bool free, float timeMs) {
    constexpr double beats[] = {1., 4., 2., .5, .25, 1.5, .75, 2./3., 1./3.};
    const double requested = free ? timeMs * 0.001 * sampleRate
        : 60. * sampleRate * beats[std::clamp(subdivision, 0, 8)] / std::max(1.f, bpm);
    overlap_ = std::clamp<int32_t>(static_cast<int32_t>(0.008f * sampleRate),
                          1, std::max<int32_t>(1, historyFrames / 8));
    invOverlap_ = 1.f / overlap_;
    modulation_ = std::min<int32_t>(24, std::max<int32_t>(0, historyFrames / 16));
    anchor_ = 4 + modulation_;
    maxHop_ = std::max<int32_t>(1, (historyFrames - 8 - 2 * modulation_) / 2 - overlap_);
    isClamped_ = (requested > static_cast<double>(maxHop_));
    const double hop = std::clamp(requested, static_cast<double>(std::min(overlap_ + 1, maxHop_)),
                                  static_cast<double>(maxHop_));
    effectiveMs_ = static_cast<float>((hop / sampleRate) * 1000.0);
    hopFrames_ = static_cast<int32_t>(hop);
    fraction_ = static_cast<uint32_t>((hop - hopFrames_) * 4294967296.);
  }

  bool isClamped() const { return isClamped_; }
  float effectiveMs() const { return effectiveMs_; }

  void reset() {
    age_ = hop_ = previousAge_ = 0;
    overlapAge_ = overlap_;
    remainder_ = 0x80000000u;
  }

  // Initialize playback window aligned to a global transport phase [0.0, 1.0)
  void syncPhase(float phase, int32_t availableHistoryFrames = 0x7fffffff) {
    const float clampedPhase = std::clamp(phase, 0.0f, 0.999999f);
    const int32_t targetAge = static_cast<int32_t>(clampedPhase * static_cast<float>(hopFrames_));
    const int32_t safeMax = (availableHistoryFrames > anchor_ + overlap_)
                                ? std::min(maxHop_, (availableHistoryFrames - anchor_ - overlap_) / 2)
                                : maxHop_;
    age_ = std::clamp(targetAge, 0, std::max(0, safeMax));
    hop_ = hopFrames_;
    overlapAge_ = overlap_; // Already in steady state for current window
  }

  float modulationLimit() const { return modulation_; }
  int32_t hopFrames() const { return hopFrames_; }
  int32_t maxHop() const { return maxHop_; }

  Frame next(bool isBoundary = false, bool syncToClock = false) {
    if (syncToClock) {
      if (isBoundary || (hop_ > 0 && age_ >= maxHop_)) {
        if (hop_ > 0 && age_ > 0) {
          previousAge_ = age_;
          overlapAge_ = 0;
        }
        hop_ = hopFrames_;
        age_ = 0;
      }
    } else {
      if (age_ == hop_) {
        if (hop_ > 0) {
          previousAge_ = age_;
          overlapAge_ = 0;
        }
        const uint32_t before = remainder_;
        remainder_ += fraction_;
        hop_ = hopFrames_ + (remainder_ < before ? 1 : 0);
        age_ = 0;
      }
    }

    Frame frame{static_cast<float>(anchor_ + 2 * age_++), 0.f, 1.f};
    if (overlapAge_ < overlap_) {
      const float x = overlapAge_++ * invOverlap_;
      frame.incoming = x * x * (3.f - 2.f * x);
      frame.previousDelay = static_cast<float>(anchor_ + 2 * previousAge_++);
    }
    return frame;
  }

 private:
  float effectiveMs_ = 0.0f;
  bool isClamped_ = false;
  int32_t age_ = 0;
  int32_t hop_ = 0;
  int32_t previousAge_ = 0;
  int32_t overlapAge_ = 0;
  int32_t overlap_ = 1;
  int32_t anchor_ = 4;
  int32_t modulation_ = 0;
  int32_t maxHop_ = 1;
  int32_t hopFrames_ = 2;
  float invOverlap_ = 1.f;
  uint32_t fraction_ = 0;
  uint32_t remainder_ = 0x80000000u;
};

}  // namespace hydra::dsp
