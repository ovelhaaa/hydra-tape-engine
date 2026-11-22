#pragma once

#include <cstdint>

#if defined(HYDRA_DSP_STAGE_PROFILE) && (defined(ARDUINO_ARCH_ESP32) || defined(ESP_PLATFORM))
#include "esp_cpu.h"
#define HYDRA_PROFILE_COMPILED 1
#else
#define HYDRA_PROFILE_COMPILED 0
#endif

namespace hydra::dsp::profile {
// All state belongs to the audio owner. Publish a snapshot only after that owner
// has stopped collecting; neither the CLI nor a second DSP instance may mutate it.
enum Stage : unsigned {
  ModulationNoise, Warp, ReverseControl, RecordFilters, ConventionalReads,
  ReverseReads, Crossfade, PlaybackFilters, Feedback, RecordAmplifier,
  MagneticWrite, Limiter, Auxiliary, Transport, Interpolation,
  ParameterClear, ParameterFilters, ParameterApply, Count
};
inline constexpr const char* names[Count] = {
  "modulation_noise", "warp", "reverse_control", "record_filters", "conventional_reads",
  "reverse_reads", "crossfade", "playback_filters", "feedback", "record_amplifier",
  "magnetic_write", "limiter", "auxiliary", "transport", "interpolation",
  "parameter_clear", "parameter_filters", "parameter_apply"
};
struct Counters {
  uint64_t cycles[Count]{};
  uint32_t calls[Count]{};
};
inline Counters block;
inline bool enabled = false;
inline void beginBlock(bool collect) {
  enabled = collect;
  if (collect) block = Counters{};
}
inline void endBlock() { enabled = false; }
#if HYDRA_PROFILE_COMPILED
inline __attribute__((always_inline)) uint32_t start() {
  return enabled ? esp_cpu_get_cycle_count() : 0u;
}
inline __attribute__((always_inline)) void finish(Stage stage, uint32_t before) {
  if (enabled) {
    const uint32_t elapsed = esp_cpu_get_cycle_count() - before;
    block.cycles[stage] += elapsed;
    ++block.calls[stage];
  }
}
#endif
}  // namespace hydra::dsp::profile

#if HYDRA_PROFILE_COMPILED
#define HYDRA_PROFILE_START(name) const uint32_t name = ::hydra::dsp::profile::start()
#define HYDRA_PROFILE_END(stage, name) ::hydra::dsp::profile::finish(::hydra::dsp::profile::stage, name)
#else
#define HYDRA_PROFILE_START(name) ((void)0)
#define HYDRA_PROFILE_END(stage, name) ((void)0)
#endif
