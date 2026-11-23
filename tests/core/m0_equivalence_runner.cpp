#include "tape_core.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

uint64_t fnv1a(const float* data, size_t count) {
  uint64_t h = 14695981039346656037ull;
  const auto* bytes = reinterpret_cast<const uint8_t*>(data);
  const size_t n = count * sizeof(float);
  for (size_t i = 0; i < n; ++i) {
    h ^= bytes[i];
    h *= 1099511628211ull;
  }
  return h;
}

}  // namespace

int main() {
  constexpr float kSampleRate = 48000.0f;
  constexpr uint32_t kBlock = 128;
  constexpr uint32_t kTotalFrames = 48000 * 4;

  hydra::dsp::TapeCore core(kSampleRate, 1600.0f, 3200.0f);
  if (!core.isValid()) {
    std::fprintf(stderr, "TapeCore invalid\n");
    return 1;
  }

  hydra::dsp::TapeParams p{};
  p.delayActive = true;
  p.delayTimeMs = 340.0f;
  p.feedback = 68.0f;
  p.delayWet = 55.0f;
  p.dryWet = 30.0f;
  p.wowRate = 0.85f;
  p.wowDepth = 22.0f;
  p.flutterRate = 6.2f;
  p.flutterDepth = 20.0f;
  p.tapeAge = 45.0f;
  p.activeHeads = 7;
  core.updateParams(p);
  core.reset();

  std::vector<float> inL(kBlock), inR(kBlock), outL(kBlock), outR(kBlock);
  std::vector<float> allL, allR;
  allL.reserve(kTotalFrames);
  allR.reserve(kTotalFrames);

  for (uint32_t offset = 0; offset < kTotalFrames; offset += kBlock) {
    for (uint32_t i = 0; i < kBlock; ++i) {
      const uint32_t n = offset + i;
      const float t = static_cast<float>(n) / kSampleRate;
      const float env = (n < 48000) ? std::exp(-3.5f * t) : 0.0f;
      const float s = env * (0.45f * std::sin(2.0f * 3.14159265f * 220.0f * t) +
                             0.20f * std::sin(2.0f * 3.14159265f * 440.0f * t));
      inL[i] = s;
      inR[i] = s * 0.9f;
    }
    core.processStereoBlock(inL.data(), inR.data(), outL.data(), outR.data(), kBlock);
    allL.insert(allL.end(), outL.begin(), outL.end());
    allR.insert(allR.end(), outR.begin(), outR.end());
  }

  const uint64_t hashL = fnv1a(allL.data(), allL.size());
  const uint64_t hashR = fnv1a(allR.data(), allR.size());
  std::printf("hashL=%016llx hashR=%016llx\n",
              static_cast<unsigned long long>(hashL),
              static_cast<unsigned long long>(hashR));
  return 0;
}
