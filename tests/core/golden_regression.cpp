#include "hydra_dsp.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr float kSampleRate = 48000.0f;
constexpr float kMaxDelayMs = 2000.0f;
constexpr uint32_t kBlockSize = 32;

struct StereoBuffer {
  std::vector<float> left;
  std::vector<float> right;
};

StereoBuffer load_csv(const std::string& path) {
  std::ifstream in(path);
  if (!in.is_open()) {
    throw std::runtime_error("failed to open input fixture: " + path);
  }

  StereoBuffer buffer;
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) {
      continue;
    }
    std::stringstream ss(line);
    std::string l;
    std::string r;
    if (!std::getline(ss, l, ',') || !std::getline(ss, r, ',')) {
      throw std::runtime_error("invalid CSV line: " + line);
    }
    buffer.left.push_back(std::stof(l));
    buffer.right.push_back(std::stof(r));
  }

  if (buffer.left.size() != buffer.right.size()) {
    throw std::runtime_error("fixture channels size mismatch");
  }

  return buffer;
}

void write_csv(const std::string& path, const StereoBuffer& buffer) {
  std::ofstream out(path);
  if (!out.is_open()) {
    throw std::runtime_error("failed to open output path: " + path);
  }
  out << std::fixed << std::setprecision(9);
  for (size_t i = 0; i < buffer.left.size(); ++i) {
    out << buffer.left[i] << ',' << buffer.right[i] << '\n';
  }
}

hydra_dsp_handle* create_handle() {
  hydra_dsp_handle* h = nullptr;
  assert(hydra_dsp_create(kSampleRate, kMaxDelayMs, &h) == 0);
  assert(h != nullptr);
  assert(hydra_dsp_prepare(h, kBlockSize, HYDRA_DSP_CHANNELS_STEREO) == 0);
  return h;
}

void apply_base_params(hydra_dsp_handle* h) {
  hydra_dsp_params p{};
  p.flutterDepth = 0.0f;
  p.wowDepth = 0.0f;
  p.dropoutSeverity = 0.0f;
  p.drive = 0.0f;
  p.noise = 0.0f;
  p.tapeSpeed = 50.0f;
  p.tapeAge = 0.0f;
  p.headBumpAmount = 0.0f;
  p.azimuthError = 0.0f;
  p.flutterRate = 0.1f;
  p.wowRate = 0.1f;
  p.delayActive = 1.0f;
  p.delayTimeMs = 250.0f;
  p.feedback = 35.0f;
  p.dryWet = 50.0f;
  p.activeHeads = 7.0f;
  p.bpm = 120.0f;
  p.headsMusical = 0.0f;
  p.guitarFocus = 0.0f;
  p.tone = 80.0f;
  p.pingPong = 0.0f;
  p.freeze = 0.0f;
  p.reverse = 0.0f;
  p.reverseSmear = 0.0f;
  p.spring = 0.0f;
  p.springDecay = 60.0f;
  p.springDamping = 45.0f;
  p.springMix = 0.0f;
  assert(hydra_dsp_set_params(h, &p) == 0);
  assert(hydra_dsp_commit(h) == 0);
}

StereoBuffer process_delay_modes(hydra_dsp_handle* h, const StereoBuffer& input) {
  StereoBuffer out{std::vector<float>(input.left.size()), std::vector<float>(input.right.size())};
  for (size_t start = 0; start < input.left.size(); start += kBlockSize) {
    if (start == 128) {
      assert(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_HEADS_MUSICAL, 1.0f) == 0);
      assert(hydra_dsp_commit(h) == 0);
    }
    if (start == 256) {
      assert(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_REVERSE, 1.0f) == 0);
      assert(hydra_dsp_commit(h) == 0);
    }
    if (start == 384) {
      assert(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_REVERSE, 0.0f) == 0);
      assert(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_HEADS_MUSICAL, 0.0f) == 0);
      assert(hydra_dsp_commit(h) == 0);
    }

    const auto n = static_cast<uint32_t>(std::min<size_t>(kBlockSize, input.left.size() - start));
    assert(hydra_dsp_process(h,
                             input.left.data() + start,
                             input.right.data() + start,
                             out.left.data() + start,
                             out.right.data() + start,
                             n) == 0);
  }
  return out;
}

StereoBuffer process_reset_state(hydra_dsp_handle* h, const StereoBuffer& input) {
  StereoBuffer out{std::vector<float>(input.left.size() * 2), std::vector<float>(input.right.size() * 2)};

  for (size_t start = 0; start < input.left.size(); start += kBlockSize) {
    const auto n = static_cast<uint32_t>(std::min<size_t>(kBlockSize, input.left.size() - start));
    assert(hydra_dsp_process(h,
                             input.left.data() + start,
                             input.right.data() + start,
                             out.left.data() + start,
                             out.right.data() + start,
                             n) == 0);
  }

  hydra_dsp_reset(h);

  const size_t offset = input.left.size();
  for (size_t start = 0; start < input.left.size(); start += kBlockSize) {
    const auto n = static_cast<uint32_t>(std::min<size_t>(kBlockSize, input.left.size() - start));
    assert(hydra_dsp_process(h,
                             input.left.data() + start,
                             input.right.data() + start,
                             out.left.data() + offset + start,
                             out.right.data() + offset + start,
                             n) == 0);
  }

  return out;
}

StereoBuffer process_param_automation(hydra_dsp_handle* h, const StereoBuffer& input) {
  StereoBuffer out{std::vector<float>(input.left.size()), std::vector<float>(input.right.size())};
  for (size_t start = 0; start < input.left.size(); start += kBlockSize) {
    if (start == 64) {
      assert(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_DRY_WET, 30.0f) == 0);
      assert(hydra_dsp_commit(h) == 0);
    }
    if (start == 192) {
      assert(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_DRY_WET, 70.0f) == 0);
      assert(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_FEEDBACK, 55.0f) == 0);
      assert(hydra_dsp_commit(h) == 0);
    }
    if (start == 320) {
      assert(hydra_dsp_set_parameter(h, HYDRA_DSP_PARAM_DELAY_MS, 380.0f) == 0);
      assert(hydra_dsp_commit(h) == 0);
    }
    const auto n = static_cast<uint32_t>(std::min<size_t>(kBlockSize, input.left.size() - start));
    assert(hydra_dsp_process(h,
                             input.left.data() + start,
                             input.right.data() + start,
                             out.left.data() + start,
                             out.right.data() + start,
                             n) == 0);
  }
  return out;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 7) {
    std::cerr << "usage: " << argv[0] << " --scenario <name> --input <path> --output <path>\n";
    return 2;
  }

  std::string scenario;
  std::string inputPath;
  std::string outputPath;
  for (int i = 1; i < argc; i += 2) {
    const std::string key = argv[i];
    const std::string value = argv[i + 1];
    if (key == "--scenario") {
      scenario = value;
    } else if (key == "--input") {
      inputPath = value;
    } else if (key == "--output") {
      outputPath = value;
    }
  }

  if (scenario.empty() || inputPath.empty() || outputPath.empty()) {
    std::cerr << "missing arguments\n";
    return 2;
  }

  try {
    const StereoBuffer input = load_csv(inputPath);
    hydra_dsp_handle* h = create_handle();
    apply_base_params(h);

    StereoBuffer out;
    if (scenario == "delay_modes") {
      out = process_delay_modes(h, input);
    } else if (scenario == "reset_state") {
      out = process_reset_state(h, input);
    } else if (scenario == "param_automation") {
      out = process_param_automation(h, input);
    } else {
      std::cerr << "unknown scenario: " << scenario << '\n';
      hydra_dsp_destroy(h);
      return 2;
    }

    hydra_dsp_destroy(h);
    write_csv(outputPath, out);
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }
}
