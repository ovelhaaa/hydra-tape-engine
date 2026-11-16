#include "check.h"
#include "hydra_dsp.h"

#include <array>
#include <fstream>
#include <iostream>
#include <string_view>

namespace {
constexpr uint32_t kBlock = 128;
static_assert(sizeof(float) == 4, "listening runner uses float32 PCM");

struct Event {
  uint32_t frame;
  int parameter;
  float value;
};
}

int main(int argc, char** argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--version") {
    const auto version = hydra_dsp_get_api_version();
    std::cout << "TapeCore C API " << (version >> 16) << '.'
              << ((version >> 8) & 0xff) << '.' << (version & 0xff) << '\n';
    return 0;
  }
  if (argc != 4) {
    std::cerr << "usage: hydra_listen input.f32 automation.txt output.f32\n";
    return 2;
  }

  std::ifstream input(argv[1], std::ios::binary);
  std::ifstream automation(argv[2]);
  std::ofstream output(argv[3], std::ios::binary);
  CHECK(input && automation && output);

  hydra_dsp_handle* handle = nullptr;
  CHECK(hydra_dsp_create_v1_4(48000.0f, 2000.0f, 8100.0f, &handle) == 0);
  CHECK(hydra_dsp_prepare(handle, kBlock, HYDRA_DSP_CHANNELS_STEREO) == 0);

  std::array<float, kBlock * 2> interleaved{};
  std::array<float, kBlock> inL{}, inR{}, outL{}, outR{};
  Event event{};
  auto nextEvent = [&] {
    if (!(automation >> event.frame)) {
      CHECK(automation.eof());
      return false;
    }
    CHECK(automation >> event.parameter >> event.value);
    CHECK(event.parameter >= 0 && event.parameter < HYDRA_DSP_PARAM_COUNT);
    return true;
  };
  bool pending = nextEvent();
  uint32_t frame = 0;
  for (;;) {
    input.read(reinterpret_cast<char*>(interleaved.data()), sizeof(interleaved));
    if (input.gcount() == 0) break;
    CHECK(input.gcount() % (2 * sizeof(float)) == 0);
    const uint32_t count = input.gcount() / (2 * sizeof(float));
    while (pending && event.frame == frame) {
      CHECK(hydra_dsp_set_parameter(handle, static_cast<hydra_dsp_param_id>(event.parameter), event.value) == 0);
      pending = nextEvent();
    }
    CHECK(!pending || event.frame >= frame + count);
    CHECK(hydra_dsp_commit(handle) == 0);
    for (uint32_t i = 0; i < count; ++i) {
      inL[i] = interleaved[2 * i];
      inR[i] = interleaved[2 * i + 1];
    }
    CHECK(hydra_dsp_process(handle, inL.data(), inR.data(), outL.data(), outR.data(), count) == 0);
    for (uint32_t i = 0; i < count; ++i) {
      interleaved[2 * i] = outL[i];
      interleaved[2 * i + 1] = outR[i];
    }
    output.write(reinterpret_cast<const char*>(interleaved.data()), count * 2 * sizeof(float));
    CHECK(output);
    frame += count;
  }
  CHECK(input.eof() && frame > 0 && !pending);
  output.close();
  CHECK(output);
  hydra_dsp_destroy(handle);
}
