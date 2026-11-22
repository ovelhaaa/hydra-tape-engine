#include "feedback_routing_experiment.hpp"

#include <cmath>
#include <initializer_list>
#include <cstdio>
#include <cstdlib>

namespace {
void check(bool ok, const char* message) {
  if (!ok) {
    std::fprintf(stderr, "phase72 feedback routing FAIL: %s\n", message);
    std::exit(1);
  }
}
bool near(float a, float b) { return std::fabs(a - b) < 1e-6f; }
}

int main() {
  using hydra::dsp::experimental::independentTapeFeedbackSource;
  // Forward/reverse mixing happens upstream; the isolated source must not
  // crossfade with Reverse a second time.
  const float forward = 0.24f;
  const float reverse = -0.51f;
  for (int i = 0; i <= 1000; ++i) {
    const float blend = static_cast<float>(i) * 0.001f;
    const float mixedHead = forward + blend * (reverse - forward);
    const float routed = independentTapeFeedbackSource(mixedHead);
    check(near(routed, mixedHead), "source changed upstream read-head sample");
    check(std::isfinite(routed), "nonfinite source");
  }
  // Historically both output monitor gain and new hiss alter the feedback:
  // (texture * textureMix + delay * delayMix + hiss). Experimental routing
  // is independent of these quantities for identical read-head samples.
  for (float textureGain : {0.0f, 0.25f, 1.0f}) {
    for (float delayGain : {0.0f, 0.5f, 1.0f}) {
      for (float hiss : {-0.1f, 0.0f, 0.1f}) {
        const float monitored = 0.3f * textureGain + forward * delayGain + hiss;
        const float original = monitored;
        const float alternative = independentTapeFeedbackSource(forward);
        check(near(alternative, forward), "feedback unexpectedly uses monitoring controls");
        if (textureGain == 1.0f && delayGain == 0.0f && hiss == 0.0f) {
          check(!near(original, alternative), "legacy vs alternate paths not distinct");
        }
      }
    }
  }
  std::puts("Phase 7.2 routing experiment scalar contract PASS");
  return 0;
}
