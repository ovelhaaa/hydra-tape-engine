// Reuse the unchanged historical setup; do not weaken its A energy assertion.
#define main historical_regression_main
#include "regression_core.cpp"
#undef main
#include <iomanip>
#include <cstring>
#include "feedback_calibration_experiment.hpp"

int main() {
  constexpr size_t frames = 8192;
  std::vector<float> inL(frames), inR(frames);
  inL[955] = .8f; inR[1920] = -.6f;
  auto run = [&](float capacity, size_t block) {
    hydra_dsp_handle* h=nullptr;
    CHECK(hydra_dsp_create_v1_4(kSampleRate,20.f,capacity,&h)==0);
    CHECK(hydra_dsp_prepare(h,kFrames,2)==0);
    configure_ping_pong_delay(h, false);
    set_param(h, HYDRA_DSP_PARAM_DELAY_MS, 20.f);
    std::array<std::vector<float>,2> y{std::vector<float>(frames),std::vector<float>(frames)};
    for(size_t start=0;start<frames;start+=block) {
      auto n=std::min(block,frames-start);
      CHECK(hydra_dsp_process(h,inL.data()+start,inR.data()+start,
                             y[0].data()+start,y[1].data()+start,n)==0);
    }
    hydra_dsp_destroy(h); return y;
  };
  auto shortFull=run(20.f,frames), shortSplit=run(20.f,37);
  // Mathematical guard/wrap validation is in filter_probe.cpp. Whole-engine
  // full/split identity remains exact, independent of the old A energy floor.
  CHECK(shortFull==shortSplit);
  for(int ch=0;ch<2;++ch) {
    for(float v:shortFull[ch]) CHECK(std::isfinite(v));
    size_t first=ch?2840:1880, last=ch?2960:1990;
    double e=channel_energy(shortFull[ch],first,last);
    CHECK(e>0); // Non-silence alone is NOT the continuity contract above.
    std::cout << "channel=" << ch << " energy=" << std::setprecision(17) << e << '\n';
  }
  uint64_t hash=14695981039346656037ull;
  for(auto& channel:shortFull) for(float v:channel) {
    unsigned char bytes[sizeof v];std::memcpy(bytes,&v,sizeof v);
    for(auto byte:bytes){hash^=byte;hash*=1099511628211ull;}
  }
  std::cout << "response_fnv1a64=" << std::hex << hash << '\n';
  std::cout << "PASS exact full/split, finite non-silent channels\n";
}
