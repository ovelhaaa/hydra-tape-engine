#pragma once

namespace hydra::dsp::experimental {

// Phase 7.2A: alternate source for the existing feedback processors.
// The "delay head" signal is already the forward/reverse crossfade upstream.
// It contains neither the monitor's dry/wet gains nor the newly injected hiss,
// and it bypasses the monitor-only dropout/azimuth/colouration path.
// Important: Warp is handled by a separate feedback path and is not changed.
//
// This is a compile-time-only listening experiment, not a public DSP parameter.
// It must remain disabled for all production, Native/WASM and ESP32 builds until
// separate human sonic and physical acceptance is recorded.
inline float independentTapeFeedbackSource(float delayHeadSignal) {
  return delayHeadSignal;
}

} // namespace hydra::dsp::experimental
