#include "hydra_dsp.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <vector>

namespace {
constexpr float kSampleRate = 48000.0f;
constexpr uint32_t kFrames = 48000;
constexpr uint32_t kBlock = 128;

hydra_dsp_handle* make_handle() {
  hydra_dsp_handle* h = nullptr;
  assert(hydra_dsp_create(kSampleRate, 2000.0f, &h) == 0);
  assert(h != nullptr);
  assert(hydra_dsp_prepare(h, kBlock, HYDRA_DSP_CHANNELS_STEREO) == 0);
  return h;
}

hydra_dsp_params base_params() {
  hydra_dsp_params p{};
  p.flutterDepth = 0.0f;
  p.wowDepth = 0.0f;
  p.dropoutSeverity = 0.0f;
  p.drive = 20.0f;
  p.noise = 0.0f;
  p.tapeSpeed = 50.0f;
  p.tapeAge = 20.0f;
  p.headBumpAmount = 0.0f;
  p.azimuthError = 0.0f;
  p.flutterRate = 6.0f;
  p.wowRate = 0.8f;
  p.delayActive = 0.0f;
  p.delayTimeMs = 30.0f;
  p.feedback = 0.0f;
  p.dryWet = 100.0f;
  p.delayWet = 100.0f;
  p.activeHeads = 4.0f;
  p.bpm = 120.0f;
  p.headsMusical = 0.0f;
  p.guitarFocus = 0.0f;
  p.tone = 80.0f;
  p.pingPong = 0.0f;
  p.freeze = 0.0f;
  p.reverse = 0.0f;
  p.reverseSmear = 0.0f;
  p.spring = 0.0f;
  p.springDecay = 0.0f;
  p.springDamping = 0.0f;
  p.springMix = 0.0f;
  return p;
}

void set_params(hydra_dsp_handle* h, const hydra_dsp_params& p) {
  assert(hydra_dsp_set_params(h, &p) == 0);
  assert(hydra_dsp_commit(h) == 0);
}

float rms(const std::vector<float>& v) {
  double sum = 0.0;
  for (float x : v) sum += double(x) * double(x);
  return static_cast<float>(std::sqrt(sum / std::max<size_t>(1, v.size())));
}

float rms_diff(const std::vector<float>& a, const std::vector<float>& b) {
  assert(a.size() == b.size());
  double sum = 0.0;
  for (size_t i = 0; i < a.size(); ++i) {
    const double d = double(a[i]) - double(b[i]);
    sum += d * d;
  }
  return static_cast<float>(std::sqrt(sum / std::max<size_t>(1, a.size())));
}

void render(hydra_dsp_handle* h,
            const std::vector<float>& inL,
            const std::vector<float>& inR,
            std::vector<float>* outL,
            std::vector<float>* outR) {
  outL->assign(inL.size(), 0.0f);
  outR->assign(inR.size(), 0.0f);
  for (size_t pos = 0; pos < inL.size(); pos += kBlock) {
    const uint32_t n = static_cast<uint32_t>(std::min<size_t>(kBlock, inL.size() - pos));
    assert(hydra_dsp_process(h, inL.data() + pos, inR.data() + pos, outL->data() + pos, outR->data() + pos, n) == 0);
  }
}

void test_noise_gain_and_stereo_decorrelation() {
  std::vector<float> zero(kFrames, 0.0f), outL, outR;

  hydra_dsp_handle* h0 = make_handle();
  auto p0 = base_params();
  p0.noise = 0.0f;
  set_params(h0, p0);
  render(h0, zero, zero, &outL, &outR);
  const float silentRms = 0.5f * (rms(outL) + rms(outR));
  assert(silentRms < 1.0e-5f);
  hydra_dsp_destroy(h0);

  hydra_dsp_handle* h1 = make_handle();
  auto p1 = base_params();
  p1.noise = 50.0f;
  set_params(h1, p1);
  render(h1, zero, zero, &outL, &outR);
  const float noiseRms = 0.5f * (rms(outL) + rms(outR));
  const float lrDiffRms = rms_diff(outL, outR);
  assert(noiseRms > 1.0e-3f);
  assert(noiseRms < 0.12f);
  assert(lrDiffRms > noiseRms * 0.25f);
  hydra_dsp_destroy(h1);
}

void test_wow_flutter_affect_delay_and_texture_paths() {
  std::vector<float> inL(kFrames), inR(kFrames), dryL, dryR, modL, modR;
  for (uint32_t i = 0; i < kFrames; ++i) {
    const float s = 0.35f * std::sin(2.0f * 3.14159265358979323846f * 440.0f * float(i) / kSampleRate);
    inL[i] = s;
    inR[i] = s;
  }

  auto verify = [&](float delayActive, float threshold) {
    hydra_dsp_handle* hDry = make_handle();
    auto pDry = base_params();
    pDry.delayActive = delayActive;
    set_params(hDry, pDry);
    render(hDry, inL, inR, &dryL, &dryR);
    hydra_dsp_destroy(hDry);

    hydra_dsp_handle* hMod = make_handle();
    auto pMod = base_params();
    pMod.delayActive = delayActive;
    pMod.flutterDepth = 45.0f;
    pMod.wowDepth = 35.0f;
    pMod.flutterRate = 8.0f;
    pMod.wowRate = 1.2f;
    set_params(hMod, pMod);
    render(hMod, inL, inR, &modL, &modR);
    hydra_dsp_destroy(hMod);

    const float diff = 0.5f * (rms_diff(dryL, modL) + rms_diff(dryR, modR));
    if (diff <= threshold) {
      std::cerr << "wow/flutter diff " << diff << " <= " << threshold
                << " (delayActive=" << delayActive << ")\n";
    }
    assert(diff > threshold);
  };

  verify(1.0f, 2.0e-4f);
  verify(0.0f, 1.0e-4f);
}
}  // namespace

int main() {
  test_noise_gain_and_stereo_decorrelation();
  test_wow_flutter_affect_delay_and_texture_paths();
  std::cout << "audible_controls: ok\n";
  return 0;
}
