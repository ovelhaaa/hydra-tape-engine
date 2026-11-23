#include "hydra_ui_renderer.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace hydra::ui {

namespace {

// Compact 5x7 ASCII font (characters 32..126, 5 columns per char, LSB = top row)
static const uint8_t kFont5x7[95][5] = {
    {0x00,0x00,0x00,0x00,0x00}, // ' '
    {0x00,0x00,0x5F,0x00,0x00}, // '!'
    {0x00,0x07,0x00,0x07,0x00}, // '"'
    {0x14,0x7F,0x14,0x7F,0x14}, // '#'
    {0x24,0x2A,0x7F,0x2A,0x12}, // '$'
    {0x23,0x13,0x08,0x64,0x62}, // '%'
    {0x36,0x49,0x55,0x22,0x50}, // '&'
    {0x00,0x05,0x03,0x00,0x00}, // '''
    {0x00,0x1C,0x22,0x41,0x00}, // '('
    {0x00,0x41,0x22,0x1C,0x00}, // ')'
    {0x14,0x08,0x3E,0x08,0x14}, // '*'
    {0x08,0x08,0x3E,0x08,0x08}, // '+'
    {0x00,0x50,0x30,0x00,0x00}, // ','
    {0x08,0x08,0x08,0x08,0x08}, // '-'
    {0x00,0x60,0x60,0x00,0x00}, // '.'
    {0x20,0x10,0x08,0x04,0x02}, // '/'
    {0x3E,0x51,0x49,0x45,0x3E}, // '0'
    {0x00,0x42,0x7F,0x40,0x00}, // '1'
    {0x42,0x61,0x51,0x49,0x46}, // '2'
    {0x21,0x41,0x45,0x4B,0x31}, // '3'
    {0x18,0x14,0x12,0x7F,0x10}, // '4'
    {0x27,0x45,0x45,0x45,0x39}, // '5'
    {0x3C,0x4A,0x49,0x49,0x30}, // '6'
    {0x01,0x71,0x09,0x05,0x03}, // '7'
    {0x36,0x49,0x49,0x49,0x36}, // '8'
    {0x06,0x49,0x49,0x29,0x1E}, // '9'
    {0x00,0x36,0x36,0x00,0x00}, // ':'
    {0x00,0x56,0x36,0x00,0x00}, // ';'
    {0x08,0x14,0x22,0x41,0x00}, // '<'
    {0x14,0x14,0x14,0x14,0x14}, // '='
    {0x00,0x41,0x22,0x14,0x08}, // '>'
    {0x02,0x01,0x51,0x09,0x06}, // '?'
    {0x32,0x49,0x79,0x41,0x3E}, // '@'
    {0x7E,0x11,0x11,0x11,0x7E}, // 'A'
    {0x7F,0x49,0x49,0x49,0x36}, // 'B'
    {0x3E,0x41,0x41,0x41,0x22}, // 'C'
    {0x7F,0x41,0x41,0x22,0x1C}, // 'D'
    {0x7F,0x49,0x49,0x49,0x41}, // 'E'
    {0x7F,0x09,0x09,0x09,0x01}, // 'F'
    {0x3E,0x41,0x49,0x49,0x7A}, // 'G'
    {0x7F,0x08,0x08,0x08,0x7F}, // 'H'
    {0x00,0x41,0x7F,0x41,0x00}, // 'I'
    {0x20,0x40,0x41,0x3F,0x01}, // 'J'
    {0x7F,0x08,0x14,0x22,0x41}, // 'K'
    {0x7F,0x40,0x40,0x40,0x40}, // 'L'
    {0x7F,0x02,0x0C,0x02,0x7F}, // 'M'
    {0x7F,0x04,0x08,0x10,0x7F}, // 'N'
    {0x3E,0x41,0x41,0x41,0x3E}, // 'O'
    {0x7F,0x09,0x09,0x09,0x06}, // 'P'
    {0x3E,0x41,0x51,0x21,0x5E}, // 'Q'
    {0x7F,0x09,0x19,0x29,0x46}, // 'R'
    {0x46,0x49,0x49,0x49,0x31}, // 'S'
    {0x01,0x01,0x7F,0x01,0x01}, // 'T'
    {0x3F,0x40,0x40,0x40,0x3F}, // 'U'
    {0x1F,0x20,0x40,0x20,0x1F}, // 'V'
    {0x3F,0x40,0x38,0x40,0x3F}, // 'W'
    {0x63,0x14,0x08,0x14,0x63}, // 'X'
    {0x07,0x08,0x70,0x08,0x07}, // 'Y'
    {0x61,0x51,0x49,0x45,0x43}, // 'Z'
    {0x00,0x7F,0x41,0x41,0x00}, // '['
    {0x02,0x04,0x08,0x10,0x20}, // '\'
    {0x00,0x41,0x41,0x7F,0x00}, // ']'
    {0x04,0x02,0x01,0x02,0x04}, // '^'
    {0x40,0x40,0x40,0x40,0x40}, // '_'
    {0x00,0x01,0x02,0x04,0x00}, // '`'
    {0x20,0x54,0x54,0x54,0x78}, // 'a'
    {0x7F,0x48,0x44,0x44,0x38}, // 'b'
    {0x38,0x44,0x44,0x44,0x20}, // 'c'
    {0x38,0x44,0x44,0x48,0x7F}, // 'd'
    {0x38,0x54,0x54,0x54,0x18}, // 'e'
    {0x08,0x7E,0x09,0x01,0x02}, // 'f'
    {0x0C,0x52,0x52,0x52,0x3E}, // 'g'
    {0x7F,0x08,0x04,0x04,0x78}, // 'h'
    {0x00,0x44,0x7D,0x40,0x00}, // 'i'
    {0x20,0x40,0x44,0x3D,0x00}, // 'j'
    {0x7F,0x10,0x28,0x44,0x00}, // 'k'
    {0x00,0x41,0x7F,0x40,0x00}, // 'l'
    {0x7C,0x04,0x18,0x04,0x78}, // 'm'
    {0x7C,0x08,0x04,0x04,0x78}, // 'n'
    {0x38,0x44,0x44,0x44,0x38}, // 'o'
    {0x7C,0x14,0x14,0x14,0x08}, // 'p'
    {0x08,0x14,0x14,0x18,0x7C}, // 'q'
    {0x7C,0x08,0x04,0x04,0x08}, // 'r'
    {0x48,0x54,0x54,0x54,0x20}, // 's'
    {0x04,0x3F,0x44,0x40,0x20}, // 't'
    {0x3C,0x40,0x40,0x20,0x7C}, // 'u'
    {0x1C,0x20,0x40,0x20,0x1C}, // 'v'
    {0x3C,0x40,0x30,0x40,0x3C}, // 'w'
    {0x44,0x28,0x10,0x28,0x44}, // 'x'
    {0x0C,0x50,0x50,0x50,0x3C}, // 'y'
    {0x44,0x64,0x54,0x4C,0x44}, // 'z'
    {0x00,0x08,0x36,0x41,0x00}, // '{'
    {0x00,0x00,0x7F,0x00,0x00}, // '|'
    {0x00,0x41,0x36,0x08,0x00}, // '}'
    {0x10,0x08,0x08,0x10,0x08}, // '~'
};

} // namespace

UiRenderer::UiRenderer(bool swapBytesForSpi) : swapBytes_(swapBytesForSpi) {}

uint16_t UiRenderer::encodeColor(uint16_t rgb565) const {
    if (!swapBytes_) return rgb565;
    return static_cast<uint16_t>((rgb565 >> 8) | (rgb565 << 8));
}

void UiRenderer::fillScreen(uint16_t* fb, uint16_t color) const {
    uint16_t c = encodeColor(color);
    for (int i = 0; i < kWidth * kHeight; ++i) fb[i] = c;
}

void UiRenderer::fillRect(uint16_t* fb, int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) const {
    if (w <= 0 || h <= 0) return;
    int16_t x1 = std::max<int16_t>(0, x);
    int16_t y1 = std::max<int16_t>(0, y);
    int16_t x2 = std::min<int16_t>(kWidth, x + w);
    int16_t y2 = std::min<int16_t>(kHeight, y + h);
    if (x1 >= x2 || y1 >= y2) return;
    uint16_t c = encodeColor(color);
    for (int16_t iy = y1; iy < y2; ++iy) {
        uint16_t* row = &fb[iy * kWidth];
        for (int16_t ix = x1; ix < x2; ++ix) row[ix] = c;
    }
}

void UiRenderer::drawHLine(uint16_t* fb, int16_t x, int16_t y, int16_t w, uint16_t color) const {
    fillRect(fb, x, y, w, 1, color);
}

void UiRenderer::drawRect(uint16_t* fb, int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color, int16_t t) const {
    fillRect(fb, x, y, w, t, color);
    fillRect(fb, x, y + h - t, w, t, color);
    fillRect(fb, x, y, t, h, color);
    fillRect(fb, x + w - t, y, t, h, color);
}

void UiRenderer::drawChar(uint16_t* fb, int16_t x, int16_t y, char c, uint16_t color, uint8_t scale) const {
    if (c < 32 || c > 126) c = '?';
    const uint8_t* glyph = kFont5x7[c - 32];
    uint16_t enc = encodeColor(color);
    for (int col = 0; col < 5; ++col) {
        uint8_t bits = glyph[col];
        for (int row = 0; row < 7; ++row) {
            if (bits & (1u << row)) {
                if (scale == 1) {
                    int16_t px = x + col;
                    int16_t py = y + row;
                    if (px >= 0 && px < kWidth && py >= 0 && py < kHeight) {
                        fb[py * kWidth + px] = enc;
                    }
                } else {
                    fillRect(fb, x + col * scale, y + row * scale, scale, scale, color);
                }
            }
        }
    }
}

void UiRenderer::drawText(uint16_t* fb, int16_t x, int16_t y, const char* text, uint16_t color, uint8_t scale) const {
    if (!text) return;
    int16_t cx = x;
    int16_t adv = 6 * scale;
    while (*text) {
        if (cx + 5 * scale > kWidth) break;
        drawChar(fb, cx, y, *text, color, scale);
        cx += adv;
        ++text;
    }
}

void UiRenderer::drawTextCentered(uint16_t* fb, int16_t x, int16_t y, int16_t w, const char* text, uint16_t color, uint8_t scale) const {
    if (!text) return;
    int len = static_cast<int>(std::strlen(text));
    if (len == 0) return;
    int16_t tw = len * 6 * scale - scale;
    int16_t sx = x + (w - tw) / 2;
    drawText(fb, sx, y, text, color, scale);
}

void UiRenderer::drawProgressBar(uint16_t* fb, int16_t x, int16_t y, int16_t w, int16_t h, float norm, uint16_t fillCol, uint16_t bgCol) const {
    float n = std::max(0.0f, std::min(1.0f, norm));
    fillRect(fb, x, y, w, h, bgCol);
    int16_t fw = static_cast<int16_t>(n * w);
    if (fw > 0) {
        fillRect(fb, x, y, fw, h, fillCol);
    }
}

void UiRenderer::drawHeader(uint16_t* fb, const char* left, const char* right, uint16_t rightCol) const {
    fillRect(fb, 0, 0, kWidth, 26, kColSurface);
    drawHLine(fb, 0, 26, kWidth, kColBorder);
    drawText(fb, 8, 6, left, kColOrange, 2);
    if (right && right[0]) {
        int len = static_cast<int>(std::strlen(right));
        int16_t rx = kWidth - 8 - len * 6;
        drawText(fb, rx, 9, right, rightCol, 1);
    }
}

void UiRenderer::drawFooter(uint16_t* fb, const char* text) const {
    fillRect(fb, 0, 218, kWidth, 22, kColSurface);
    drawHLine(fb, 0, 218, kWidth, kColBorder);
    drawTextCentered(fb, 0, 225, kWidth, text, kColTextMuted, 1);
}

void UiRenderer::drawBanner(uint16_t* fb, const char* text, bool isAlert) const {
    uint16_t bg = isAlert ? kColRed : kColOrange;
    fillRect(fb, 16, 98, 208, 40, bg);
    drawRect(fb, 16, 98, 208, 40, kColTextPrimary, 2);
    drawTextCentered(fb, 16, 111, 208, text, kColBg, 2);
}

void UiRenderer::render(uint16_t* fb,
                        const UiModel& model,
                        const UiCatalog& catalog,
                        const UiSnapshot& snap,
                        uint64_t nowUs) {
    if (!fb) return;

    switch (model.screen()) {
        case UiScreen::SPLASH:     renderSplash(fb); break;
        case UiScreen::HOME:       renderHome(fb, model, catalog, snap); break;
        case UiScreen::MENU:       renderMenu(fb, model); break;
        case UiScreen::PARAM_LIST: renderParamList(fb, model, catalog, snap); break;
        case UiScreen::PARAM_EDIT: renderParamEdit(fb, model, catalog, snap); break;
        case UiScreen::TAP_TEMPO:  renderTapTempo(fb, snap); break;
        case UiScreen::PRESETS:    renderPresets(fb, model); break;
    }

    if (model.hasStatusBanner(nowUs)) {
        bool alert = snap.bypassed && (std::strstr(model.statusBanner(), "BYPASS") != nullptr);
        drawBanner(fb, model.statusBanner(), alert);
    }
}

void UiRenderer::renderSplash(uint16_t* fb) const {
    fillScreen(fb, kColBg);
    drawRect(fb, 10, 10, 220, 220, kColOrange, 2);
    drawTextCentered(fb, 0, 64, kWidth, "HYDRA", kColOrange, 4);
    drawTextCentered(fb, 0, 108, kWidth, "TAPE ENGINE", kColTextPrimary, 2);
    drawHLine(fb, 40, 136, 160, kColBorder);
    drawTextCentered(fb, 0, 154, kWidth, "ST7789 240x240", kColTextMuted, 1);
    drawTextCentered(fb, 0, 174, kWidth, "INICIALIZANDO...", kColGreen, 1);
}

void UiRenderer::renderHome(uint16_t* fb,
                            const UiModel& model,
                            const UiCatalog& catalog,
                            const UiSnapshot& snap) const {
    fillScreen(fb, kColBg);

    // Top bar: Mode + EFFECT / BYPASS
    SemanticModeId mode = UiModel::deriveActiveMode(snap);
    fillRect(fb, 0, 0, kWidth, 28, kColSurface);
    drawHLine(fb, 0, 28, kWidth, kColBorder);

    fillRect(fb, 6, 4, 114, 20, kColSurfaceHi);
    drawText(fb, 12, 7, UiCatalog::semanticModeShort(mode), kColOrange, 2);

    const char* stateStr = snap.bypassed ? "SW BYP" : "EFFECT";
    uint16_t stateCol = snap.bypassed ? kColRed : kColGreen;
    fillRect(fb, 146, 4, 88, 20, kColBg);
    drawRect(fb, 146, 4, 88, 20, stateCol, 1);
    drawTextCentered(fb, 146, 7, 88, stateStr, stateCol, 2);

    // TIME card
    bool timeFocus = (model.homeFocus() == HomeFocus::TIME);
    bool timeEdit = timeFocus && model.isHomeEditing();
    fillRect(fb, 8, 36, 224, 80, kColSurface);
    drawRect(fb, 8, 36, 224, 80,
             timeFocus ? kColOrange : kColBorder,
             timeEdit ? 3 : (timeFocus ? 2 : 1));

    drawText(fb, 16, 43, timeEdit ? "TIME [EDIT]" : "TIME", timeFocus ? kColOrange : kColTextMuted, 1);

    float delayMs = snap.values[static_cast<size_t>(UiParamId::DELAY_TIME_MS)];
    bool bpmUnit = snap.values[static_cast<size_t>(UiParamId::DELAY_UNIT_BPM)] > 0.5f;
    char timeBuf[24];
    if (bpmUnit && delayMs > 1.0f) {
        std::snprintf(timeBuf, sizeof(timeBuf), "%.0f", 60000.0f / delayMs);
    } else {
        std::snprintf(timeBuf, sizeof(timeBuf), "%.0f", delayMs);
    }
    drawText(fb, 18, 60, timeBuf, kColTextPrimary, 4);
    drawText(fb, 178, 76, bpmUnit ? "bpm" : "ms", kColTextMuted, 2);

    // FB & MIX cards
    auto drawMiniCard = [&](int16_t x, const char* lbl, HomeFocus f, float val) {
        bool foc = (model.homeFocus() == f);
        bool edt = foc && model.isHomeEditing();
        fillRect(fb, x, 124, 108, 62, kColSurface);
        drawRect(fb, x, 124, 108, 62, foc ? kColOrange : kColBorder, edt ? 3 : (foc ? 2 : 1));
        drawText(fb, x + 8, 130, edt ? "[EDIT]" : lbl, foc ? kColOrange : kColTextMuted, 1);
        char vbuf[16];
        std::snprintf(vbuf, sizeof(vbuf), "%.0f%%", val);
        drawText(fb, x + 8, 144, vbuf, kColTextPrimary, 2);
        drawProgressBar(fb, x + 8, 172, 92, 6, val / 100.0f, foc ? kColOrange : kColGreen, kColBg);
    };

    drawMiniCard(8, "FB", HomeFocus::FB, snap.values[static_cast<size_t>(UiParamId::FEEDBACK)]);
    drawMiniCard(124, "MIX", HomeFocus::MIX, snap.values[static_cast<size_t>(UiParamId::DRY_WET)]);

    // Footer: BPM + MENU button + tiny DSP load
    fillRect(fb, 0, 194, kWidth, 46, kColSurface);
    drawHLine(fb, 0, 194, kWidth, kColBorder);

    char bpmBuf[20];
    std::snprintf(bpmBuf, sizeof(bpmBuf), "%.0f", snap.effectiveBpm);
    drawText(fb, 10, 200, bpmBuf, kColTextPrimary, 3);
    drawText(fb, 10, 226, "BPM", kColTextMuted, 1);

    char diagBuf[24];
    std::snprintf(diagBuf, sizeof(diagBuf), "DSP %.0f%%", snap.dspLoadPct);
    drawText(fb, 68, 226, diagBuf, kColTextMuted, 1);

    bool menuFocus = (model.homeFocus() == HomeFocus::MENU);
    fillRect(fb, 144, 200, 88, 34, menuFocus ? kColOrange : kColSurfaceHi);
    drawRect(fb, 144, 200, 88, 34, menuFocus ? kColTextPrimary : kColBorder, 1);
    drawTextCentered(fb, 144, 210, 88, "MENU >", menuFocus ? kColBg : kColTextPrimary, 2);
    (void)catalog;
}

void UiRenderer::renderMenu(uint16_t* fb, const UiModel& model) const {
    fillScreen(fb, kColBg);

    char rightBuf[16];
    std::snprintf(rightBuf, sizeof(rightBuf), "%u/%u",
                  static_cast<unsigned>(model.menuCursor() + 1),
                  static_cast<unsigned>(UiGroupId::COUNT));
    drawHeader(fb, "MENU", rightBuf, kColTextMuted);

    constexpr int kRows = 5;
    int cursor = static_cast<int>(model.menuCursor());
    int total = static_cast<int>(UiGroupId::COUNT);
    int scroll = std::max(0, std::min(cursor - 2, total - kRows));

    for (int r = 0; r < kRows; ++r) {
        int idx = scroll + r;
        if (idx >= total) break;
        int16_t y = 32 + r * 36;
        bool sel = (idx == cursor);
        fillRect(fb, 8, y, 224, 32, sel ? kColOrange : kColSurface);
        drawRect(fb, 8, y, 224, 32, sel ? kColTextPrimary : kColBorder, 1);
        drawText(fb, 16, y + 8, sel ? ">" : " ", sel ? kColBg : kColOrange, 2);
        drawText(fb, 34, y + 8, UiCatalog::groupName(static_cast<UiGroupId>(idx)),
                 sel ? kColBg : kColTextPrimary, 2);
    }

    drawFooter(fb, "CLICK:ABRIR   HOLD:HOME");
}

void UiRenderer::renderParamList(uint16_t* fb,
                                 const UiModel& model,
                                 const UiCatalog& catalog,
                                 const UiSnapshot& snap) const {
    fillScreen(fb, kColBg);

    char rightBuf[16];
    std::snprintf(rightBuf, sizeof(rightBuf), "%zu/%zu",
                  model.listCursor() + 1,
                  model.currentGroupParamCount());
    drawHeader(fb, UiCatalog::groupName(model.currentGroup()), rightBuf, kColTextMuted);

    bool bpmUnit = snap.values[static_cast<size_t>(UiParamId::DELAY_UNIT_BPM)] > 0.5f;
    size_t count = model.currentGroupParamCount();
    size_t scroll = model.listScroll();
    size_t cursor = model.listCursor();

    for (size_t r = 0; r < UiModel::kListVisibleRows; ++r) {
        size_t idx = scroll + r;
        if (idx >= count) break;
        int16_t y = 32 + static_cast<int16_t>(r) * 36;
        bool sel = (idx == cursor);
        UiParamId pid = model.currentGroupParamAt(idx);
        const UiParamDesc& desc = catalog.get(pid);

        fillRect(fb, 6, y, 228, 32, sel ? kColSurfaceHi : kColSurface);
        drawRect(fb, 6, y, 228, 32, sel ? kColOrange : kColBorder, sel ? 2 : 1);

        drawText(fb, 12, y + 12, desc.label, sel ? kColOrange : kColTextPrimary, 1);

        char vbuf[24];
        UiCatalog::formatValue(desc, snap.values[static_cast<size_t>(pid)], vbuf, sizeof(vbuf), bpmUnit);
        int len = static_cast<int>(std::strlen(vbuf));
        int16_t vx = 228 - len * 6;
        drawText(fb, vx, y + 12, vbuf, desc.available ? kColTextPrimary : kColTextMuted, 1);
    }

    drawFooter(fb, "CLICK:EDIT   HOLD:MENU");
}

void UiRenderer::renderParamEdit(uint16_t* fb,
                                 const UiModel& model,
                                 const UiCatalog& catalog,
                                 const UiSnapshot& snap) const {
    fillScreen(fb, kColBg);

    UiParamId pid = model.editParamId();
    const UiParamDesc& desc = catalog.get(pid);
    float val = snap.values[static_cast<size_t>(pid)];
    bool bpmUnit = snap.values[static_cast<size_t>(UiParamId::DELAY_UNIT_BPM)] > 0.5f;

    drawHeader(fb, UiCatalog::groupName(desc.group), "EDIT", kColOrange);
    drawTextCentered(fb, 8, 38, 224, desc.label, kColTextPrimary, 2);

    char vbuf[24];
    UiCatalog::formatValue(desc, val, vbuf, sizeof(vbuf), bpmUnit);

    if (desc.kind == ParamValueKind::HEADS_MASK) {
        int mask = static_cast<int>(val + 0.5f);
        const char* names[3] = {"A", "B", "C"};
        for (int h = 0; h < 3; ++h) {
            bool on = (mask & (1 << h)) != 0;
            int16_t bx = 24 + h * 68;
            fillRect(fb, bx, 82, 56, 56, on ? kColOrange : kColSurface);
            drawRect(fb, bx, 82, 56, 56, on ? kColTextPrimary : kColBorder, 2);
            drawTextCentered(fb, bx, 98, 56, names[h], on ? kColBg : kColTextMuted, 3);
        }
        drawTextCentered(fb, 8, 152, 224, vbuf, kColTextPrimary, 2);
    } else if (desc.kind == ParamValueKind::BOOL) {
        bool on = (val > 0.5f);
        fillRect(fb, 48, 86, 144, 58, on ? kColGreen : kColSurface);
        drawRect(fb, 48, 86, 144, 58, on ? kColTextPrimary : kColBorder, 2);
        drawTextCentered(fb, 48, 102, 144, vbuf, on ? kColBg : kColTextMuted, 4);
    } else {
        uint8_t scale = (std::strlen(vbuf) <= 6) ? 4 : 3;
        drawTextCentered(fb, 8, 88, 224, vbuf, kColOrange, scale);

        float span = std::max(1e-5f, desc.maxVal - desc.minVal);
        float norm = (val - desc.minVal) / span;
        drawProgressBar(fb, 20, 146, 200, 14, norm, kColOrange, kColSurface);

        char rangeBuf[36];
        std::snprintf(rangeBuf, sizeof(rangeBuf), "%.0f .. %.0f %s",
                      desc.minVal, desc.maxVal, desc.unit ? desc.unit : "");
        drawTextCentered(fb, 8, 172, 224, rangeBuf, kColTextMuted, 1);
    }

    drawFooter(fb, "GIRO:AJUSTA   CLICK:OK");
}

void UiRenderer::renderTapTempo(uint16_t* fb, const UiSnapshot& snap) const {
    fillScreen(fb, kColBg);
    drawHeader(fb, "TAP TEMPO", "TEMPO", kColTextMuted);

    const auto& st = snap.tapState;
    char bpmBuf[20];
    if (st.status == HYDRA_DSP_TAP_STATUS_IDLE && st.tap_count == 0) {
        std::snprintf(bpmBuf, sizeof(bpmBuf), "%.1f", snap.effectiveBpm);
    } else {
        std::snprintf(bpmBuf, sizeof(bpmBuf), "%.1f", st.applied_bpm);
    }
    drawTextCentered(fb, 8, 48, 224, bpmBuf, kColTextPrimary, 4);
    drawTextCentered(fb, 8, 86, 224, "BPM", kColTextMuted, 2);

    const char* statusTxt = "IDLE";
    uint16_t statusCol = kColTextMuted;
    if (st.status == HYDRA_DSP_TAP_STATUS_ACQUIRING) {
        statusTxt = "ACQUIRING";
        statusCol = kColOrange;
    } else if (st.status == HYDRA_DSP_TAP_STATUS_LOCKED) {
        statusTxt = "LOCKED";
        statusCol = kColGreen;
    } else if (st.status == HYDRA_DSP_TAP_STATUS_REJECTED) {
        statusCol = kColRed;
        if (st.last_rejection == HYDRA_DSP_TAP_REJECT_BOUNCE) statusTxt = "REJ: BOUNCE";
        else if (st.last_rejection == HYDRA_DSP_TAP_REJECT_OUT_OF_RANGE) statusTxt = "REJ: RANGE";
        else if (st.last_rejection == HYDRA_DSP_TAP_REJECT_OUT_OF_ORDER) statusTxt = "REJ: ORDER";
        else statusTxt = "REJECTED";
    }

    fillRect(fb, 32, 118, 176, 30, kColSurface);
    drawRect(fb, 32, 118, 176, 30, statusCol, 2);
    drawTextCentered(fb, 32, 126, 176, statusTxt, statusCol, 2);

    char infoBuf[36];
    std::snprintf(infoBuf, sizeof(infoBuf), "TAPS:%u  CONF:%.0f%%",
                  static_cast<unsigned>(st.tap_count), st.confidence * 100.0f);
    drawTextCentered(fb, 8, 162, 224, infoBuf, kColTextPrimary, 1);
    drawProgressBar(fb, 32, 180, 176, 10, st.confidence, statusCol, kColSurface);

    drawFooter(fb, "CLICK:TAP   HOLD:VOLTAR");
}

void UiRenderer::renderPresets(uint16_t* fb, const UiModel& model) const {
    fillScreen(fb, kColBg);
    drawHeader(fb, "PRESETS", "SAVE:N/A", kColTextMuted);

    size_t count = UiCatalog::presetCount();
    size_t cursor = model.presetCursor();
    size_t scroll = (cursor >= 4) ? (cursor - 3) : 0;

    for (size_t r = 0; r < 4; ++r) {
        size_t idx = scroll + r;
        if (idx >= count) break;
        int16_t y = 32 + static_cast<int16_t>(r) * 38;
        bool sel = (idx == cursor);
        const auto& p = UiCatalog::presetAt(idx);

        fillRect(fb, 8, y, 224, 34, sel ? kColSurfaceHi : kColSurface);
        drawRect(fb, 8, y, 224, 34, sel ? kColOrange : kColBorder, sel ? 2 : 1);
        drawText(fb, 14, y + 6, p.displayName, sel ? kColOrange : kColTextPrimary, 2);
        drawText(fb, 136, y + 13, p.subtitle, kColTextMuted, 1);
    }

    if (model.isPresetConfirmPending()) {
        fillRect(fb, 14, 184, 212, 30, kColOrange);
        drawTextCentered(fb, 14, 192, 212, "CONFIRMAR? CLICK=SIM", kColBg, 1);
    } else {
        drawTextCentered(fb, 8, 192, 224, "Salvar indisponivel (sem storage)", kColTextMuted, 1);
    }

    drawFooter(fb, "CLICK:SEL   HOLD:MENU");
}

} // namespace hydra::ui
