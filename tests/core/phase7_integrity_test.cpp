#include "tape_integrity.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace {
void check(bool condition, const char* message) {
  if (!condition) {
    std::fprintf(stderr, "phase7 integrity FAIL: %s\n", message);
    std::exit(1);
  }
}
bool near(double a, double b, double tolerance) {
  return std::fabs(a - b) <= tolerance;
}

// RBJ high-shelf coefficient calculation as configured by TapeCore's
// BiquadFilter::setHighShelf. Pole test covers the exact historical cutoff
// request that exceeded Nyquist, plus a dense parameter-domain sweep.
double largestHighShelfPole(float sampleRate, float requestedFrequency,
                            float q, float gainDb) {
  const double frequency = hydra::dsp::detail::boundedBiquadFrequency(
      sampleRate, requestedFrequency);
  const double Q = hydra::dsp::detail::boundedBiquadQ(q);
  const double A = std::pow(10.0, gainDb / 40.0);
  const double omega = 2.0 * 3.14159265358979323846 * frequency / sampleRate;
  const double cosine = std::cos(omega);
  const double sine = std::sin(omega);
  const double alpha = sine / (2.0 * Q);
  const double ap1 = A + 1.0, am1 = A - 1.0;
  const double tw = 2.0 * std::sqrt(A) * alpha;
  const double a0 = ap1 - am1 * cosine + tw;
  const double a1 = 2.0 * (am1 - ap1 * cosine) / a0;
  const double a2 = (ap1 - am1 * cosine - tw) / a0;
  check(std::isfinite(a0) && std::isfinite(a1) && std::isfinite(a2),
        "high-shelf coefficients must be finite");
  const double discriminant = a1 * a1 - 4.0 * a2;
  if (discriminant < 0.0) {
    check(a2 >= 0.0, "complex pole radius requires nonnegative a2");
    return std::sqrt(a2);
  }
  const double root1 = (-a1 + std::sqrt(discriminant)) / 2.0;
  const double root2 = (-a1 - std::sqrt(discriminant)) / 2.0;
  return std::max(std::fabs(root1), std::fabs(root2));
}

void testBiquadBoundsAndPoles() {
  using hydra::dsp::detail::boundedBiquadFrequency;
  for (float sampleRate : {44100.0f, 48000.0f, 96000.0f}) {
    check(near(boundedBiquadFrequency(sampleRate, 32000.0f),
               std::min(32000.0f, 0.45f * sampleRate), 1e-3),
          "high shelf must remain below Nyquist");
    check(boundedBiquadFrequency(sampleRate, -50.0f) >= 1.0f,
          "low frequency must be bounded");
    check(std::isfinite(boundedBiquadFrequency(
              sampleRate, std::numeric_limits<float>::quiet_NaN())),
          "NaN cutoff must be sanitized");
    for (int speed = 0; speed <= 100; speed += 5) {
      for (int age = 0; age <= 100; age += 5) {
        for (int tone = 0; tone <= 100; tone += 5) {
          const double ageNorm = age * 0.01;
          const double toneFactor = (tone * 0.01 - 0.5) * 2.0;
          double ageFactor = 1.0 - ageNorm * 0.90;
          if (toneFactor > 0.0) {
            ageFactor += toneFactor * 0.5;
            if (ageFactor > 1.0) ageFactor = 1.0;
          } else {
            ageFactor *= 1.0 + toneFactor * 0.5;
          }
          const float cut = static_cast<float>(std::max(
              400.0, (6000.0 + speed * 100.0) * ageFactor));
          const double pole = largestHighShelfPole(
              sampleRate, 2.0f * cut, 0.5f, -12.0f);
          check(std::isfinite(pole) && pole < 1.0,
                "playback shelving pole must be inside unit circle");
        }
      }
    }
  }
}

void testFeedbackCompressor() {
  using hydra::dsp::detail::continuousFeedbackSoftKnee;
  // The old branch at +0.7 jumped roughly 0.2 units: new knee must
  // agree with the upper hard-compression branch at the same point.
  const double upper = 0.6 + (0.7 - 0.6) / 1.5;
  check(near(continuousFeedbackSoftKnee(0.7f), upper, 2e-7),
        "upper knee value must match compressed region");
  check(near(continuousFeedbackSoftKnee(0.5f), 0.5, 1e-7),
        "lower knee value must match unity region");
  check(near(continuousFeedbackSoftKnee(-0.7f), -upper, 2e-7),
        "odd-symmetric negative branch");

  float previous = continuousFeedbackSoftKnee(-4.0f);
  for (int i = 1; i <= 80000; ++i) {
    const float x = -4.0f + i * 0.0001f;
    const float y = continuousFeedbackSoftKnee(x);
    check(std::isfinite(y), "compressor nonfinite");
    check(y >= previous - 1e-6f, "compressor not monotonic");
    check(near(y, -continuousFeedbackSoftKnee(-x), 1e-6),
          "compressor must be odd");
    previous = y;
  }

  const float h = 0.0001f;
  const auto slope = [](float a, float b) {
    return (hydra::dsp::detail::continuousFeedbackSoftKnee(b) -
            hydra::dsp::detail::continuousFeedbackSoftKnee(a)) / (b - a);
  };
  check(std::fabs(slope(0.5f-h, 0.5f) -
                  slope(0.5f, 0.5f+h)) < 0.002,
        "first derivative continuous at lower knee");
  check(std::fabs(slope(0.7f-h, 0.7f) -
                  slope(0.7f, 0.7f+h)) < 0.002,
        "first derivative continuous at upper knee");
}

} // namespace

int main() {
  testBiquadBoundsAndPoles();
  testFeedbackCompressor();
  std::puts("Phase7.1 biquad and feedback knee integrity PASS");
  return 0;
}
