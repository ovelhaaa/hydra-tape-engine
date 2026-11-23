#include "tape_core.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr float kSampleRate = 48000.0f;
constexpr uint32_t kBlockSize = 128;
constexpr float kTwoPi = 6.28318530717958647692f;

struct StereoBuffer {
  std::vector<float> left;
  std::vector<float> right;
};

StereoBuffer genPluckStimulus(float durationSec, bool singleHit) {
  const size_t frames = static_cast<size_t>(kSampleRate * durationSec);
  StereoBuffer buf{std::vector<float>(frames, 0.0f), std::vector<float>(frames, 0.0f)};
  struct Hit { float t; float f; float pan; };
  std::vector<Hit> hits;
  if (singleHit) {
    hits.push_back({0.05f, 440.0f, 0.5f});
  } else {
    hits.push_back({0.05f, 440.0f, 0.45f});
    hits.push_back({0.65f, 587.33f, 0.55f});
    hits.push_back({1.25f, 659.25f, 0.48f});
  }
  for (const auto& hit : hits) {
    const size_t start = static_cast<size_t>(hit.t * kSampleRate);
    const size_t len = static_cast<size_t>(0.14f * kSampleRate);
    for (size_t k = 0; k < len && start + k < frames; ++k) {
      const float attack = 1.0f - std::exp(-static_cast<float>(k) / 24.0f);
      const float decay = std::exp(-static_cast<float>(k) / (0.025f * kSampleRate));
      const float s = 0.60f * attack * decay * (
          std::sin((kTwoPi * hit.f * k) / kSampleRate) +
          0.45f * std::sin((kTwoPi * hit.f * 2.0f * k) / kSampleRate) +
          0.28f * std::sin((kTwoPi * hit.f * 3.0f * k) / kSampleRate) +
          0.16f * std::sin((kTwoPi * hit.f * 5.0f * k) / kSampleRate));
      buf.left[start + k] += s * (1.0f - hit.pan * 0.2f);
      buf.right[start + k] += s * (0.8f + hit.pan * 0.2f);
    }
  }
  return buf;
}

StereoBuffer genSustainedChords(float durationSec) {
  const size_t frames = static_cast<size_t>(kSampleRate * durationSec);
  StereoBuffer buf{std::vector<float>(frames, 0.0f), std::vector<float>(frames, 0.0f)};
  struct Chord { float t0; float t1; float freqs[4]; };
  const Chord chords[2] = {
      {0.05f, 1.15f, {220.0f, 277.18f, 329.63f, 415.30f}},
      {1.30f, 2.35f, {196.0f, 246.94f, 293.66f, 369.99f}},
  };
  for (const auto& ch : chords) {
    const size_t i0 = static_cast<size_t>(ch.t0 * kSampleRate);
    const size_t i1 = static_cast<size_t>(ch.t1 * kSampleRate);
    const float dur = ch.t1 - ch.t0;
    for (size_t i = i0; i < i1 && i < frames; ++i) {
      const float tRel = static_cast<float>(i - i0) / kSampleRate;
      const float env = std::min(1.0f, tRel / 0.04f) * std::min(1.0f, (dur - tRel) / 0.12f);
      float sL = 0.0f, sR = 0.0f;
      for (int idx = 0; idx < 4; ++idx) {
        const float f = ch.freqs[idx];
        const float tone = std::sin((kTwoPi * f * i) / kSampleRate) +
                           0.35f * std::sin((kTwoPi * f * 2.0f * i) / kSampleRate) +
                           0.15f * std::sin((kTwoPi * f * 3.0f * i) / kSampleRate);
        const float pan = 0.35f + (static_cast<float>(idx) / 3.0f) * 0.3f;
        sL += tone * (1.0f - pan) * 0.15f;
        sR += tone * pan * 0.15f;
      }
      buf.left[i] += sL * env;
      buf.right[i] += sR * env;
    }
  }
  return buf;
}

StereoBuffer genMixedAutomation(float durationSec) {
  auto p = genPluckStimulus(durationSec, false);
  auto c = genSustainedChords(durationSec);
  for (size_t i = 0; i < p.left.size(); ++i) {
    p.left[i] = 0.55f * p.left[i] + 0.55f * c.left[i];
    p.right[i] = 0.55f * p.right[i] + 0.55f * c.right[i];
  }
  return p;
}

hydra::dsp::TapeParams defaultParams() {
  hydra::dsp::TapeParams p{};
  p.flutterDepth = 10.0f;
  p.wowDepth = 8.0f;
  p.dropoutSeverity = 2.0f;
  p.drive = 35.0f;
  p.noise = 0.0f;
  p.tapeSpeed = 50.0f;
  p.tapeAge = 35.0f;
  p.headBumpAmount = 30.0f;
  p.azimuthError = 10.0f;
  p.flutterRate = 6.0f;
  p.wowRate = 0.8f;
  p.delayActive = true;
  p.delayTimeMs = 280.0f;
  p.feedback = 55.0f;
  p.dryWet = 55.0f;
  p.delayWet = 70.0f;
  p.bpm = 120.0f;
  p.tone = 50.0f;
  p.activeHeads = 4;
  return p;
}

uint64_t fnv1a64(const float* data, size_t count, uint64_t h = 14695981039346656037ULL) {
  const auto* bytes = reinterpret_cast<const uint8_t*>(data);
  const size_t len = count * sizeof(float);
  for (size_t i = 0; i < len; ++i) {
    h ^= bytes[i];
    h *= 1099511628211ULL;
  }
  return h;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "Usage: %s <output_dir>\n", argv[0]);
    return 1;
  }
  const std::string outDir = argv[1];

  const auto singlePluck = genPluckStimulus(4.0f, true);
  const auto multiPluck = genPluckStimulus(4.0f, false);
  const auto chords = genSustainedChords(4.0f);
  const auto mixed = genMixedAutomation(4.0f);

  struct ScenarioDef {
    const char* id;
    const StereoBuffer* stim;
    hydra::dsp::TapeParams params;
    bool automate;
  };

  std::vector<ScenarioDef> scenarios;
  {
    auto p = defaultParams();
    p.delayTimeMs = 320.0f; p.feedback = 55.0f; p.dryWet = 55.0f; p.delayWet = 75.0f; p.tapeAge = 35.0f; p.activeHeads = 4;
    scenarios.push_back({"01_single_head_moderate_fb", &singlePluck, p, false});
  }
  {
    auto p = defaultParams();
    p.delayTimeMs = 280.0f; p.feedback = 85.0f; p.dryWet = 60.0f; p.delayWet = 80.0f; p.tapeAge = 40.0f; p.drive = 55.0f; p.activeHeads = 4;
    scenarios.push_back({"02_single_head_high_fb", &singlePluck, p, false});
  }
  {
    auto p = defaultParams();
    p.delayTimeMs = 360.0f; p.feedback = 58.0f; p.dryWet = 55.0f; p.delayWet = 75.0f; p.tapeAge = 35.0f; p.activeHeads = 7;
    scenarios.push_back({"03_multi_head_three", &multiPluck, p, false});
  }
  {
    auto p = defaultParams();
    p.delayTimeMs = 340.0f; p.feedback = 60.0f; p.dryWet = 60.0f; p.delayWet = 75.0f; p.tapeAge = 90.0f; p.tone = 40.0f; p.drive = 50.0f; p.activeHeads = 4;
    scenarios.push_back({"04_high_tape_age", &chords, p, false});
  }
  {
    auto p = defaultParams();
    p.delayTimeMs = 300.0f; p.feedback = 62.0f; p.dryWet = 55.0f; p.delayWet = 75.0f; p.tapeAge = 30.0f; p.activeHeads = 4;
    scenarios.push_back({"05_guitar_pluck_transients", &multiPluck, p, false});
  }
  {
    auto p = defaultParams();
    p.delayTimeMs = 340.0f; p.feedback = 62.0f; p.dryWet = 55.0f; p.delayWet = 75.0f; p.tapeAge = 35.0f; p.activeHeads = 4;
    scenarios.push_back({"06_sustained_chords", &chords, p, false});
  }
  {
    auto p = defaultParams();
    p.delayTimeMs = 350.0f; p.feedback = 65.0f; p.dryWet = 60.0f; p.delayWet = 80.0f; p.tapeAge = 40.0f; p.activeHeads = 4;
    p.reverse = true; p.reverseMode = 1;
    scenarios.push_back({"07_reverse_feedback", &multiPluck, p, false});
  }
  {
    auto p = defaultParams();
    p.delayTimeMs = 300.0f; p.feedback = 60.0f; p.dryWet = 55.0f; p.delayWet = 75.0f; p.tapeAge = 35.0f; p.activeHeads = 7;
    p.pingPong = true;
    scenarios.push_back({"08_ping_pong", &multiPluck, p, false});
  }
  {
    auto p = defaultParams();
    p.delayTimeMs = 280.0f; p.feedback = 65.0f; p.dryWet = 60.0f; p.delayWet = 80.0f; p.noise = 30.0f; p.tapeAge = 40.0f; p.activeHeads = 4;
    scenarios.push_back({"09_silence_after_stimulus", &singlePluck, p, false});
  }
  {
    auto p = defaultParams();
    p.delayTimeMs = 300.0f; p.dryWet = 55.0f; p.delayWet = 75.0f; p.activeHeads = 4;
    scenarios.push_back({"10_control_automation", &mixed, p, true});
  }

  for (const auto& sc : scenarios) {
    hydra::dsp::TapeCore core(kSampleRate, 2000.0f, 8100.0f);
    if (!core.isValid()) {
      std::fprintf(stderr, "Failed to allocate TapeCore\n");
      return 1;
    }
    const size_t frames = sc.stim->left.size();
    std::vector<float> outL(frames), outR(frames);
    auto p = sc.params;
    core.updateParams(p);

    for (size_t i = 0; i < frames; i += kBlockSize) {
      const uint32_t n = static_cast<uint32_t>(std::min<size_t>(kBlockSize, frames - i));
      if (sc.automate) {
        const float t = static_cast<float>(i) / static_cast<float>(frames);
        p.tapeAge = 20.0f + 65.0f * t;
        p.feedback = 35.0f + 40.0f * t;
        p.tapeSpeed = 35.0f + 40.0f * (1.0f - t);
        core.updateParams(p);
      }
      core.processStereoBlock(sc.stim->left.data() + i, sc.stim->right.data() + i,
                              outL.data() + i, outR.data() + i, n);
    }

    const std::string path = outDir + "/" + sc.id + ".f32";
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
      std::fprintf(stderr, "Failed to open %s\n", path.c_str());
      return 1;
    }
    std::fwrite(outL.data(), sizeof(float), frames, f);
    std::fwrite(outR.data(), sizeof(float), frames, f);
    std::fclose(f);

    uint64_t h = fnv1a64(outL.data(), frames);
    h = fnv1a64(outR.data(), frames, h);
    std::printf("%s %016llx\n", sc.id, static_cast<unsigned long long>(h));
  }

  return 0;
}
