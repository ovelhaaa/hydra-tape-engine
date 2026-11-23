#pragma once

#if defined(HYDRA_UI_BACKEND_ST7789)

#include "hydra_ui_types.h"
#include "driver/pulse_cnt.h"
#include <cstdint>
#include <cstddef>

namespace hydra::ui {

class EncoderPcntEsp32 {
public:
    bool begin(int pinA,
               int pinB,
               int pinSw,
               int countsPerDetent = 4,
               uint32_t debounceMs = 35,
               uint32_t longPressMs = 800);

    // Poll encoder delta and debounced switch state; returns number of events written
    size_t poll(uint64_t nowUs, UiEvent* outEvents, size_t maxEvents);

private:
    pcnt_unit_handle_t pcntUnit_ = nullptr;
    int pinSw_ = -1;
    int countsPerDetent_ = 4;
    uint32_t debounceUs_ = 35000;
    uint32_t longPressUs_ = 800000;

    int lastPcntCount_ = 0;
    int remainder_ = 0;

    // Button debounce & long-press state machine
    bool stablePressed_ = false;
    bool rawLast_ = false;
    uint64_t rawChangedUs_ = 0;
    uint64_t pressStartUs_ = 0;
    bool longFired_ = false;
};

} // namespace hydra::ui

#endif // HYDRA_UI_BACKEND_ST7789
