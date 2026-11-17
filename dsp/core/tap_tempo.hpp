#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace hydra::dsp {

enum class TapTempoStatus : uint8_t {
  Idle = 0,         // Waiting for taps (or timed out)
  Acquiring = 1,    // 1st or 2nd tap, gathering intervals
  Locked = 2,       // 3+ consistent taps, tempo locked/refined
  Rejected = 3      // Last tap was rejected
};

enum class TapRejectionReason : uint8_t {
  None = 0,
  Bounce = 1,        // < 80 ms
  OutOfRange = 2,    // < 30 BPM or > 300 BPM
  OutOfOrder = 3     // timestamp <= lastValidTimestamp
};

struct TapResult {
  bool accepted = false;
  bool tempoUpdated = false;
  double newBpm = 0.0;
  TapTempoStatus status = TapTempoStatus::Idle;
  TapRejectionReason rejection = TapRejectionReason::None;

  explicit operator bool() const noexcept { return accepted; }
};

class TapTempo {
 public:
  static constexpr double kMinBpm = 30.0;
  static constexpr double kMaxBpm = 300.0;
  static constexpr double kDefaultBpm = 120.0;
  static constexpr double kDebounceSeconds = 0.080;  // 80 ms debounce window
  static constexpr double kTimeoutSeconds = 3.000;   // 3.0 s session timeout
  static constexpr size_t kMaxHistory = 5;           // Circular history up to 5 intervals
  static constexpr double kOutlierThreshold = 0.20;  // 20% relative deviation threshold

  TapTempo() {
    reset();
  }

  void prepare(double sampleRate) {
    if (std::isfinite(sampleRate) && sampleRate > 0.0) {
      sampleRate_ = sampleRate;
    }
  }

  double sampleRate() const { return sampleRate_; }

  void reset() {
    resetSequence();
    tapCount_ = 0;
    lastValidTimestampFrames_ = 0;
    lastObservedTimestampFrames_ = 0;
    isTimedOut_ = true;
    status_ = TapTempoStatus::Idle;
    lastRejection_ = TapRejectionReason::None;
    confidence_ = 0.0f;
    // appliedBpm_ and estimatedBpm_ are preserved across reset
  }

  void resetSequence() {
    for (size_t i = 0; i < kMaxHistory; ++i) {
      intervals_[i] = 0.0;
    }
    historyCount_ = 0;
    historyIndex_ = 0;
    consecutiveOutliers_ = 0;
    pendingNewTempoInterval_ = 0.0;
  }

  // Record a tap event at the given timestamp in sample frames.
  // Returns TapResult distinguishing whether tap was accepted, whether tempo was updated,
  // the new BPM, status, and any rejection reason.
  TapResult recordTap(uint64_t timestampFrames) {
    lastObservedTimestampFrames_ = timestampFrames;

    if (tapCount_ > 0) {
      // Check for out-of-order timestamp
      if (timestampFrames <= lastValidTimestampFrames_) {
        status_ = TapTempoStatus::Rejected;
        lastRejection_ = TapRejectionReason::OutOfOrder;
        return TapResult{false, false, appliedBpm_, status_, lastRejection_};
      }

      const uint64_t deltaFrames = timestampFrames - lastValidTimestampFrames_;
      const double intervalSeconds = static_cast<double>(deltaFrames) / sampleRate_;

      // Check for timeout (> 3.0s) -> ends previous session and starts new sequence.
      // Note: Tap 1 of a new sequence establishes the time base only; tempo is NOT updated.
      if (intervalSeconds > kTimeoutSeconds) {
        resetSequence();
        tapCount_ = 1;
        lastValidTimestampFrames_ = timestampFrames;
        isTimedOut_ = false;
        status_ = TapTempoStatus::Acquiring;
        lastRejection_ = TapRejectionReason::None;
        confidence_ = 0.0f;
        return TapResult{true, false, appliedBpm_, status_, lastRejection_};
      }

      // Check for debounce (< 80ms)
      if (intervalSeconds < kDebounceSeconds) {
        status_ = TapTempoStatus::Rejected;
        lastRejection_ = TapRejectionReason::Bounce;
        // Do not update lastValidTimestampFrames_ on bounce
        return TapResult{false, false, appliedBpm_, status_, lastRejection_};
      }

      // Check for nominal tempo range (30 to 300 BPM)
      const double rawBpm = 60.0 / intervalSeconds;
      if (rawBpm < kMinBpm || rawBpm > kMaxBpm) {
        status_ = TapTempoStatus::Rejected;
        lastRejection_ = TapRejectionReason::OutOfRange;
        // Update lastValidTimestampFrames_ so next tap can measure from here
        lastValidTimestampFrames_ = timestampFrames;
        return TapResult{false, false, appliedBpm_, status_, lastRejection_};
      }

      // Valid interval within range
      lastValidTimestampFrames_ = timestampFrames;
      lastRejection_ = TapRejectionReason::None;
      isTimedOut_ = false;
      tapCount_++;

      if (historyCount_ == 0) {
        // Tap 2: First valid interval. Responsive immediate estimation.
        intervals_[0] = intervalSeconds;
        historyCount_ = 1;
        historyIndex_ = 1;
        estimatedBpm_ = 60.0 / intervalSeconds;
        appliedBpm_ = std::clamp(estimatedBpm_, kMinBpm, kMaxBpm);
        confidence_ = 0.40f; // Initial responsive confidence
        status_ = TapTempoStatus::Acquiring;
        consecutiveOutliers_ = 0;
        return TapResult{true, true, appliedBpm_, status_, lastRejection_};
      }

      if (historyCount_ == 1) {
        // Tap 3: Second valid interval. Verify consistency.
        const double prevInterval = intervals_[0];
        const double relDiff = std::abs(intervalSeconds - prevInterval) / prevInterval;

        if (relDiff <= kOutlierThreshold) {
          intervals_[1] = intervalSeconds;
          historyCount_ = 2;
          historyIndex_ = 2;
          const double avgInterval = 0.5 * (prevInterval + intervalSeconds);
          estimatedBpm_ = 60.0 / avgInterval;
          appliedBpm_ = std::clamp(estimatedBpm_, kMinBpm, kMaxBpm);
          confidence_ = 0.75f;
          status_ = TapTempoStatus::Locked;
          consecutiveOutliers_ = 0;
          return TapResult{true, true, appliedBpm_, status_, lastRejection_};
        } else {
          // If second interval deviates significantly from first, adapt to the new cadence
          intervals_[0] = intervalSeconds;
          historyCount_ = 1;
          historyIndex_ = 1;
          estimatedBpm_ = 60.0 / intervalSeconds;
          appliedBpm_ = std::clamp(estimatedBpm_, kMinBpm, kMaxBpm);
          confidence_ = 0.40f;
          status_ = TapTempoStatus::Acquiring;
          consecutiveOutliers_ = 0;
          return TapResult{true, true, appliedBpm_, status_, lastRejection_};
        }
      }

      // Tap 4+: historyCount_ >= 2
      const double curMedian = computeMedian();
      const double dev = std::abs(intervalSeconds - curMedian) / curMedian;

      if (dev <= kOutlierThreshold) {
        // Inlier: add to circular history
        addInterval(intervalSeconds);
        consecutiveOutliers_ = 0;
        const double newMedian = computeMedian();
        estimatedBpm_ = 60.0 / newMedian;
        appliedBpm_ = std::clamp(estimatedBpm_, kMinBpm, kMaxBpm);
        confidence_ = (historyCount_ >= 4) ? 1.0f : 0.85f;
        status_ = TapTempoStatus::Locked;
        return TapResult{true, true, appliedBpm_, status_, lastRejection_};
      } else {
        // Outlier detected
        consecutiveOutliers_++;
        if (consecutiveOutliers_ == 1) {
          // Isolated outlier: preserve established BPM, wait for next tap. Tempo NOT updated.
          pendingNewTempoInterval_ = intervalSeconds;
          status_ = TapTempoStatus::Locked;
          return TapResult{true, false, appliedBpm_, status_, lastRejection_};
        } else if (consecutiveOutliers_ >= 2) {
          // Two consecutive intervals indicating a new tempo!
          const double diffBetweenOutliers =
              std::abs(intervalSeconds - pendingNewTempoInterval_) / pendingNewTempoInterval_;
          if (diffBetweenOutliers <= kOutlierThreshold) {
            // Intentional tempo change confirmed! Re-anchor history to new cadence.
            intervals_[0] = pendingNewTempoInterval_;
            intervals_[1] = intervalSeconds;
            historyCount_ = 2;
            historyIndex_ = 2;
            consecutiveOutliers_ = 0;
            const double avgInterval = 0.5 * (pendingNewTempoInterval_ + intervalSeconds);
            estimatedBpm_ = 60.0 / avgInterval;
            appliedBpm_ = std::clamp(estimatedBpm_, kMinBpm, kMaxBpm);
            confidence_ = 0.80f;
            status_ = TapTempoStatus::Locked;
            return TapResult{true, true, appliedBpm_, status_, lastRejection_};
          } else {
            pendingNewTempoInterval_ = intervalSeconds;
            consecutiveOutliers_ = 1;
            return TapResult{true, false, appliedBpm_, status_, lastRejection_};
          }
        }
      }
      return TapResult{true, false, appliedBpm_, status_, lastRejection_};
    }

    // Tap 1: Start a new sequence. Establish time reference only; tempo is NOT updated.
    tapCount_ = 1;
    lastValidTimestampFrames_ = timestampFrames;
    isTimedOut_ = false;
    status_ = TapTempoStatus::Acquiring;
    lastRejection_ = TapRejectionReason::None;
    confidence_ = 0.0f;
    consecutiveOutliers_ = 0;
    return TapResult{true, false, appliedBpm_, status_, lastRejection_};
  }

  // Periodic update called from audio loop or timer with current engine sample position.
  // Checks for timeout condition (> 3.0s without tap).
  void update(uint64_t currentTimestampFrames) {
    lastObservedTimestampFrames_ = currentTimestampFrames;
    if (tapCount_ > 0 && !isTimedOut_) {
      if (currentTimestampFrames >= lastValidTimestampFrames_) {
        const uint64_t elapsedFrames = currentTimestampFrames - lastValidTimestampFrames_;
        const double elapsedSeconds = static_cast<double>(elapsedFrames) / sampleRate_;
        if (elapsedSeconds > kTimeoutSeconds) {
          isTimedOut_ = true;
          status_ = TapTempoStatus::Idle;
          confidence_ = 0.0f;
        }
      }
    }
  }

  // Explicitly sync the applied BPM from external source (e.g. transport manual change)
  void setAppliedBpm(double bpm) {
    if (std::isfinite(bpm) && bpm >= kMinBpm && bpm <= kMaxBpm) {
      appliedBpm_ = bpm;
      estimatedBpm_ = bpm;
    }
  }

  double estimatedBpm() const { return estimatedBpm_; }
  double appliedBpm() const { return appliedBpm_; }
  uint32_t tapCount() const { return tapCount_; }
  float confidence() const { return confidence_; }
  bool isTimedOut() const { return isTimedOut_; }
  TapTempoStatus status() const { return status_; }
  TapRejectionReason lastRejection() const { return lastRejection_; }
  uint64_t lastValidTimestampFrames() const { return lastValidTimestampFrames_; }

  float timeSinceLastTapSeconds(uint64_t currentTimestampFrames) const {
    if (tapCount_ == 0 || lastValidTimestampFrames_ == 0) {
      return -1.0f;
    }
    if (currentTimestampFrames < lastValidTimestampFrames_) {
      return 0.0f;
    }
    const uint64_t delta = currentTimestampFrames - lastValidTimestampFrames_;
    return static_cast<float>(static_cast<double>(delta) / sampleRate_);
  }

 private:
  void addInterval(double interval) {
    if (historyCount_ < kMaxHistory) {
      intervals_[historyCount_++] = interval;
      historyIndex_ = historyCount_ % kMaxHistory;
    } else {
      intervals_[historyIndex_] = interval;
      historyIndex_ = (historyIndex_ + 1) % kMaxHistory;
    }
  }

  double computeMedian() const {
    if (historyCount_ == 0) return 0.5; // default 120 BPM
    if (historyCount_ == 1) return intervals_[0];
    if (historyCount_ == 2) return 0.5 * (intervals_[0] + intervals_[1]);

    double temp[kMaxHistory];
    for (size_t i = 0; i < historyCount_; ++i) {
      temp[i] = intervals_[i];
    }

    // Small stack sort (N <= 5)
    for (size_t i = 1; i < historyCount_; ++i) {
      double key = temp[i];
      size_t j = i;
      while (j > 0 && temp[j - 1] > key) {
        temp[j] = temp[j - 1];
        --j;
      }
      temp[j] = key;
    }

    if (historyCount_ % 2 == 1) {
      return temp[historyCount_ / 2];
    } else {
      const size_t mid = historyCount_ / 2;
      return 0.5 * (temp[mid - 1] + temp[mid]);
    }
  }

  double sampleRate_ = 44100.0;
  uint64_t lastValidTimestampFrames_ = 0;
  uint64_t lastObservedTimestampFrames_ = 0;
  uint32_t tapCount_ = 0;
  double intervals_[kMaxHistory] = {0.0};
  size_t historyCount_ = 0;
  size_t historyIndex_ = 0;
  uint32_t consecutiveOutliers_ = 0;
  double pendingNewTempoInterval_ = 0.0;

  double estimatedBpm_ = kDefaultBpm;
  double appliedBpm_ = kDefaultBpm;
  float confidence_ = 0.0f;
  bool isTimedOut_ = true;
  TapTempoStatus status_ = TapTempoStatus::Idle;
  TapRejectionReason lastRejection_ = TapRejectionReason::None;
};

}  // namespace hydra::dsp
