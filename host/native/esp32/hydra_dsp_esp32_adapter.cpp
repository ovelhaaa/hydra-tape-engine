#include "hydra_dsp_esp32_adapter.h"

#if defined(ESP32) || defined(ARDUINO)
#include "esp_timer.h"
static inline int64_t getSystemTimeUs() { return esp_timer_get_time(); }
#else
#include <chrono>
static inline int64_t getSystemTimeUs() {
  auto now = std::chrono::steady_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::microseconds>(now).count();
}
#endif

namespace {

bool sameParams(const hydra_dsp_params& a, const hydra_dsp_params& b) {
  return a.flutterDepth == b.flutterDepth &&
         a.wowDepth == b.wowDepth &&
         a.dropoutSeverity == b.dropoutSeverity &&
         a.drive == b.drive &&
         a.noise == b.noise &&
         a.tapeSpeed == b.tapeSpeed &&
         a.tapeAge == b.tapeAge &&
         a.headBumpAmount == b.headBumpAmount &&
         a.azimuthError == b.azimuthError &&
         a.flutterRate == b.flutterRate &&
         a.wowRate == b.wowRate &&
         a.delayActive == b.delayActive &&
         a.delayTimeMs == b.delayTimeMs &&
         a.feedback == b.feedback &&
         a.dryWet == b.dryWet &&
         a.delayWet == b.delayWet &&
         a.activeHeads == b.activeHeads &&
         a.bpm == b.bpm &&
         a.headsMusical == b.headsMusical &&
         a.guitarFocus == b.guitarFocus &&
         a.tone == b.tone &&
         a.pingPong == b.pingPong &&
         a.freeze == b.freeze &&
         a.reverse == b.reverse &&
         a.reverseSmear == b.reverseSmear &&
         a.spring == b.spring &&
         a.springDecay == b.springDecay &&
         a.springDamping == b.springDamping &&
         a.springMix == b.springMix &&
         a.warpActive == b.warpActive &&
         a.warpSpeed == b.warpSpeed &&
         a.warpGrainMs == b.warpGrainMs &&
         a.warpStereo == b.warpStereo &&
         a.warpFeedback == b.warpFeedback &&
         a.warpMode == b.warpMode;
}

}  // namespace

HydraDspEsp32Adapter::HydraDspEsp32Adapter(float sampleRate, float maxDelayMs,
                                           float historyMs)
    : sampleRate_(sampleRate) {
  if (hydra_dsp_create_v1_4(sampleRate, maxDelayMs, historyMs, &handle_) == 0) {
    if (hydra_dsp_prepare(handle_, 128, HYDRA_DSP_CHANNELS_STEREO) != 0) {
      hydra_dsp_destroy(handle_);
      handle_ = nullptr;
    }
  }
}

HydraDspEsp32Adapter::~HydraDspEsp32Adapter() {
  hydra_dsp_destroy(handle_);
}

bool HydraDspEsp32Adapter::isValid() const {
  return handle_ != nullptr;
}

bool HydraDspEsp32Adapter::isReverseClamped() const {
  int clamped = 0;
  if (handle_ && hydra_dsp_get_reverse_status(handle_, nullptr, &clamped) == 0) {
    return clamped != 0;
  }
  return false;
}

float HydraDspEsp32Adapter::effectiveReverseMs() const {
  float ms = 0.0f;
  if (handle_ && hydra_dsp_get_reverse_status(handle_, nullptr, nullptr) == 0) {
    float effMs = 0.0f;
    hydra_dsp_get_reverse_status(handle_, &effMs, nullptr);
    return effMs;
  }
  return 0.0f;
}

void HydraDspEsp32Adapter::updateParams(const hydra_dsp_params& params) {
  updateParams({params, HYDRA_DSP_REVERSE_LEGACY, HYDRA_DSP_REVERSE_QUARTER});
}

void HydraDspEsp32Adapter::updateParams(const hydra_dsp_params_v1_4& params) {
  applyParams(params, false);
}

bool HydraDspEsp32Adapter::applyParams(const hydra_dsp_params_v1_4& params, bool force) {
  if (!handle_) return false;
  if (!force && paramsInitialized_ && sameParams(params_.v1_3, params.v1_3) &&
      params_.reverseMode == params.reverseMode && params_.reverseDivision == params.reverseDivision) return false;

  const int setResult = hydra_dsp_set_params_sized(handle_, &params, sizeof(params));
  if (setResult != 0) return false;
  const int commitResult = hydra_dsp_commit(handle_);
  if (commitResult != 0) return false;

  params_ = params;
  paramsInitialized_ = true;
  return true;
}

void HydraDspEsp32Adapter::updateFilters() {
  applyParams(params_, true);
}

float HydraDspEsp32Adapter::process(float input) {
  if (!handle_) return input;
  float outL = input;
  float outR = input;
  if (processStereoBlock(&input, &input, &outL, &outR, 1) != 0) {
    return input;
  }
  return outL;
}

void HydraDspEsp32Adapter::processStereo(float inL, float inR, float* outL, float* outR) {
  if (!outL || !outR) return;
  if (processStereoBlock(&inL, &inR, outL, outR, 1) != 0) {
    *outL = inL;
    *outR = inR;
  }
}

int HydraDspEsp32Adapter::processStereoBlock(const float* inL,
                                             const float* inR,
                                             float* outL,
                                             float* outR,
                                             uint32_t frames) {
  if (!handle_ || !inL || !inR || !outL || !outR) return -1;

  // Drain RT-safe handoff command queue at the beginning of the audio block
  drainCommandQueue();

  const int res = hydra_dsp_process(handle_, inL, inR, outL, outR, frames);

  // Publish telemetry state snapshot lock-free (wait-free seqlock)
  hydra_dsp_tap_tempo_state tapSt{};
  if (hydra_dsp_tap_tempo_get_state(handle_, &tapSt) == 0) {
    publishedTapTempoState_.publish(tapSt);
  }
  hydra_dsp_transport_state transSt{};
  if (hydra_dsp_transport_get_state(handle_, &transSt) == 0) {
    publishedTransportState_.publish(transSt);
  }
  hydra_dsp_midi_clock_state midiSt{};
  if (hydra_dsp_midi_clock_get_state(handle_, &midiSt) == 0) {
    publishedMidiClockState_.publish(midiSt);
  }

  return res;
}

void HydraDspEsp32Adapter::drainCommandQueue() {
  if (!handle_) return;
  DspHandoffCommand cmd;
  while (commandQueue_.pop(cmd)) {
    switch (cmd.type) {
      case DspHandoffCommandType::TapTempoEvent: {
        uint64_t ts = cmd.timestamp_frames;
        if (cmd.arrival_time_us > 0 && ts == 0) {
          // Precise physical arrival to sample frame mapping:
          // Subtract the elapsed time between arrival and current block execution
          hydra_dsp_transport_state st{};
          if (hydra_dsp_transport_get_state(handle_, &st) == 0) {
            const int64_t nowUs = getSystemTimeUs();
            const int64_t deltaUs = nowUs - cmd.arrival_time_us;
            if (deltaUs >= 0) {
              const uint64_t deltaFrames = static_cast<uint64_t>((deltaUs * sampleRate_) / 1000000.0);
              ts = (st.absolute_sample_position >= deltaFrames)
                       ? (st.absolute_sample_position - deltaFrames)
                       : 0;
            } else {
              ts = st.absolute_sample_position;
            }
          }
        }
        hydra_dsp_tap_tempo_event(handle_, ts);
        break;
      }
      case DspHandoffCommandType::TapTempoReset:
        hydra_dsp_tap_tempo_reset(handle_);
        break;
      case DspHandoffCommandType::TransportPlay:
        hydra_dsp_transport_play(handle_);
        break;
      case DspHandoffCommandType::TransportPause:
        hydra_dsp_transport_pause(handle_);
        break;
      case DspHandoffCommandType::TransportStop:
        hydra_dsp_transport_stop(handle_);
        break;
      case DspHandoffCommandType::TransportReset:
        hydra_dsp_transport_reset(handle_);
        break;
      case DspHandoffCommandType::TransportSetBpm:
        hydra_dsp_transport_set_bpm(handle_, cmd.bpm);
        break;
      case DspHandoffCommandType::TransportSetTimeSignature:
        hydra_dsp_transport_set_time_signature(handle_, cmd.num, cmd.den);
        break;
      case DspHandoffCommandType::MidiClockReset:
        hydra_dsp_midi_clock_reset(handle_);
        break;
      case DspHandoffCommandType::SetTempoSource:
        hydra_dsp_set_tempo_source(handle_, cmd.tempo_source);
        break;
      default:
        break;
    }
  }

  // Drain dedicated MIDI clock event queue
  DspMidiEvent midiEv;
  while (midiQueue_.pop(midiEv)) {
    uint64_t ts = midiEv.timestamp_frames;
    if (midiEv.arrival_time_us > 0 && ts == 0) {
      hydra_dsp_transport_state st{};
      if (hydra_dsp_transport_get_state(handle_, &st) == 0) {
        const int64_t nowUs = getSystemTimeUs();
        const int64_t deltaUs = nowUs - midiEv.arrival_time_us;
        if (deltaUs >= 0) {
          const uint64_t deltaFrames = static_cast<uint64_t>((deltaUs * sampleRate_) / 1000000.0);
          ts = (st.absolute_sample_position >= deltaFrames)
                   ? (st.absolute_sample_position - deltaFrames)
                   : 0;
        } else {
          ts = st.absolute_sample_position;
        }
      }
    }
    hydra_dsp_midi_clock_event(handle_, midiEv.status, ts);
  }
}

// Synchronous Direct API
int HydraDspEsp32Adapter::transportPlay() {
  return handle_ ? hydra_dsp_transport_play(handle_) : -1;
}

int HydraDspEsp32Adapter::transportPause() {
  return handle_ ? hydra_dsp_transport_pause(handle_) : -1;
}

int HydraDspEsp32Adapter::transportStop() {
  return handle_ ? hydra_dsp_transport_stop(handle_) : -1;
}

int HydraDspEsp32Adapter::transportReset() {
  return handle_ ? hydra_dsp_transport_reset(handle_) : -1;
}

int HydraDspEsp32Adapter::transportSetBpm(float bpm) {
  return handle_ ? hydra_dsp_transport_set_bpm(handle_, bpm) : -1;
}

int HydraDspEsp32Adapter::transportSetTimeSignature(uint32_t numerator, uint32_t denominator) {
  return handle_ ? hydra_dsp_transport_set_time_signature(handle_, numerator, denominator) : -1;
}

int HydraDspEsp32Adapter::transportGetState(hydra_dsp_transport_state* outState) const {
  if (!outState) return -1;
  if (publishedTransportState_.read(*outState)) {
    return 0;
  }
  return handle_ ? hydra_dsp_transport_get_state(handle_, outState) : -1;
}

int HydraDspEsp32Adapter::tapTempoEvent(uint64_t timestamp_frames) {
  return handle_ ? hydra_dsp_tap_tempo_event(handle_, timestamp_frames) : -1;
}

int HydraDspEsp32Adapter::tapTempoGetState(hydra_dsp_tap_tempo_state* outState) const {
  if (!outState) return -1;
  if (publishedTapTempoState_.read(*outState)) {
    return 0;
  }
  return handle_ ? hydra_dsp_tap_tempo_get_state(handle_, outState) : -1;
}

int HydraDspEsp32Adapter::tapTempoReset() {
  return handle_ ? hydra_dsp_tap_tempo_reset(handle_) : -1;
}

// Asynchronous RT-Safe Handoff API
bool HydraDspEsp32Adapter::postTapTempoEvent(uint64_t timestamp_frames, int64_t arrival_time_us) {
  DspHandoffCommand cmd;
  cmd.type = DspHandoffCommandType::TapTempoEvent;
  cmd.timestamp_frames = timestamp_frames;
  cmd.arrival_time_us = (arrival_time_us > 0) ? arrival_time_us : getSystemTimeUs();
  return commandQueue_.push(cmd);
}

bool HydraDspEsp32Adapter::postTapTempoReset() {
  DspHandoffCommand cmd;
  cmd.type = DspHandoffCommandType::TapTempoReset;
  return commandQueue_.push(cmd);
}

bool HydraDspEsp32Adapter::postTransportPlay() {
  DspHandoffCommand cmd;
  cmd.type = DspHandoffCommandType::TransportPlay;
  return commandQueue_.push(cmd);
}

bool HydraDspEsp32Adapter::postTransportPause() {
  DspHandoffCommand cmd;
  cmd.type = DspHandoffCommandType::TransportPause;
  return commandQueue_.push(cmd);
}

bool HydraDspEsp32Adapter::postTransportStop() {
  DspHandoffCommand cmd;
  cmd.type = DspHandoffCommandType::TransportStop;
  return commandQueue_.push(cmd);
}

bool HydraDspEsp32Adapter::postTransportReset() {
  DspHandoffCommand cmd;
  cmd.type = DspHandoffCommandType::TransportReset;
  return commandQueue_.push(cmd);
}

bool HydraDspEsp32Adapter::postTransportSetBpm(float bpm) {
  DspHandoffCommand cmd;
  cmd.type = DspHandoffCommandType::TransportSetBpm;
  cmd.bpm = bpm;
  return commandQueue_.push(cmd);
}

bool HydraDspEsp32Adapter::postTransportSetTimeSignature(uint32_t numerator, uint32_t denominator) {
  DspHandoffCommand cmd;
  cmd.type = DspHandoffCommandType::TransportSetTimeSignature;
  cmd.num = numerator;
  cmd.den = denominator;
  return commandQueue_.push(cmd);
}

int HydraDspEsp32Adapter::midiClockEvent(uint8_t status, uint64_t timestamp_frames) {
  return handle_ ? hydra_dsp_midi_clock_event(handle_, status, timestamp_frames) : -1;
}

int HydraDspEsp32Adapter::midiClockGetState(hydra_dsp_midi_clock_state* outState) const {
  if (!outState) return -1;
  if (publishedMidiClockState_.read(*outState)) {
    return 0;
  }
  return handle_ ? hydra_dsp_midi_clock_get_state(handle_, outState) : -1;
}

int HydraDspEsp32Adapter::midiClockReset() {
  return handle_ ? hydra_dsp_midi_clock_reset(handle_) : -1;
}

int HydraDspEsp32Adapter::setTempoSource(uint32_t source) {
  return handle_ ? hydra_dsp_set_tempo_source(handle_, source) : -1;
}

int HydraDspEsp32Adapter::getTempoSource(uint32_t* outSource) const {
  return handle_ ? hydra_dsp_get_tempo_source(handle_, outSource) : -1;
}

bool HydraDspEsp32Adapter::postMidiClockEvent(uint8_t status, uint64_t timestamp_frames, int64_t arrival_time_us) {
  DspMidiEvent ev;
  ev.status = status;
  ev.timestamp_frames = timestamp_frames;
  ev.arrival_time_us = (arrival_time_us > 0) ? arrival_time_us : getSystemTimeUs();
  return midiQueue_.push(ev);
}

bool HydraDspEsp32Adapter::postMidiClockReset() {
  DspHandoffCommand cmd;
  cmd.type = DspHandoffCommandType::MidiClockReset;
  return commandQueue_.push(cmd);
}

bool HydraDspEsp32Adapter::postSetTempoSource(uint32_t source) {
  DspHandoffCommand cmd;
  cmd.type = DspHandoffCommandType::SetTempoSource;
  cmd.tempo_source = source;
  return commandQueue_.push(cmd);
}
