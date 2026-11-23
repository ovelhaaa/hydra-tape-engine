#pragma once

#include "hydra_ui_model.h"
#include "hydra_ui_catalog.h"
#include "hydra_ui_snapshot.h"
#include <cstdint>

namespace hydra::ui {

class UiRenderer {
public:
    static constexpr int16_t kWidth = 240;
    static constexpr int16_t kHeight = 240;

    // Industrial dark palette (RGB565)
    static constexpr uint16_t kColBg           = 0x0841; // #08080A
    static constexpr uint16_t kColSurface      = 0x18E3; // #181C1E
    static constexpr uint16_t kColSurfaceHi    = 0x2965; // #282C30
    static constexpr uint16_t kColBorder       = 0x39E7; // #383C3E
    static constexpr uint16_t kColTextPrimary  = 0xF7BE; // #F0F4F8
    static constexpr uint16_t kColTextMuted    = 0x8C71; // #888C8E
    static constexpr uint16_t kColOrange       = 0xFBC3; // #FF7A1A
    static constexpr uint16_t kColGreen        = 0x2EA6; // #2ED573
    static constexpr uint16_t kColRed          = 0xF986; // #FF3030
    static constexpr uint16_t kColYellow       = 0xFE60; // #FFCC00

    explicit UiRenderer(bool swapBytesForSpi = true);

    void render(uint16_t* fb,
                const UiModel& model,
                const UiCatalog& catalog,
                const UiSnapshot& snap,
                uint64_t nowUs);

private:
    bool swapBytes_;

    uint16_t encodeColor(uint16_t rgb565) const;
    void fillScreen(uint16_t* fb, uint16_t color) const;
    void fillRect(uint16_t* fb, int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) const;
    void drawRect(uint16_t* fb, int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color, int16_t thickness = 1) const;
    void drawHLine(uint16_t* fb, int16_t x, int16_t y, int16_t w, uint16_t color) const;
    void drawChar(uint16_t* fb, int16_t x, int16_t y, char c, uint16_t color, uint8_t scale) const;
    void drawText(uint16_t* fb, int16_t x, int16_t y, const char* text, uint16_t color, uint8_t scale) const;
    void drawTextCentered(uint16_t* fb, int16_t x, int16_t y, int16_t w, const char* text, uint16_t color, uint8_t scale) const;
    void drawProgressBar(uint16_t* fb, int16_t x, int16_t y, int16_t w, int16_t h, float norm, uint16_t fillCol, uint16_t bgCol) const;

    void drawHeader(uint16_t* fb, const char* left, const char* right, uint16_t rightCol) const;
    void drawFooter(uint16_t* fb, const char* text) const;
    void drawBanner(uint16_t* fb, const char* text, bool isAlert) const;

    void renderSplash(uint16_t* fb) const;
    void renderHome(uint16_t* fb, const UiModel& model, const UiCatalog& catalog, const UiSnapshot& snap) const;
    void renderMenu(uint16_t* fb, const UiModel& model) const;
    void renderParamList(uint16_t* fb, const UiModel& model, const UiCatalog& catalog, const UiSnapshot& snap) const;
    void renderParamEdit(uint16_t* fb, const UiModel& model, const UiCatalog& catalog, const UiSnapshot& snap) const;
    void renderTapTempo(uint16_t* fb, const UiSnapshot& snap) const;
    void renderPresets(uint16_t* fb, const UiModel& model) const;
};

} // namespace hydra::ui
