#pragma once

#include <algorithm>
#include <cstdint>

#define IRAM_ATTR
#ifndef PI
#define PI 3.14159265358979323846f
#endif

template <typename T>
constexpr T constrain(T value, T lower, T upper) {
  return std::min(std::max(value, lower), upper);
}

// The firmware only uses micros() to decorrelate noise seeds.  A fixed value
// makes the desktop comparison deterministic rather than emulating wall time.
inline std::uint32_t micros() { return 0u; }

struct HydraDesktopSerial {
  template <typename T> void println(const T&) const {}
};
inline HydraDesktopSerial Serial;
