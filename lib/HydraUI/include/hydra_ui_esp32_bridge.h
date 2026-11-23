#pragma once

#if defined(HYDRA_UI_BACKEND_ST7789)

#include "hydra_ui_pins.h"
#include "hydra_ui_catalog.h"
#include "hydra_ui_snapshot.h"
#include "hydra_ui_model.h"
#include "hydra_ui_renderer.h"
#include "st7789_driver_esp32.h"
#include "encoder_pcnt_esp32.h"
#include <cstdint>

namespace hydra::ui {

class HydraUiEsp32Bridge {
public:
    HydraUiEsp32Bridge();
    ~HydraUiEsp32Bridge();

    bool begin();
    void tick();

    // Diagnostics
    uint32_t lastFrameRenderUs() const { return lastRenderUs_; }
    uint32_t lastFrameFlushUs() const { return lastFlushUs_; }
    uint16_t currentFps() const { return currentFps_; }

private:
    UiCatalog catalog_;
    UiModel model_;
    UiRenderer renderer_;
    ST7789DriverEsp32 display_;
    EncoderPcntEsp32 encoder_;

    UiSnapshot snapshot_{};
    bool dirty_ = true;
    uint64_t bootUs_ = 0;
    uint64_t lastDrawUs_ = 0;
    uint64_t fpsWindowStartUs_ = 0;
    uint16_t fpsFrameCount_ = 0;
    uint16_t currentFps_ = 0;
    uint32_t lastRenderUs_ = 0;
    uint32_t lastFlushUs_ = 0;

    void captureSnapshot(uint64_t nowUs);
    void executeCommand(const UiCommand& cmd);
};

} // namespace hydra::ui

#endif // HYDRA_UI_BACKEND_ST7789
