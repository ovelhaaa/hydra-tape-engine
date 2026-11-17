#pragma once

#include "hydra_dsp.h"
#include "spsc_queue.hpp"

enum class DspHandoffCommandType : uint8_t {
  None = 0,
  TapTempoEvent,
  TapTempoReset,
  TransportPlay,
  TransportPause,
  TransportStop,
  TransportReset,
  TransportSetBpm,
  TransportSetTimeSignature
};

struct DspHandoffCommand {
  DspHandoffCommandType type = DspHandoffCommandType::None;
  uint64_t timestamp_frames = 0;
  int64_t arrival_time_us = 0;
  float bpm = 0.0f;
  uint32_t num = 0;
  uint32_t den = 0;
};

class HydraDspEsp32Adapter {
public:
  HydraDspEsp32Adapter(float sampleRate, float maxDelayMs = 2000.0f,
                       float historyMs = 0.0f);
  ~HydraDspEsp32Adapter();

  bool isValid() const;
  bool isReverseClamped() const;
  float effectiveReverseMs() const;
  void updateParams(const hydra_dsp_params& params);
  void updateParams(const hydra_dsp_params_v1_4& params);
  void updateFilters();
  float process(float input);
  void processStereo(float inL, float inR, float* outL, float* outR);
  int processStereoBlock(const float* inL,
                         const float* inR,
                         float* outL,
                         float* outR,
                         uint32_t frames);

  // Synchronous Direct API (for single-thread execution or direct tests)
  int transportPlay();
  int transportPause();
  int transportStop();
  int transportReset();
  int transportSetBpm(float bpm);
  int transportSetTimeSignature(uint32_t numerator, uint32_t denominator);
  int transportGetState(hydra_dsp_transport_state* outState) const;

  int tapTempoEvent(uint64_t timestamp_frames = 0);
  int tapTempoGetState(hydra_dsp_tap_tempo_state* outState) const;
  int tapTempoReset();

  // Asynchronous Real-Time Safe Handoff API (Lock-Free SPSC queue from Core 0 / CLI / GPIO)
  bool postTapTempoEvent(uint64_t timestamp_frames = 0, int64_t arrival_time_us = 0);
  bool postTapTempoReset();
  bool postTransportPlay();
  bool postTransportPause();
  bool postTransportStop();
  bool postTransportReset();
  bool postTransportSetBpm(float bpm);
  bool postTransportSetTimeSignature(uint32_t numerator, uint32_t denominator);

  // Drain pending handoff commands (executed at block start in audio task)
  void drainCommandQueue();

private:
  bool applyParams(const hydra_dsp_params_v1_4& params, bool force);

  hydra_dsp_handle* handle_ = nullptr;
  hydra_dsp_params_v1_4 params_{};
  bool paramsInitialized_ = false;
  float sampleRate_ = 44100.0f;

  // Real-Time Safe Lock-Free Handoff Structures
  hydra::dsp::SpscQueue<DspHandoffCommand, 32> commandQueue_;
  mutable hydra::dsp::StatePublisher<hydra_dsp_tap_tempo_state> publishedTapTempoState_;
  mutable hydra::dsp::StatePublisher<hydra_dsp_transport_state> publishedTransportState_;
};
