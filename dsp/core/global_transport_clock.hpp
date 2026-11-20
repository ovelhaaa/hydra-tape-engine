#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace hydra::dsp {

enum class MusicalSubdivision : uint8_t {
  Whole = 0,             // 1/1 (4 beats)
  Half = 1,              // 1/2 (2 beats)
  Quarter = 2,           // 1/4 (1 beat)
  Eighth = 3,            // 1/8 (0.5 beat)
  Sixteenth = 4,         // 1/16 (0.25 beat)
  ThirtySecond = 5,      // 1/32 (0.125 beat)
  DottedQuarter = 6,     // 1/4 dotted (1.5 beats)
  DottedEighth = 7,      // 1/8 dotted (0.75 beat)
  TripletQuarter = 8,    // 1/4 triplet (2/3 beat)
  TripletEighth = 9,     // 1/8 triplet (1/3 beat)
  Count = 10
};

enum class TransportState : uint8_t {
  Stopped = 0,
  Playing = 1,
  Paused = 2
};

struct BoundaryEvent {
  uint32_t frameOffset = 0;  // Offset within block (0 .. frames-1)
  uint64_t cycleIndex = 0;   // Monotonic cycle index for this subdivision
};

struct RationalSubdiv {
  uint8_t p;     // Numerator for cycle calculation: floor(p * m / q)
  uint8_t q;     // Denominator
  double beats;  // Nominal duration in quarter-note beats (q / p)
};

class GlobalTransportClock {
 public:
  static constexpr RationalSubdiv kSubdivisions[static_cast<size_t>(MusicalSubdivision::Count)] = {
      {1, 4, 4.0},            // Whole (4 beats)
      {1, 2, 2.0},            // Half (2 beats)
      {1, 1, 1.0},            // Quarter (1 beat)
      {2, 1, 0.5},            // Eighth (0.5 beat)
      {4, 1, 0.25},           // Sixteenth (0.25 beat)
      {8, 1, 0.125},          // ThirtySecond (0.125 beat)
      {2, 3, 1.5},            // DottedQuarter (1.5 beats)
      {4, 3, 0.75},           // DottedEighth (0.75 beat)
      {3, 2, 2.0 / 3.0},      // TripletQuarter (2/3 beat)
      {3, 1, 1.0 / 3.0}       // TripletEighth (1/3 beat)
  };

  GlobalTransportClock() {
    updateIncrements();
  }

  void prepare(double sampleRate) {
    if (std::isfinite(sampleRate) && sampleRate > 0.0) {
      sampleRate_ = sampleRate;
      updateIncrements();
    }
  }

  // Set BPM with phase preservation (instantaneous change without musical position jump)
  void setBpm(double bpm) {
    if (!std::isfinite(bpm) || bpm <= 0.0) {
      return; // Reject invalid/non-finite BPM without modifying state
    }
    const double clampedBpm = std::clamp(bpm, 1.0, 999.0);
    if (clampedBpm == bpm_) return;

    // Snapshot musical position at the moment of change
    // m(n) = m_origin + (n - n_origin) * beatsPerSample
    musicalOrigin_ = currentMusicalPosition();
    sampleOrigin_ = transportSamplePosition_;
    bpm_ = clampedBpm;
    updateIncrements();
  }

  double bpm() const { return bpm_; }
  double sampleRate() const { return sampleRate_; }

  void play() {
    if (state_ != TransportState::Playing) {
      state_ = TransportState::Playing;
    }
  }

  void pause() {
    if (state_ == TransportState::Playing) {
      // Snapshot position so it does not advance while paused
      musicalOrigin_ = currentMusicalPosition();
      sampleOrigin_ = transportSamplePosition_;
      state_ = TransportState::Paused;
    }
  }

  void stop() {
    if (state_ != TransportState::Stopped) {
      musicalOrigin_ = currentMusicalPosition();
      sampleOrigin_ = transportSamplePosition_;
      state_ = TransportState::Stopped;
    }
  }

  void reset() {
    musicalOrigin_ = 0.0;
    transportSamplePosition_ = 0;
    sampleOrigin_ = 0;
    transportGeneration_++;
  }

  void locate(double musicalPositionBeats, uint64_t samplePosition = 0) {
    if (!std::isfinite(musicalPositionBeats) || musicalPositionBeats < 0.0) {
      return; // Reject invalid/non-finite musical position without modifying state
    }
    musicalOrigin_ = musicalPositionBeats;
    transportSamplePosition_ = samplePosition;
    sampleOrigin_ = samplePosition;
    transportGeneration_++;
  }

  static constexpr bool isValidTimeSignature(uint32_t numerator, uint32_t denominator) {
    return (numerator >= 1 && numerator <= 32) &&
           (denominator == 2 || denominator == 4 || denominator == 8 || denominator == 16);
  }

  bool setTimeSignature(uint32_t numerator, uint32_t denominator) {
    if (!isValidTimeSignature(numerator, denominator)) {
      return false; // Reject invalid values without modifying state
    }
    num_ = numerator;
    den_ = denominator;
    beatsPerBar_ = static_cast<double>(num_) * (4.0 / static_cast<double>(den_));
    return true;
  }

  uint32_t timeSignatureNumerator() const { return num_; }
  uint32_t timeSignatureDenominator() const { return den_; }
  double beatsPerBar() const { return beatsPerBar_; }

  TransportState state() const { return state_; }
  bool isPlaying() const { return state_ == TransportState::Playing; }
  uint32_t generation() const { return transportGeneration_; }

  // Absolute sample counter: advances on every DSP audio frame processed
  uint64_t absoluteSamplePosition() const { return absoluteSamplePosition_; }

  // Transport sample counter: advances only when playing
  uint64_t transportSamplePosition() const { return transportSamplePosition_; }

  // Musical position in quarter-note beats (origin at 0.0)
  // Queries represent the START of the current sample frame
  double musicalPosition() const {
    return currentMusicalPosition();
  }

  int64_t beatIndex() const {
    return static_cast<int64_t>(std::floor(musicalPosition()));
  }

  float beatPhase() const {
    const double m = musicalPosition();
    return static_cast<float>(m - std::floor(m));
  }

  int64_t barIndex() const {
    return static_cast<int64_t>(std::floor(musicalPosition() / beatsPerBar_));
  }

  float barPhase() const {
    const double m = musicalPosition();
    const double b = beatsPerBar_;
    const double bar = std::floor(m / b);
    return static_cast<float>((m - bar * b) / b);
  }

  static constexpr double subdivisionBeats(MusicalSubdivision subdiv) {
    const size_t idx = static_cast<size_t>(subdiv);
    return (idx < static_cast<size_t>(MusicalSubdivision::Count)) ? kSubdivisions[idx].beats : 1.0;
  }

  double subdivisionSamples(MusicalSubdivision subdiv) const {
    return subdivisionBeats(subdiv) * samplesPerBeat_;
  }

  uint64_t subdivisionCycleIndex(MusicalSubdivision subdiv) const {
    return cycleIndexAt(subdiv, musicalPosition());
  }

  float subdivisionPhase(MusicalSubdivision subdiv) const {
    const size_t idx = static_cast<size_t>(subdiv);
    if (idx >= static_cast<size_t>(MusicalSubdivision::Count)) return 0.0f;
    const double m = musicalPosition();
    const double scaled = (static_cast<double>(kSubdivisions[idx].p) * m) /
                          static_cast<double>(kSubdivisions[idx].q);
    return static_cast<float>(scaled - std::floor(scaled));
  }

  // Returns true if the START of the current sample frame is on a subdivision boundary
  bool isSubdivisionBoundary(MusicalSubdivision subdiv) const {
    return isSubdivisionBoundaryAt(subdiv, transportSamplePosition_);
  }

  // Same predicate at an arbitrary transport sample. Block-level reverse
  // scheduling uses it to reconcile the closed-form boundary solver with the
  // per-sample definition at floating-point ties.
  bool isSubdivisionBoundaryAt(MusicalSubdivision subdiv, uint64_t transportSample) const {
    if (state_ != TransportState::Playing) return false;
    if (transportSample == 0) return true; // Origin is always boundary
    const uint64_t currentCycle = cycleIndexAt(subdiv, musicalPositionAt(transportSample));
    const uint64_t prevCycle = cycleIndexAt(subdiv, musicalPositionAt(transportSample - 1));
    return currentCycle > prevCycle;
  }

  // Distance in sample frames from the start of the current frame to the next boundary
  int32_t framesToNextBoundary(MusicalSubdivision subdiv) const {
    if (state_ != TransportState::Playing || beatsPerSample_ <= 0.0) {
      return static_cast<int32_t>(std::ceil(subdivisionSamples(subdiv)));
    }
    const size_t idx = static_cast<size_t>(subdiv);
    if (idx >= static_cast<size_t>(MusicalSubdivision::Count)) return 0;

    const double m = musicalPosition();
    const uint64_t currentCycle = cycleIndexAt(subdiv, m);
    const double nextBoundaryBeats = static_cast<double>(currentCycle + 1) *
                                     (static_cast<double>(kSubdivisions[idx].q) /
                                      static_cast<double>(kSubdivisions[idx].p));
    const double deltaBeats = nextBoundaryBeats - m;
    const double frames = deltaBeats / beatsPerSample_;
    return std::max<int32_t>(1, static_cast<int32_t>(std::ceil(frames - 1e-9)));
  }

  // Advances the clock by exactly 1 sample frame
  void tick() {
    absoluteSamplePosition_++;
    if (state_ == TransportState::Playing) {
      transportSamplePosition_++;
    }
  }

  // Advances the clock by K sample frames
  void advanceBlock(uint32_t frames) {
    absoluteSamplePosition_ += frames;
    if (state_ == TransportState::Playing) {
      transportSamplePosition_ += frames;
    }
  }

  // Collects all subdivision boundaries that occur within a block of [frames] samples,
  // relative to the block start (frameOffset in 0 .. frames-1).
  // Zero dynamic allocation.
  uint32_t getSubdivisionBoundariesInBlock(MusicalSubdivision subdiv,
                                           uint32_t frames,
                                           BoundaryEvent* outEvents,
                                           uint32_t maxEvents) const {
    if (!outEvents || maxEvents == 0 || frames == 0 || state_ != TransportState::Playing) {
      return 0;
    }
    const size_t idx = static_cast<size_t>(subdiv);
    if (idx >= static_cast<size_t>(MusicalSubdivision::Count)) return 0;

    const double p = static_cast<double>(kSubdivisions[idx].p);
    const double q = static_cast<double>(kSubdivisions[idx].q);
    const double cycleDurationBeats = q / p;

    uint32_t eventCount = 0;
    const uint64_t startSample = transportSamplePosition_;
    const uint64_t endSample = startSample + frames;

    // Check if frame 0 itself is a boundary
    const uint64_t startCycle = cycleIndexAt(subdiv, musicalPositionAt(startSample));
    if (startSample == 0) {
      outEvents[eventCount++] = {0, startCycle};
    } else {
      const uint64_t beforeCycle = cycleIndexAt(subdiv, musicalPositionAt(startSample - 1));
      if (startCycle > beforeCycle) {
        outEvents[eventCount++] = {0, startCycle};
      }
    }

    // Next boundary cycle to look for
    uint64_t nextCycle = startCycle + 1;
    while (eventCount < maxEvents) {
      const double targetBeats = static_cast<double>(nextCycle) * cycleDurationBeats;
      const double deltaBeats = targetBeats - musicalOrigin_;
      // Solve: (targetSample - sampleOrigin_) * beatsPerSample_ >= deltaBeats
      const double targetSampleRel = deltaBeats / beatsPerSample_;
      const int64_t targetSample = static_cast<int64_t>(std::ceil(targetSampleRel + static_cast<double>(sampleOrigin_) - 1e-9));

      if (targetSample >= static_cast<int64_t>(endSample)) {
        break; // Beyond this block
      }

      if (targetSample >= static_cast<int64_t>(startSample)) {
        const uint32_t offset = static_cast<uint32_t>(targetSample - static_cast<int64_t>(startSample));
        if (offset < frames) {
          // Avoid duplicate at offset 0
          if (eventCount == 0 || outEvents[eventCount - 1].frameOffset != offset) {
            outEvents[eventCount++] = {offset, nextCycle};
          }
        }
      }
      nextCycle++;
    }

    // The closed-form solver and the per-sample predicate can disagree by one
    // frame at floating-point ties. The predicate is authoritative: nudge each
    // solved offset by one frame, drop solver artefacts, and deduplicate.
    uint32_t kept = 0;
    for (uint32_t e = 0; e < eventCount; ++e) {
      int32_t offset = static_cast<int32_t>(outEvents[e].frameOffset);
      if (!isSubdivisionBoundaryAt(subdiv, startSample + static_cast<uint64_t>(offset))) {
        if (offset > 0 && isSubdivisionBoundaryAt(subdiv, startSample + static_cast<uint64_t>(offset - 1))) {
          --offset;
        } else if (offset + 1 < static_cast<int32_t>(frames) &&
                   isSubdivisionBoundaryAt(subdiv, startSample + static_cast<uint64_t>(offset + 1))) {
          ++offset;
        } else {
          continue;
        }
      }
      if (kept > 0 && outEvents[kept - 1].frameOffset == static_cast<uint32_t>(offset)) {
        continue;
      }
      outEvents[kept] = outEvents[e];
      outEvents[kept].frameOffset = static_cast<uint32_t>(offset);
      ++kept;
    }
    return kept;
  }

  // Reverse V2 division mapping helper
  static constexpr MusicalSubdivision fromReverseDivision(int revDiv) {
    switch (revDiv) {
      case 1: return MusicalSubdivision::Whole;
      case 2: return MusicalSubdivision::Half;
      case 0: return MusicalSubdivision::Quarter;
      case 3: return MusicalSubdivision::Eighth;
      case 4: return MusicalSubdivision::Sixteenth;
      case 5: return MusicalSubdivision::DottedQuarter;
      case 6: return MusicalSubdivision::DottedEighth;
      case 7: return MusicalSubdivision::TripletQuarter;
      case 8: return MusicalSubdivision::TripletEighth;
      default: return MusicalSubdivision::Quarter;
    }
  }

 private:
  void updateIncrements() {
    samplesPerBeat_ = (60.0 * sampleRate_) / bpm_;
    beatsPerSample_ = bpm_ / (60.0 * sampleRate_);
  }

  double currentMusicalPosition() const {
    if (state_ == TransportState::Stopped || state_ == TransportState::Paused) {
      return musicalOrigin_;
    }
    const int64_t deltaSamples = static_cast<int64_t>(transportSamplePosition_) -
                                 static_cast<int64_t>(sampleOrigin_);
    return musicalOrigin_ + static_cast<double>(deltaSamples) * beatsPerSample_;
  }

  double musicalPositionAt(uint64_t transportSample) const {
    const int64_t deltaSamples = static_cast<int64_t>(transportSample) -
                                 static_cast<int64_t>(sampleOrigin_);
    return musicalOrigin_ + static_cast<double>(deltaSamples) * beatsPerSample_;
  }

  static uint64_t cycleIndexAt(MusicalSubdivision subdiv, double musicalPos) {
    const size_t idx = static_cast<size_t>(subdiv);
    if (idx >= static_cast<size_t>(MusicalSubdivision::Count)) return 0;
    const double scaled = (static_cast<double>(kSubdivisions[idx].p) * musicalPos) /
                          static_cast<double>(kSubdivisions[idx].q);
    return static_cast<uint64_t>(std::max(0.0, std::floor(scaled)));
  }

  double sampleRate_ = 48000.0;
  double bpm_ = 120.0;
  double samplesPerBeat_ = 24000.0;
  double beatsPerSample_ = 1.0 / 24000.0;

  double musicalOrigin_ = 0.0;
  uint64_t sampleOrigin_ = 0;
  uint64_t transportSamplePosition_ = 0;
  uint64_t absoluteSamplePosition_ = 0;

  uint32_t num_ = 4;
  uint32_t den_ = 4;
  double beatsPerBar_ = 4.0;

  TransportState state_ = TransportState::Playing;
  uint32_t transportGeneration_ = 1;
};

}  // namespace hydra::dsp
