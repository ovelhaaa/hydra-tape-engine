// F4 opt-in performance-transition contract.
//
// Built with Classic C=1 / F2=2 / F3=0 / F4=1. The production core keeps
// HYDRA_DSP_REFORM_F4_TRANSITIONS=0; this target only qualifies the experiment.
#include "check.h"
#include "hydra_dsp.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace {

constexpr int kRate = 44100;

struct Engine {
  hydra_dsp_handle* handle = nullptr;
  Engine() {
    CHECK(hydra_dsp_create_v1_4(kRate, 2000.0f, 8000.0f, &handle) == 0);
    CHECK(handle != nullptr);
    CHECK(hydra_dsp_prepare(handle, 128, HYDRA_DSP_CHANNELS_STEREO) == 0);
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
    set(HYDRA_DSP_PARAM_TONE, 50.0f);
    CHECK(hydra_dsp_set_tempo_source(handle, 0) == 0);
    CHECK(hydra_dsp_transport_play(handle) == 0);
    CHECK(hydra_dsp_commit(handle) == 0);
  }
  ~Engine() { hydra_dsp_destroy(handle); }
  void set(hydra_dsp_param_id id, float value) {
    CHECK(hydra_dsp_set_parameter(handle, id, value) == 0);
  }
  void process(const float* l, const float* r, float* ol, float* orr, uint32_t n) {
    CHECK(hydra_dsp_process(handle, l, r, ol, orr, n) == 0);
  }
};

float lcg(uint32_t& s) {
  s = s * 1664525u + 1013904223u;
  return static_cast<float>(s & 0xFFFFFFu) / 8388608.0f - 1.0f;
}

// Same steady bed as the evidence bench: harmonic content, no transients.
std::vector<float> make_signal(int frames, int channel) {
  std::vector<float> y(static_cast<size_t>(frames));
  uint32_t seed = channel == 0 ? 20261124u : 20261125u;
  const double two_pi = 6.283185307179586;
  for (int i = 0; i < frames; ++i) {
    const double t = static_cast<double>(i) / kRate;
    const double am = 0.85 + 0.15 * std::sin(two_pi * 0.7 * t);
    y[i] = static_cast<float>(am * (0.14 * std::sin(two_pi * 110.0 * t + channel * 0.15) +
                                    0.10 * std::sin(two_pi * 220.0 * t + 0.3 + channel * 0.25) +
                                    0.06 * std::sin(two_pi * 440.0 * t + 1.1 + channel * 0.2) +
                                    0.035 * std::sin(two_pi * 1760.0 * t + 0.7 + channel * 0.3)) +
           0.002 * lcg(seed));
  }
  return y;
}

std::vector<float> render(const std::vector<float>& inL, const std::vector<float>& inR,
                          int block, uint64_t event_frame, hydra_dsp_param_id id, float value) {
  Engine engine;
  std::vector<float> outL(inL.size()), outR(inL.size());
  bool applied = false;
  for (size_t start = 0; start < inL.size(); start += block) {
    const uint32_t n = static_cast<uint32_t>(
        std::min<size_t>(block, inL.size() - start));
    if (!applied && start == event_frame) {
      engine.set(id, value);
      CHECK(hydra_dsp_commit(engine.handle) == 0);
      applied = true;
    }
    engine.process(inL.data() + start, inR.data() + start,
                   outL.data() + start, outR.data() + start, n);
  }
  CHECK(applied);
  return outL;
}

double max_abs_diff(const std::vector<float>& a, const std::vector<float>& b, size_t from = 0) {
  double worst = 0.0;
  for (size_t i = from; i < a.size(); ++i) worst = std::max(worst, std::fabs(double(a[i] - b[i])));
  return worst;
}

double boundary_step(const std::vector<float>& y, size_t frame, size_t window) {
  double worst = 0.0;
  for (size_t i = frame; i < std::min(y.size(), frame + window); ++i) {
    worst = std::max(worst, std::fabs(double(y[i] - y[i - 1])));
  }
  return worst;
}

double pre_step(const std::vector<float>& y, size_t frame, size_t window) {
  double worst = 0.0;
  for (size_t i = frame > window ? frame - window : 1; i < frame; ++i) {
    worst = std::max(worst, std::fabs(double(y[i] - y[i - 1])));
  }
  return worst;
}

// 6.8733 s: multiple of 1, 37, 64, 128 and 256.
constexpr uint64_t kCommonEvent = 303104;

void test_block_size_independence() {
  const int frames = 8 * kRate;
  auto inL = make_signal(frames, 0);
  auto inR = make_signal(frames, 1);
  std::vector<float> reference;
  for (int block : {1, 37, 64, 128, 256}) {
    auto y = render(inL, inR, block, kCommonEvent, HYDRA_DSP_PARAM_ACTIVE_HEADS, 7.0f);
    if (reference.empty()) {
      reference = y;
      continue;
    }
    const double diff = max_abs_diff(reference, y);
    std::fprintf(stderr, "block %3d head-switch max_abs_vs_block1=%.9g\n", block, diff);
    CHECK(diff <= 1e-6);
  }
}

void test_mix_recovers_static_final_value() {
  const int frames = 2 * kRate;
  auto inL = make_signal(frames, 0);
  auto inR = make_signal(frames, 1);
  Engine moving;
  Engine fixed;
  moving.set(HYDRA_DSP_PARAM_DRY_WET, 0.0f);
  moving.set(HYDRA_DSP_PARAM_DELAY_WET, 0.0f);
  CHECK(hydra_dsp_commit(moving.handle) == 0);
  fixed.set(HYDRA_DSP_PARAM_DRY_WET, 100.0f);
  fixed.set(HYDRA_DSP_PARAM_DELAY_WET, 100.0f);
  CHECK(hydra_dsp_commit(fixed.handle) == 0);
  const uint64_t event = 44096;  // 0.9998 s, block edge
  CHECK(event % 64 == 0);
  std::vector<float> a(frames), aR(frames), b(frames), bR(frames);
  for (int start = 0; start < frames; start += 64) {
    const uint32_t n = static_cast<uint32_t>(std::min(64, frames - start));
    if (start == static_cast<int>(event)) {
      moving.set(HYDRA_DSP_PARAM_DRY_WET, 100.0f);
      moving.set(HYDRA_DSP_PARAM_DELAY_WET, 100.0f);
      CHECK(hydra_dsp_commit(moving.handle) == 0);
    }
    moving.process(inL.data() + start, inR.data() + start, a.data() + start,
                   aR.data() + start, n);
    fixed.process(inL.data() + start, inR.data() + start, b.data() + start,
                  bR.data() + start, n);
  }
  const size_t settled = event + static_cast<size_t>(0.25 * kRate);
  const double diff = std::max(max_abs_diff(a, b, settled), max_abs_diff(aR, bR, settled));
  std::fprintf(stderr, "mix final-value max_abs_after_250ms=%.9g\n", diff);
  CHECK(diff <= 2e-4);
}

void test_head_transition_is_below_natural_slope() {
  const int frames = 3 * kRate;
  auto inL = make_signal(frames, 0);
  auto inR = make_signal(frames, 1);
  Engine engine;
  engine.set(HYDRA_DSP_PARAM_ACTIVE_HEADS, 4.0f);
  CHECK(hydra_dsp_commit(engine.handle) == 0);
  std::vector<float> y(frames), yR(frames);
  const size_t event = 66176;  // 1.5004 s, block edge
  CHECK(event % 128 == 0);
  for (size_t start = 0; start < size_t(frames); start += 128) {
    const uint32_t n = static_cast<uint32_t>(std::min<size_t>(128, frames - start));
    if (start == event) {
      engine.set(HYDRA_DSP_PARAM_ACTIVE_HEADS, 7.0f);
      CHECK(hydra_dsp_commit(engine.handle) == 0);
    }
    engine.process(inL.data() + start, inR.data() + start, y.data() + start,
                   yR.data() + start, n);
  }
  const double natural = pre_step(y, event, static_cast<size_t>(0.05 * kRate));
  const double after = boundary_step(y, event, static_cast<size_t>(0.005 * kRate));
  std::fprintf(stderr, "head natural=%.9g transition_5ms=%.9g\n", natural, after);
  CHECK(after <= std::max(1e-6, natural * 1.25));
  for (float v : y) CHECK(std::isfinite(v) && std::fabs(v) <= 0.99f);
}

void test_commands_during_transition() {
  const int frames = 3 * kRate;
  auto inL = make_signal(frames, 0);
  auto inR = make_signal(frames, 1);
  Engine engine;
  engine.set(HYDRA_DSP_PARAM_DRIVE, 0.0f);
  engine.set(HYDRA_DSP_PARAM_ACTIVE_HEADS, 4.0f);
  CHECK(hydra_dsp_commit(engine.handle) == 0);
  const uint64_t a = 44032, b = 44672, c = 45312, d = 45952;
  std::vector<float> y(frames), yR(frames);
  for (size_t start = 0; start < size_t(frames); start += 128) {
    const uint32_t n = static_cast<uint32_t>(std::min<size_t>(128, frames - start));
    if (start == a) { engine.set(HYDRA_DSP_PARAM_ACTIVE_HEADS, 7.0f); CHECK(hydra_dsp_commit(engine.handle) == 0); }
    if (start == b) { engine.set(HYDRA_DSP_PARAM_ACTIVE_HEADS, 1.0f); CHECK(hydra_dsp_commit(engine.handle) == 0); }
    if (start == c) { engine.set(HYDRA_DSP_PARAM_DRIVE, 100.0f); CHECK(hydra_dsp_commit(engine.handle) == 0); }
    if (start == d) { engine.set(HYDRA_DSP_PARAM_ACTIVE_HEADS, 3.0f); CHECK(hydra_dsp_commit(engine.handle) == 0); }
    engine.process(inL.data() + start, inR.data() + start, y.data() + start,
                   yR.data() + start, n);
  }
  for (float v : y) CHECK(std::isfinite(v) && std::fabs(v) <= 0.99f);
  CHECK(boundary_step(y, 46100, 512) <= 0.2);
}

// Commands at exact sample offsets, independent of process block boundaries.
// Each spacing is shorter than the 10 ms ramp; masks reverse direction.
std::vector<float> rapid_render(int spacing, int block) {
  const int frames=2*kRate;
  auto l=make_signal(frames,0), r=make_signal(frames,1);
  std::vector<float> out(frames), right(frames);
  Engine e;
  e.set(HYDRA_DSP_PARAM_HEADS_MUSICAL,1.f);
  e.set(HYDRA_DSP_PARAM_FEEDBACK,95.f);
  CHECK(hydra_dsp_commit(e.handle)==0);
  const int masks[]={7,1,7,4,3,4,7,1,7,4,3,4};
  int event=0, start=0;
  while(start<frames){
    const int next=kRate+event*spacing;
    if(event<12 && start==next){
      e.set(HYDRA_DSP_PARAM_ACTIVE_HEADS,float(masks[event]));
      e.set(HYDRA_DSP_PARAM_BPM,(event%2)?155.f:85.f);
      e.set(HYDRA_DSP_PARAM_DRIVE,(event%2)?20.f:80.f);
      e.set(HYDRA_DSP_PARAM_DRY_WET,(event%2)?30.f:70.f);
      e.set(HYDRA_DSP_PARAM_DELAY_WET,(event%2)?30.f:70.f);
      if(event==2 || event==6) e.set(HYDRA_DSP_PARAM_FREEZE,event==2?1.f:0.f);
      CHECK(hydra_dsp_commit(e.handle)==0);
      ++event;
    }
    int n=std::min(block,frames-start);
    if(event<12) n=std::min(n,kRate+event*spacing-start);
    CHECK(n>0);
    e.process(l.data()+start,r.data()+start,out.data()+start,right.data()+start,n);
    start+=n;
  }
  CHECK(event==12);
  for(int j=0;j<12;++j){
    const size_t at=kRate+j*spacing;
    // A retarget must not inject a one-sample gain jump. Compare to the
    // measured natural stereo slope before the first gesture, not limiter headroom.
    CHECK(boundary_step(out,at,1)<=std::max(1e-6,pre_step(out,kRate,2205)*1.25));
    CHECK(boundary_step(right,at,1)<=std::max(1e-6,pre_step(right,kRate,2205)*1.25));
  }
  for(int i=0;i<frames;++i){
    CHECK(std::isfinite(out[i]) && std::isfinite(right[i]));
    CHECK(std::fabs(out[i])<=.99f && std::fabs(right[i])<=.99f);
  }
  // Existing F4 discontinuity ceiling, measured throughout the interrupted ramps.
  CHECK(boundary_step(out,kRate,12*spacing+441)<=.2);
  out.insert(out.end(),right.begin(),right.end());
  return out;
}
void test_sub_ramp_retargeting(){
  for(int spacing : {88,176,265}) { // nearest frames to 2,4,6 ms at 44.1 kHz
    const auto reference=rapid_render(spacing,1);
    for(int block : {37,64,128,256}) CHECK(reference==rapid_render(spacing,block));
    std::fprintf(stderr,"rapid commands spacing=%d frames: PASS\n",spacing);
  }
}

void test_freeze_release_is_bounded() {
  const int frames = 3 * kRate;
  auto inL = make_signal(frames, 0);
  auto inR = make_signal(frames, 1);
  Engine engine;
  engine.set(HYDRA_DSP_PARAM_ACTIVE_HEADS, 7.0f);
  engine.set(HYDRA_DSP_PARAM_DELAY_MS, 300.0f);
  CHECK(hydra_dsp_commit(engine.handle) == 0);
  const size_t freeze_on = 44160;   // 1.0014 s
  const size_t freeze_off = 88192;  // 2.000 s
  const size_t arrival = freeze_off + static_cast<size_t>(0.300 * kRate);
  std::vector<float> y(frames), yR(frames);
  for (size_t start = 0; start < size_t(frames); start += 128) {
    const uint32_t n = static_cast<uint32_t>(std::min<size_t>(128, frames - start));
    if (start == freeze_on) { engine.set(HYDRA_DSP_PARAM_FREEZE, 1.0f); CHECK(hydra_dsp_commit(engine.handle) == 0); }
    if (start == freeze_off) { engine.set(HYDRA_DSP_PARAM_FREEZE, 0.0f); CHECK(hydra_dsp_commit(engine.handle) == 0); }
    engine.process(inL.data() + start, inR.data() + start, y.data() + start,
                   yR.data() + start, n);
  }
  const double natural = pre_step(y, arrival, static_cast<size_t>(0.05 * kRate));
  const double after = boundary_step(y, arrival, static_cast<size_t>(0.005 * kRate));
  std::fprintf(stderr, "freeze natural=%.9g arrival_5ms=%.9g\n", natural, after);
  CHECK(after <= std::max(1e-6, natural * 1.25));
  for (float v : y) CHECK(std::isfinite(v));
}

}  // namespace

int main() {
#if HYDRA_DSP_REFORM_F4_TRANSITIONS != 1
  // Qualification builds may define the production default globally; the
  // experiment contract only applies to the opt-in target.
  std::puts("F4 performance-transition contract: SKIP (built with F4=0)");
  return 0;
#else
  test_block_size_independence();
  test_mix_recovers_static_final_value();
  test_head_transition_is_below_natural_slope();
  test_commands_during_transition();
  test_sub_ramp_retargeting();
  test_freeze_release_is_bounded();
  std::puts("F4 performance-transition contract: PASS");
  return 0;
#endif
}
