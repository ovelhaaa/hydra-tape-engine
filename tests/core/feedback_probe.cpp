// Offline long-run stage probe for Phase 6.4 feedback analysis.
//
// Runs the tail-growth scenarios from scripts/validate_bias_engine.py through
// the instrumented DSP build and captures a window of internal stage values.
// Recording a window near the start and near the end of a long run shows
// whether the pre-limiter record/feedback/magnetic state converges, which the
// post-limiter output alone cannot prove.
#include "hydra_dsp.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <array>

extern "C" {
void phase64_diag_enable(int on);
int phase64_diag_count();
const float* phase64_diag_data();
int phase64_diag_stride();
}

int main(int argc, char** argv) {
  double rate = 48000.0;
  double seconds = 60.0;
  double window_start = 55.0;
  double feedback = 95.0;
  double heads = 1.0;
  double reverse = 0.0;
  double freq = 440.0, amp = 0.1, input_seconds = 1.0;
  std::vector<std::array<double,3>> events;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto next = [&]() { return std::atof(argv[++i]); };
    if (a == "--rate") rate = next();
    else if (a == "--seconds") seconds = next();
    else if (a == "--window-start") window_start = next();
    else if (a == "--feedback") feedback = next();
    else if (a == "--heads") heads = next();
    else if (a == "--reverse") reverse = next();
    else if (a == "--freq") freq = next();
    else if (a == "--amp") amp = next();
    else if (a == "--input-seconds") input_seconds = next();
    else if (a == "--ev") { const std::string spec = argv[++i];
      const size_t c1 = spec.find(':');
      const size_t c2 = spec.find(':', c1 + 1);
      events.push_back({std::atof(spec.substr(0, c1).c_str()),
                        std::atof(spec.substr(c1 + 1, c2 - c1 - 1).c_str()),
                        std::atof(spec.substr(c2 + 1).c_str())}); }
  }

  const uint32_t block = 128;
  const uint32_t total = static_cast<uint32_t>(rate * seconds);
  const uint32_t window_frame = static_cast<uint32_t>(rate * window_start);

  hydra_dsp_handle* h = nullptr;
  if (hydra_dsp_create_v1_4(static_cast<float>(rate), 2000.0f, 8000.0f, &h) != 0) return 2;
  if (hydra_dsp_prepare(h, block, HYDRA_DSP_CHANNELS_STEREO) != 0) return 2;

  // Parameter map from scripts/validate_bias_engine.py.
  const int ids[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 11, 12, 13, 14, 15, 16, 19, 20, 23, 25, 30};
  const float vals[] = {0, 0, 0, 25, 0, 50, 50, 0, 0, 1, 125, 35, 100, 100, 1, 0, 100, 0, 0, 0};
  for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i) {
    hydra_dsp_set_parameter(h, static_cast<hydra_dsp_param_id>(ids[i]), vals[i]);
  }
  hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_FEEDBACK, static_cast<float>(feedback));
  hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_ACTIVE_HEADS, static_cast<float>(heads));
  hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_REVERSE, static_cast<float>(reverse));
  hydra_dsp_set_tempo_source(h, 0);
  hydra_dsp_transport_play(h);

  std::vector<float> inL(block, 0.0f), inR(block, 0.0f), outL(block), outR(block);
  uint32_t captured = 0;
  bool armed = false;
  for (uint32_t start = 0; start < total; start += block) {
    for (uint32_t i = 0; i < block; ++i) {
      const uint32_t f = start + i;
      float v = 0.0f;
      if (f < static_cast<uint32_t>(rate * input_seconds)) {
        const double t = static_cast<double>(f) / rate;
        v = static_cast<float>(amp * std::sin(2.0 * 3.14159265358979323846 * freq * t));
      }
      inL[i] = v;
      inR[i] = v;
    }
    for (const auto& ev : events) {
      const uint32_t ef = static_cast<uint32_t>(ev[0] * rate);
      if (ef >= start && ef < start + block) {
        hydra_dsp_set_parameter(h, static_cast<hydra_dsp_param_id>(static_cast<int>(ev[1])),
                                static_cast<float>(ev[2]));
      }
    }
    if (!armed && start >= window_frame) {
      phase64_diag_enable(1);
      armed = true;
    }
    hydra_dsp_process(h, inL.data(), inR.data(), outL.data(), outR.data(), block);
    if (phase64_diag_count() >= 8192) {
      phase64_diag_enable(0);
      captured = 1;
    }
  }
  hydra_dsp_destroy(h);
  if (!captured && phase64_diag_count() == 0) {
    std::cerr << "window never reached\n";
    return 3;
  }

  const int count = phase64_diag_count();
  const float* data = phase64_diag_data();
  const int stride = phase64_diag_stride();
  std::cout << "count " << count << " stride " << stride << " rate " << rate
            << " seconds " << seconds << " window_start " << window_start << "\n";
  for (int i = 0; i < count; ++i) {
    const float* r = data + static_cast<size_t>(i) * stride;
    std::cout << i;
    for (int s = 0; s < stride; ++s) {
      char buf[64];
      std::snprintf(buf, sizeof(buf), " %a", static_cast<double>(r[s]));
      std::cout << buf;
    }
    std::cout << "\n";
  }
  return 0;
}
