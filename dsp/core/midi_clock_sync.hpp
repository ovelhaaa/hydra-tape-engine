#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace hydra::dsp {

enum class MidiClockSyncStatus : uint8_t {
  Disconnected = 0,
  Acquiring = 1,
  Locked = 2,
  Holdover = 3,
  Lost = 4
};

enum class TempoSource : uint8_t {
  Internal = 0,
  Tap = 1,
  MidiExternal = 2
};

struct MidiClockEventResult {
  bool accepted = false;
  bool tempoUpdated = false;
  double estimatedBpm = 0.0;
  double appliedBpm = 0.0;
  double phaseErrorBeats = 0.0;
  MidiClockSyncStatus status = MidiClockSyncStatus::Disconnected;
  uint8_t transportAction = 0; // 0=None, 1=Start, 2=Continue, 3=Stop
};

class MidiClockSync {
 public:
  static constexpr double kMinBpm = 30.0;
  static constexpr double kMaxBpm = 300.0;
  static constexpr double kDefaultBpm = 120.0;
  static constexpr size_t kHistorySize = 24; // 24 pulses = 1 quarter-note beat
  static constexpr double kMaxPhaseCorrectionPercent = 0.03; // Max 3% BPM adjustment for phase tracking
  static constexpr double kMaxPhaseCorrectionBpm = 3.0;      // Max 3.0 BPM absolute correction
  static constexpr double kPhaseGain = 15.0;                 // Proportional phase correction gain

  MidiClockSync() {
    reset();
  }

  void prepare(double sampleRate) {
    if (std::isfinite(sampleRate) && sampleRate > 0.0) {
      sampleRate_ = sampleRate;
      updateIntervalBounds();
    }
  }

  double sampleRate() const { return sampleRate_; }

  void reset() {
    status_ = MidiClockSyncStatus::Disconnected;
    estimatedBpm_ = kDefaultBpm;
    appliedBpm_ = kDefaultBpm;
    phaseErrorBeats_ = 0.0;
    estimatedJitterMs_ = 0.0;
    pulseCount_ = 0;
    lastPulseFrame_ = 0;
    consecutiveLockedPulses_ = 0;
    historyCount_ = 0;
    historyIndex_ = 0;
    transportRunning_ = false;
    missedPulses_ = 0;
    for (size_t i = 0; i < kHistorySize; ++i) {
      intervals_[i] = 0.0;
    }
  }

  // Soft reset of acquisition without losing history/BPM configuration
  void resetAcquisition() {
    status_ = MidiClockSyncStatus::Acquiring;
    consecutiveLockedPulses_ = 0;
    historyCount_ = 0;
    historyIndex_ = 0;
    phaseErrorBeats_ = 0.0;
    for (size_t i = 0; i < kHistorySize; ++i) {
      intervals_[i] = 0.0;
    }
  }

  // Process a real-time MIDI event byte (0xF8, 0xFA, 0xFB, 0xFC)
  // currentGtcMusicalPos: current musical position in quarter-note beats from GTC
  MidiClockEventResult processEvent(uint8_t statusByte,
                                    uint64_t timestampFrames,
                                    double currentGtcMusicalPos) {
    MidiClockEventResult res{};
    res.estimatedBpm = estimatedBpm_;
    res.appliedBpm = appliedBpm_;
    res.status = status_;

    switch (statusByte) {
      case 0xFA: { // Start
        transportRunning_ = true;
        pulseCount_ = 0;
        consecutiveLockedPulses_ = 0;
        res.accepted = true;
        res.transportAction = 1; // Start
        if (status_ == MidiClockSyncStatus::Disconnected || status_ == MidiClockSyncStatus::Lost) {
          status_ = MidiClockSyncStatus::Acquiring;
        }
        res.status = status_;
        return res;
      }
      case 0xFB: { // Continue
        transportRunning_ = true;
        // Align external pulse count with preserved GTC position
        if (currentGtcMusicalPos >= 0.0) {
          pulseCount_ = static_cast<uint64_t>(std::max(0.0, std::round(currentGtcMusicalPos * 24.0)));
        }
        res.accepted = true;
        res.transportAction = 2; // Continue
        res.status = status_;
        return res;
      }
      case 0xFC: { // Stop
        transportRunning_ = false;
        res.accepted = true;
        res.transportAction = 3; // Stop
        res.status = status_;
        return res;
      }
      case 0xF8: { // Timing Clock (24 PPQN)
        res.accepted = true;
        return processTimingClock(timestampFrames, currentGtcMusicalPos);
      }
      default:
        // Ignore non-realtime or unsupported system bytes
        res.accepted = false;
        return res;
    }
  }

  // Periodic update called from audio process block to evaluate timeouts
  void update(uint64_t currentFrame) {
    if (status_ == MidiClockSyncStatus::Disconnected) {
      return;
    }

    if (currentFrame <= lastPulseFrame_) {
      return;
    }

    const uint64_t elapsedFrames = currentFrame - lastPulseFrame_;
    const double expectedPulseFrames = (historyCount_ > 0 && smoothedIntervalFrames_ > 0.0)
                                           ? smoothedIntervalFrames_
                                           : ((60.0 * sampleRate_) / (24.0 * appliedBpm_));

    // Adaptive timeout thresholds based on expected interval
    const uint64_t timeoutFrames = static_cast<uint64_t>(
        std::max(sampleRate_ * 0.150, 4.0 * expectedPulseFrames));
    const uint64_t lostFrames = static_cast<uint64_t>(
        std::max(sampleRate_ * 1.000, 12.0 * expectedPulseFrames));

    if (elapsedFrames > lostFrames) {
      if (status_ != MidiClockSyncStatus::Lost) {
        status_ = MidiClockSyncStatus::Lost;
        consecutiveLockedPulses_ = 0;
      }
    } else if (elapsedFrames > timeoutFrames) {
      if (status_ == MidiClockSyncStatus::Locked || status_ == MidiClockSyncStatus::Acquiring) {
        status_ = MidiClockSyncStatus::Holdover;
        consecutiveLockedPulses_ = 0;
      }
    }
  }

  // Getters
  MidiClockSyncStatus status() const { return status_; }
  double estimatedBpm() const { return estimatedBpm_; }
  double appliedBpm() const { return appliedBpm_; }
  double phaseErrorBeats() const { return phaseErrorBeats_; }
  double estimatedJitterMs() const { return estimatedJitterMs_; }
  uint64_t pulseCount() const { return pulseCount_; }
  uint64_t lastPulseFrame() const { return lastPulseFrame_; }
  bool isTransportRunning() const { return transportRunning_; }
  uint32_t missedPulses() const { return missedPulses_; }

  void setAppliedBpm(double bpm) {
    if (std::isfinite(bpm) && bpm >= kMinBpm && bpm <= kMaxBpm) {
      appliedBpm_ = bpm;
      estimatedBpm_ = bpm;
    }
  }

 private:
  void updateIntervalBounds() {
    // 30 BPM: delta = 60 * fs / (24 * 30) = 2.5 * fs / 30
    // 300 BPM: delta = 2.5 * fs / 300
    // Allow slight tolerance margin: 25 BPM to 320 BPM
    minValidIntervalFrames_ = (2.5 * sampleRate_) / 320.0;
    maxValidIntervalFrames_ = (2.5 * sampleRate_) / 25.0;
  }

  MidiClockEventResult processTimingClock(uint64_t timestampFrames, double currentGtcMusicalPos) {
    MidiClockEventResult res{};
    res.accepted = true;

    if (transportRunning_) {
      pulseCount_++;
    }

    if (lastPulseFrame_ == 0 || status_ == MidiClockSyncStatus::Disconnected || status_ == MidiClockSyncStatus::Lost) {
      // First pulse or re-connection after lost
      lastPulseFrame_ = timestampFrames;
      status_ = MidiClockSyncStatus::Acquiring;
      res.status = status_;
      res.estimatedBpm = estimatedBpm_;
      res.appliedBpm = appliedBpm_;
      return res;
    }

    if (timestampFrames <= lastPulseFrame_) {
      // Out of order or zero delta - reject interval update
      res.status = status_;
      res.estimatedBpm = estimatedBpm_;
      res.appliedBpm = appliedBpm_;
      return res;
    }

    const uint64_t deltaFrames = timestampFrames - lastPulseFrame_;
    lastPulseFrame_ = timestampFrames;

    // Check for missing pulse (e.g., dropped MIDI packet where delta is ~2x expected interval)
    double effectiveDelta = static_cast<double>(deltaFrames);
    if (historyCount_ >= 4 && smoothedIntervalFrames_ > 0.0) {
      const double ratio = effectiveDelta / smoothedIntervalFrames_;
      if (ratio >= 1.7 && ratio <= 2.3) {
        // Detected 1 missing pulse
        missedPulses_++;
        if (transportRunning_) pulseCount_++;
        effectiveDelta /= 2.0;
      } else if (ratio >= 2.7 && ratio <= 3.3) {
        // Detected 2 missing pulses
        missedPulses_ += 2;
        if (transportRunning_) pulseCount_ += 2;
        effectiveDelta /= 3.0;
      }
    }

    // Validate interval bounds
    if (effectiveDelta < minValidIntervalFrames_ || effectiveDelta > maxValidIntervalFrames_) {
      // Out of range interval
      res.status = status_;
      res.estimatedBpm = estimatedBpm_;
      res.appliedBpm = appliedBpm_;
      return res;
    }

    // Record interval in circular buffer
    intervals_[historyIndex_] = effectiveDelta;
    historyIndex_ = (historyIndex_ + 1) % kHistorySize;
    if (historyCount_ < kHistorySize) {
      historyCount_++;
    }

    // Compute moving window average and jitter
    double sum = 0.0;
    for (size_t i = 0; i < historyCount_; ++i) {
      sum += intervals_[i];
    }
    const double meanInterval = sum / static_cast<double>(historyCount_);
    smoothedIntervalFrames_ = meanInterval;

    // Jitter calculation (standard deviation in milliseconds)
    double varianceSum = 0.0;
    for (size_t i = 0; i < historyCount_; ++i) {
      const double diff = intervals_[i] - meanInterval;
      varianceSum += diff * diff;
    }
    const double stdDevFrames = std::sqrt(varianceSum / static_cast<double>(historyCount_));
    estimatedJitterMs_ = (stdDevFrames / sampleRate_) * 1000.0;

    // Instantaneous and smoothed BPM
    const double instBpm = (60.0 * sampleRate_) / (24.0 * effectiveDelta);
    estimatedBpm_ = (60.0 * sampleRate_) / (24.0 * meanInterval);
    estimatedBpm_ = std::clamp(estimatedBpm_, kMinBpm, kMaxBpm);

    // Phase error tracking
    if (transportRunning_ && pulseCount_ > 0) {
      // Expected external position: 1/24 beat per pulse
      const double expectedMusicalPos = static_cast<double>(pulseCount_) / 24.0;
      double phaseDiff = expectedMusicalPos - currentGtcMusicalPos;
      // Wrap phase difference to closest beat [-0.5, +0.5]
      phaseDiff -= std::round(phaseDiff);
      phaseErrorBeats_ = phaseDiff;
    } else {
      phaseErrorBeats_ = 0.0;
    }

    // Digital PLL: Apply proportional correction to pull phase error to zero
    double phaseCorrectionBpm = 0.0;
    if (transportRunning_ && historyCount_ >= 8) {
      phaseCorrectionBpm = phaseErrorBeats_ * kPhaseGain;
      const double maxCorrection = std::min(kMaxPhaseCorrectionBpm, estimatedBpm_ * kMaxPhaseCorrectionPercent);
      phaseCorrectionBpm = std::clamp(phaseCorrectionBpm, -maxCorrection, maxCorrection);
    }

    appliedBpm_ = std::clamp(estimatedBpm_ + phaseCorrectionBpm, kMinBpm, kMaxBpm);

    // Evaluate Phase-Locked condition
    // Requires: sufficient history (at least 8 pulses), stable frequency (within 2 BPM), and phase error within 1% beat (0.01)
    const bool freqStable = std::fabs(instBpm - estimatedBpm_) <= std::max(2.0, estimatedBpm_ * 0.03);
    const bool phaseLocked = (!transportRunning_) || (std::fabs(phaseErrorBeats_) <= 0.01);

    if (historyCount_ >= 8 && freqStable && phaseLocked) {
      consecutiveLockedPulses_++;
      if (consecutiveLockedPulses_ >= 8) {
        status_ = MidiClockSyncStatus::Locked;
      } else {
        status_ = MidiClockSyncStatus::Acquiring;
      }
    } else {
      consecutiveLockedPulses_ = 0;
      status_ = MidiClockSyncStatus::Acquiring;
    }

    res.tempoUpdated = true;
    res.estimatedBpm = estimatedBpm_;
    res.appliedBpm = appliedBpm_;
    res.phaseErrorBeats = phaseErrorBeats_;
    res.status = status_;
    return res;
  }

  double sampleRate_ = 44100.0;
  double minValidIntervalFrames_ = 344.5;  // ~320 BPM
  double maxValidIntervalFrames_ = 4410.0; // ~25 BPM

  MidiClockSyncStatus status_ = MidiClockSyncStatus::Disconnected;
  double estimatedBpm_ = kDefaultBpm;
  double appliedBpm_ = kDefaultBpm;
  double phaseErrorBeats_ = 0.0;
  double estimatedJitterMs_ = 0.0;
  uint64_t pulseCount_ = 0;
  uint64_t lastPulseFrame_ = 0;
  uint32_t consecutiveLockedPulses_ = 0;
  bool transportRunning_ = false;
  uint32_t missedPulses_ = 0;

  double intervals_[kHistorySize]{};
  size_t historyCount_ = 0;
  size_t historyIndex_ = 0;
  double smoothedIntervalFrames_ = 0.0;
};

class MidiRealtimeParser {
 public:
  static constexpr bool isRealtime(uint8_t byte) noexcept {
    return byte >= 0xF8;
  }

  template <typename Callback>
  void parseByte(uint8_t byte, Callback&& cb) {
    if (byte >= 0xF8) {
      cb(byte);
      return;
    }
    if (byte >= 0x80 && byte <= 0xEF) {
      runningStatus_ = byte;
    } else if (byte >= 0xF0 && byte <= 0xF7) {
      runningStatus_ = 0; // System common cancels running status
    }
  }

  uint8_t runningStatus() const noexcept { return runningStatus_; }
  void reset() noexcept { runningStatus_ = 0; }

 private:
  uint8_t runningStatus_ = 0;
};

} // namespace hydra::dsp

