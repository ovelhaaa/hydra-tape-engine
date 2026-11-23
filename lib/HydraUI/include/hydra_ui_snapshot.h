#pragma once

#include "hydra_ui_types.h"
#include "hydra_dsp.h"
#include <cstdint>

namespace hydra::ui {

struct UiSnapshot {
    float values[static_cast<size_t>(UiParamId::COUNT)]{};
    bool bypassed = false;
    float effectiveBpm = 120.0f;

    hydra_dsp_tap_tempo_state tapState{};
    hydra_dsp_transport_state transportState{};
    bool reverseClamped = false;
    float effectiveReverseMs = 0.0f;

    // Diagnostics (separated from performance controls)
    float dspLoadPct = 0.0f;
    uint32_t dspOverruns = 0;
    uint32_t freeInternalBytes = 0;
    uint32_t freePsramBytes = 0;
    uint16_t uiFps = 0;

    uint32_t generation = 0;
};

} // namespace hydra::ui
