// F5 musical-timing contract.
//
// Built three times with Classic C=1 / F2=2 / F3=0 and
// HYDRA_DSP_REFORM_F5_TIMING_VARIANT=0,1,2, always with the read-only test
// hooks. It proves the transport stays exact, the glides are bounded and
// convergent, the three heads keep their musical ratios, Tap/MIDI keep their
// contracts, Reverse V2 stays aligned and block-size changes do not change the
// rendered samples.
#include "check.h"
#include "f5_timing_hooks.hpp"
#include "hydra_dsp.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

constexpr int kRate = 44100;
constexpr int kBlock = 128;

#if HYDRA_DSP_REFORM_F5_TIMING_VARIANT == 0
constexpr float kMaxHeadStep = 1e9f;  // F3-A jumps; only transport is asserted
#elif HYDRA_DSP_REFORM_F5_TIMING_VARIANT == 1
constexpr float kMaxHeadStep = 0.120f;
#else
constexpr float kMaxHeadStep = 0.120f;  // Internal source uses the capstan cap
#endif

struct Engine {
  hydra_dsp_handle* handle = nullptr;

  Engine() {
    CHECK(hydra_dsp_create_v1_4(kRate, 2000.0f, 8000.0f, &handle) == 0);
    CHECK(handle != nullptr);
    CHECK(hydra_dsp_prepare(handle, kBlock, HYDRA_DSP_CHANNELS_STEREO) == 0);
    hydra_dsp_reset(handle);
    set(HYDRA_DSP_PARAM_FLUTTER_DEPTH, 0.0f);
    set(HYDRA_DSP_PARAM_WOW_DEPTH, 0.0f);
    set(HYDRA_DSP_PARAM_DROPOUT, 0.0f);
    set(HYDRA_DSP_PARAM_NOISE, 0.0f);
    set(HYDRA_DSP_PARAM_AZIMUTH, 0.0f);
    set(HYDRA_DSP_PARAM_DELAY_ACTIVE, 1.0f);
    set(HYDRA_DSP_PARAM_DELAY_MS, 500.0f);
    set(HYDRA_DSP_PARAM_FEEDBACK, 65.0f);
    set(HYDRA_DSP_PARAM_DRY_WET, 50.0f);
    set(HYDRA_DSP_PARAM_DELAY_WET, 50.0f);
    set(HYDRA_DSP_PARAM_ACTIVE_HEADS, 4.0f);
    set(HYDRA_DSP_PARAM_BPM, 120.0f);
    set(HYDRA_DSP_PARAM_HEADS_MUSICAL, 1.0f);
    set(HYDRA_DSP_PARAM_TONE, 50.0f);
    set(HYDRA_DSP_PARAM_GUITAR_FOCUS, 0.0f);
    CHECK(hydra_dsp_set_tempo_source(handle, 0) == 0);
    CHECK(hydra_dsp_transport_play(handle) == 0);
    CHECK(hydra_dsp_commit(handle) == 0);
  }
  ~Engine() { hydra_dsp_destroy(handle); }

  void set(hydra_dsp_param_id id, float value) {
    CHECK(hydra_dsp_set_parameter(handle, id, value) == 0);
  }
  void commit() { CHECK(hydra_dsp_commit(handle) == 0); }
  void process(const float* l, const float* r, float* ol, float* orr, uint32_t n) {
    CHECK(hydra_dsp_process(handle, l, r, ol, orr, n) == 0);
  }
  hydra_f5_timing_state timing() const {
    hydra_f5_timing_state s{};
    CHECK(hydra_dsp_test_timing_state(handle, &s) == 0);
    return s;
  }
};

float lcg(uint32_t& s) {
  s = s * 1664525u + 1013904223u;
  return static_cast<float>(s & 0xFFFFFFu) / 8388608.0f - 1.0f;
}

std::vector<float> make_signal(int frames, int channel) {
  std::vector<float> y(static_cast<size_t>(frames));
  uint32_t seed = channel == 0 ? 20261124u : 20261125u;
  const double two_pi = 6.283185307179586;
  for (int i = 0; i < frames; ++i) {
    const double t = static_cast<double>(i) / kRate;
    const double am = 0.85 + 0.15 * std::sin(two_pi * 0.7 * t);
    y[i] = static_cast<float>(
        am * (0.14 * std::sin(two_pi * 110.0 * t + channel * 0.15) +
              0.10 * std::sin(two_pi * 220.0 * t + 0.3 + channel * 0.25) +
              0.06 * std::sin(two_pi * 440.0 * t + 1.1 + channel * 0.2) +
              0.035 * std::sin(two_pi * 1760.0 * t + 0.7 + channel * 0.3)) +
        0.002 * lcg(seed));
  }
  return y;
}

struct Trajectory {
  std::vector<float> outL;
  std::vector<float> outR;
  std::vector<float> head3;
  std::vector<float> target3;
  std::vector<float> head1;
  std::vector<float> head2;
  std::vector<double> position;
  std::vector<float> bpm;
  std::vector<uint64_t> frames;
};

// Renders with a BPM change at `event` (frame multiple of `block`). When
// `perFrame` is set, one diagnostics sample is stored per frame; otherwise one
// per block.
Trajectory render_bpm_event(float fromBpm, float toBpm, int frames, uint64_t event,
                            int block, bool perFrame, bool viaTransportOnly = false) {
  Engine engine;
  auto inL = make_signal(frames, 0);
  auto inR = make_signal(frames, 1);
  Trajectory t;
  t.outL.assign(frames, 0.0f);
  t.outR.assign(frames, 0.0f);
  std::vector<float> oR(frames);
  bool applied = false;
  const int step = perFrame ? 1 : block;
  for (int start = 0; start < frames; start += step) {
    const uint32_t n = static_cast<uint32_t>(std::min(step, frames - start));
    if (!applied && static_cast<uint64_t>(start) == event) {
      if (viaTransportOnly) {
        CHECK(hydra_dsp_transport_set_bpm(engine.handle, toBpm) == 0);
      } else {
        engine.set(HYDRA_DSP_PARAM_BPM, toBpm);
        engine.commit();
      }
      applied = true;
    }
    engine.process(inL.data() + start, inR.data() + start, t.outL.data() + start,
                   oR.data() + start, n);
    if (perFrame || (start % block) == 0) {
      const auto s = engine.timing();
      t.head3.push_back(s.head_delay_samples[2]);
      t.target3.push_back(s.target_head_samples[2]);
      t.head1.push_back(s.head_delay_samples[0]);
      t.head2.push_back(s.head_delay_samples[1]);
      t.position.push_back(s.musical_position);
      t.bpm.push_back(static_cast<float>(s.transport_bpm));
      t.frames.push_back(static_cast<uint64_t>(start) + n);
    }
  }
  CHECK(applied);
  t.outR = std::move(oR);
  return t;
}

std::vector<float> render_musical_120_100(int frames, uint64_t event, int block) {
  Engine engine;
  auto inL = make_signal(frames, 0);
  auto inR = make_signal(frames, 1);
  std::vector<float> outL(frames), outR(frames);
  bool applied = false;
  for (int start = 0; start < frames; start += block) {
    const uint32_t n = static_cast<uint32_t>(std::min(block, frames - start));
    if (!applied && static_cast<uint64_t>(start) == event) {
      engine.set(HYDRA_DSP_PARAM_BPM, 100.0f);
      engine.commit();
      applied = true;
    }
    engine.process(inL.data() + start, inR.data() + start, outL.data() + start,
                   outR.data() + start, n);
  }
  CHECK(applied);
  return outL;
}

void test_transport_stays_exact() {
  const uint64_t event = 132096;  // 2.995 s, block edge
  auto t = render_bpm_event(120.0f, 100.0f, 5 * kRate, event, kBlock, false);
  const size_t idx = static_cast<size_t>(event / kBlock);
  CHECK(idx > 0 && idx < t.bpm.size());
  // The clock switches immediately and preserves musical phase: the position
  // at the event frame equals the previous block's end position, then advances
  // at the new tempo.
  CHECK(std::fabs(t.bpm[idx] - 100.0f) < 1e-6f);
  const double expected = t.position[idx - 1] + 128.0 * 100.0 / (60.0 * kRate);
  CHECK(std::fabs(t.position[idx] - expected) < 1e-9);
  // No drift: the transport keeps advancing at the new tempo.
  const size_t end = t.position.size() - 1;
  const double expectedEnd =
      t.position[idx] + (t.frames[end] - t.frames[idx]) * 100.0 / (60.0 * kRate);
  CHECK(std::fabs(t.position[end] - expectedEnd) < 1e-9);
}

void test_static_scene_is_stable() {
  Engine engine;
  auto inL = make_signal(2 * kRate, 0);
  auto inR = make_signal(2 * kRate, 1);
  std::vector<float> outL(2 * kRate), outR(2 * kRate);
  for (int start = 0; start < 2 * kRate; start += kBlock) {
    const uint32_t n = static_cast<uint32_t>(std::min(kBlock, 2 * kRate - start));
    engine.process(inL.data() + start, inR.data() + start, outL.data() + start,
                   outR.data() + start, n);
  }
  const auto a = engine.timing();
  // A no-op commit (fresh, identical parameters) must not move the read heads.
  engine.set(HYDRA_DSP_PARAM_DRY_WET, 50.0f);
  engine.commit();
  for (int start = 0; start < kRate; start += kBlock) {
    const uint32_t n = static_cast<uint32_t>(std::min(kBlock, kRate - start));
    engine.process(inL.data() + start, inR.data() + start, outL.data() + start,
                   outR.data() + start, n);
  }
  const auto b = engine.timing();
  CHECK(a.musical == 1);
  CHECK(std::fabs(a.head_delay_samples[0] - b.head_delay_samples[0]) == 0.0f);
  CHECK(std::fabs(a.head_delay_samples[1] - b.head_delay_samples[1]) == 0.0f);
  CHECK(std::fabs(a.head_delay_samples[2] - b.head_delay_samples[2]) == 0.0f);
}

#if HYDRA_DSP_REFORM_F5_TIMING_VARIANT != 0
void test_head_ratios_and_bounded_steps() {
  const uint64_t event = 66176;  // 1.5004 s, block edge
  auto t = render_bpm_event(120.0f, 100.0f, 4 * kRate, event, kBlock, true);
  CHECK(t.head3.size() == 4u * kRate);
  float worstStep = 0.0f;
  float worstRatio1 = 0.0f, worstRatio2 = 0.0f;
  for (size_t i = 1; i < t.head3.size(); ++i) {
    worstStep = std::max(worstStep, std::fabs(t.head3[i] - t.head3[i - 1]));
    worstRatio1 = std::max(worstRatio1, std::fabs(t.head1[i] - t.head3[i] * 0.333f));
    worstRatio2 = std::max(worstRatio2, std::fabs(t.head2[i] - t.head3[i] * 0.75f));
  }
  std::fprintf(stderr, "F5 v%d head step=%.6f ratio err=(%.6f, %.6f)\n",
               HYDRA_DSP_REFORM_F5_TIMING_VARIANT, worstStep, worstRatio1, worstRatio2);
  CHECK(worstStep <= kMaxHeadStep + 4e-3f); // one float ulp at ~22k samples
  CHECK(worstRatio1 <= 4e-3f);
  CHECK(worstRatio2 <= 4e-3f);
  // One-pole + hard cap cannot overshoot the target.
  const float target = t.target3.back();
  for (size_t i = static_cast<size_t>(event); i < t.head3.size(); ++i) {
    CHECK(t.head3[i] <= target + 1.0f);
    CHECK(t.head3[i] >= t.head3[i - 1] - 1e-4f);
  }
  CHECK(std::fabs(t.head3.back() - target) <= 0.5f);  // converging tail
}

void test_transport_only_bpm_retargets_heads() {
  const uint64_t event = 66176;
  auto t = render_bpm_event(120.0f, 100.0f, 5 * kRate, event, kBlock, false, true);
  const size_t idx = static_cast<size_t>(event / kBlock);
  CHECK(std::fabs(t.bpm[idx] - 100.0f) < 1e-6f);
  // The read target must follow the clock even without a parameter commit.
  const float err = std::fabs(t.head3.back() - t.target3.back());
  std::fprintf(stderr, "F5 v%d transport-only tail error=%.4f samples\n",
               HYDRA_DSP_REFORM_F5_TIMING_VARIANT, err);
  CHECK(err <= 0.05f);
}

void test_convergence_times() {
  const uint64_t event = 66176;
  auto t = render_bpm_event(120.0f, 100.0f, 6 * kRate, event, kBlock, false);
  const size_t idx = static_cast<size_t>(event / kBlock);
  auto first_within = [&](float limit) -> double {
    for (size_t i = idx; i < t.head3.size(); ++i) {
      if (std::fabs(t.head3[i] - t.target3[i]) <= limit) {
        return static_cast<double>(t.frames[i] - event) / kRate;
      }
    }
    return -1.0;
  };
  const double t1 = first_within(1.0f);    // ~23 us
  const double t44 = first_within(44.1f);  // 1 ms
  std::fprintf(stderr, "F5 v%d 120->100 convergence: 1ms=%.3fs 1sample=%.3fs\n",
               HYDRA_DSP_REFORM_F5_TIMING_VARIANT, t44, t1);
  CHECK(t44 >= 0.0 && t44 <= 1.5);
  CHECK(t1 >= 0.0 && t1 <= 3.5);
}

#if HYDRA_DSP_REFORM_F5_TIMING_VARIANT == 2
void test_clock_policy_is_gentler() {
  // Tap-driven changes must respect the smaller clock caps and still settle.
  Engine engine;
  CHECK(hydra_dsp_set_tempo_source(engine.handle, 1) == 0);
  auto inL = make_signal(8 * kRate, 0);
  auto inR = make_signal(8 * kRate, 1);
  std::vector<float> outL(8 * kRate), outR(8 * kRate);
  std::vector<uint64_t> taps;
  double t = 0.5;
  for (int i = 0; i < 4; ++i) { taps.push_back(static_cast<uint64_t>(t * kRate)); t += 0.5; }
  t += 0.2;
  for (int i = 0; i < 4; ++i) { taps.push_back(static_cast<uint64_t>(t * kRate)); t += 60.0 / 90.0; }
  size_t next = 0;
  float previous = 0.0f;
  bool havePrevious = false;
  float worstStep = 0.0f;
  for (int start = 0; start < 8 * kRate; start += kBlock) {
    const uint32_t n = static_cast<uint32_t>(std::min(kBlock, 8 * kRate - start));
    while (next < taps.size() && taps[next] < static_cast<uint64_t>(start) + n) {
      CHECK(hydra_dsp_tap_tempo_event(engine.handle, taps[next]) == 0);
      ++next;
    }
    engine.process(inL.data() + start, inR.data() + start, outL.data() + start,
                   outR.data() + start, n);
    const auto s = engine.timing();
    if (havePrevious) worstStep = std::max(worstStep, std::fabs(s.head_delay_samples[2] - previous));
    previous = s.head_delay_samples[2];
    havePrevious = true;
  }
  CHECK(next == taps.size());
  const auto end = engine.timing();
  const float err = std::fabs(end.head_delay_samples[2] - end.target_head_samples[2]);
  std::fprintf(stderr, "F5 v2 tap 120->90: worst block step=%.3f tail err=%.3f\n", worstStep, err);
  CHECK(err <= 1.0f);
  CHECK(worstStep <= 0.085f * kBlock + 0.5f);
}
#endif

void test_coarse_convergence_and_finiteness() {
  const uint64_t event = 132096;
  auto t = render_bpm_event(160.0f, 80.0f, 9 * kRate, event, kBlock, false);
  for (float v : t.outL) CHECK(std::isfinite(v) && std::fabs(v) <= 0.99f);
  const size_t idx = static_cast<size_t>(event / kBlock);
  CHECK(std::fabs(t.bpm[idx] - 80.0f) < 1e-6f);
  const float err = std::fabs(t.head3.back() - t.target3.back());
  std::fprintf(stderr, "F5 v%d 160->80 tail error=%.3f samples\n",
               HYDRA_DSP_REFORM_F5_TIMING_VARIANT, err);
  CHECK(err <= 0.5f);
}
#endif  // variant != 0

void test_block_size_independence() {
  constexpr int frames = 8 * kRate;
  constexpr uint64_t event = 303104;  // lcm(1,37,64,128,256)
  std::vector<float> reference;
  for (int block : {1, 37, 64, 128, 256}) {
    auto y = render_musical_120_100(frames, event, block);
    if (reference.empty()) {
      reference = std::move(y);
      continue;
    }
    double worst = 0.0;
    for (size_t i = 0; i < reference.size(); ++i) {
      worst = std::max(worst, std::fabs(double(reference[i] - y[i])));
    }
    std::fprintf(stderr, "F5 v%d block %3d max_abs_vs_block1=%.9g\n",
                 HYDRA_DSP_REFORM_F5_TIMING_VARIANT, block, worst);
    CHECK(worst <= 1e-6);
  }
}

void test_reverse_v2_stays_aligned() {
  Engine engine;
  engine.set(HYDRA_DSP_PARAM_REVERSE, 1.0f);
  engine.set(HYDRA_DSP_PARAM_REVERSE_MODE, 0.0f);
  engine.set(HYDRA_DSP_PARAM_REVERSE_DIVISION, 0.0f);  // quarter note
  engine.set(HYDRA_DSP_PARAM_HEADS_MUSICAL, 1.0f);
  engine.commit();
  auto inL = make_signal(4 * kRate, 0);
  auto inR = make_signal(4 * kRate, 1);
  std::vector<float> outL(4 * kRate), outR(4 * kRate);
  const uint64_t event = (2 * kRate / kBlock) * kBlock;  // 2.0 s, block edge
  bool applied = false;
  for (int start = 0; start < 4 * kRate; start += kBlock) {
    const uint32_t n = static_cast<uint32_t>(std::min(kBlock, 4 * kRate - start));
    if (!applied && static_cast<uint64_t>(start) == event) {
      engine.set(HYDRA_DSP_PARAM_BPM, 90.0f);
      engine.commit();
      applied = true;
    }
    engine.process(inL.data() + start, inR.data() + start, outL.data() + start,
                   outR.data() + start, n);
    CHECK(std::isfinite(outL[start]) && std::fabs(outL[start]) <= 0.99f);
  }
  CHECK(applied);
  float requested = 0.0f, effective = 0.0f;
  int clamped = 0;
  CHECK(hydra_dsp_get_reverse_status(engine.handle, &effective, &clamped) == 0);
  CHECK(hydra_dsp_get_reverse_diagnostics(engine.handle, &requested, &effective, &clamped,
                                          nullptr) == 0);
  std::fprintf(stderr, "F5 v%d reverse requested=%.2fms effective=%.2fms clamped=%d\n",
               HYDRA_DSP_REFORM_F5_TIMING_VARIANT, requested, effective, clamped);
  CHECK(std::fabs(requested - 60000.0f / 90.0f) <= 0.1f);
  CHECK(clamped == 0);
  CHECK(std::fabs(effective - requested) <= 0.5f);
  // The listener-facing head still converges after the reverse is switched off.
  engine.set(HYDRA_DSP_PARAM_REVERSE, 0.0f);
  engine.commit();
  for (int start = 0; start < 3 * kRate; start += kBlock) {
    const uint32_t n = static_cast<uint32_t>(std::min(kBlock, 3 * kRate - start));
    engine.process(inL.data() + start, inR.data() + start, outL.data() + start,
                   outR.data() + start, n);
  }
  const auto end = engine.timing();
  CHECK(std::fabs(end.head_delay_samples[2] - end.target_head_samples[2]) <= 0.5f);
}

void test_midi_clock_bound_and_tracking() {
  Engine engine;
  CHECK(hydra_dsp_set_tempo_source(engine.handle, 2) == 0);
  auto inL = make_signal(8 * kRate, 0);
  auto inR = make_signal(8 * kRate, 1);
  std::vector<float> outL(8 * kRate), outR(8 * kRate);
  struct Pulse { uint64_t frame; uint8_t status; };
  std::vector<Pulse> pulses;
  pulses.push_back({0, 0xFA});
  double frame = 0.0;
  const double change = 2.0;
  int index = 0;
  while (frame < 6.0 * kRate) {
    const double bpm = (frame >= change * kRate) ? 125.0 : 120.0;
    if (index > 0) pulses.push_back({static_cast<uint64_t>(frame), 0xF8});
    frame += 60.0 / (24.0 * bpm) * kRate;
    ++index;
  }
  size_t next = 0;
  float worstStep = 0.0f, previous = 0.0f;
  bool havePrevious = false;
  int lockedSeen = 0;
  for (int start = 0; start < 6 * kRate; start += kBlock) {
    const uint32_t n = static_cast<uint32_t>(std::min(kBlock, 6 * kRate - start));
    while (next < pulses.size() && pulses[next].frame < static_cast<uint64_t>(start) + n) {
      CHECK(hydra_dsp_midi_clock_event(engine.handle, pulses[next].status,
                                       pulses[next].frame) == 0);
      ++next;
    }
    engine.process(inL.data() + start, inR.data() + start, outL.data() + start,
                   outR.data() + start, n);
    hydra_dsp_midi_clock_state st{};
    CHECK(hydra_dsp_midi_clock_get_state(engine.handle, &st) == 0);
    if (st.sync_status == 2) ++lockedSeen;
    const auto s = engine.timing();
    if (havePrevious) worstStep = std::max(worstStep, std::fabs(s.head_delay_samples[2] - previous));
    previous = s.head_delay_samples[2];
    havePrevious = true;
  }
  CHECK(next == pulses.size());
  CHECK(lockedSeen > 0);
  const auto end = engine.timing();
  CHECK(end.transport_bpm > 122.0 && end.transport_bpm < 127.0);
#if HYDRA_DSP_REFORM_F5_TIMING_VARIANT != 0
  // The delay target follows the clock and the glide stays bounded.
  CHECK(std::fabs(end.head_delay_samples[2] - end.target_head_samples[2]) <= 44.1f);
#else
  // Vintage A keeps the documented A2 behaviour: a transport-only tempo change
  // does not retarget the musical heads until the next parameter commit.
#endif
  std::fprintf(stderr, "F5 v%d MIDI tail bpm=%.3f err=%.2f step=%.3f\n",
               HYDRA_DSP_REFORM_F5_TIMING_VARIANT, end.transport_bpm,
               std::fabs(end.head_delay_samples[2] - end.target_head_samples[2]), worstStep);
  CHECK(worstStep <= kMaxHeadStep * kBlock + 1.0f);
}

}  // namespace

int main() {
  std::fprintf(stderr, "F5 timing v%d: transport\n", HYDRA_DSP_REFORM_F5_TIMING_VARIANT);
  test_transport_stays_exact();
  std::fprintf(stderr, "F5 timing v%d: static\n", HYDRA_DSP_REFORM_F5_TIMING_VARIANT);
  test_static_scene_is_stable();
#if HYDRA_DSP_REFORM_F5_TIMING_VARIANT != 0
  std::fprintf(stderr, "F5 timing v%d: heads\n", HYDRA_DSP_REFORM_F5_TIMING_VARIANT);
  test_head_ratios_and_bounded_steps();
  std::fprintf(stderr, "F5 timing v%d: transport-only\n", HYDRA_DSP_REFORM_F5_TIMING_VARIANT);
  test_transport_only_bpm_retargets_heads();
  std::fprintf(stderr, "F5 timing v%d: convergence\n", HYDRA_DSP_REFORM_F5_TIMING_VARIANT);
  test_convergence_times();
  std::fprintf(stderr, "F5 timing v%d: coarse\n", HYDRA_DSP_REFORM_F5_TIMING_VARIANT);
  test_coarse_convergence_and_finiteness();
#if HYDRA_DSP_REFORM_F5_TIMING_VARIANT == 2
  std::fprintf(stderr, "F5 timing v%d: tap policy\n", HYDRA_DSP_REFORM_F5_TIMING_VARIANT);
  test_clock_policy_is_gentler();
#endif
#endif
  std::fprintf(stderr, "F5 timing v%d: block size\n", HYDRA_DSP_REFORM_F5_TIMING_VARIANT);
  test_block_size_independence();
  std::fprintf(stderr, "F5 timing v%d: reverse\n", HYDRA_DSP_REFORM_F5_TIMING_VARIANT);
  test_reverse_v2_stays_aligned();
  std::fprintf(stderr, "F5 timing v%d: midi\n", HYDRA_DSP_REFORM_F5_TIMING_VARIANT);
  test_midi_clock_bound_and_tracking();
  std::printf("F5 timing contract v%d: PASS\n", HYDRA_DSP_REFORM_F5_TIMING_VARIANT);
  return 0;
}
