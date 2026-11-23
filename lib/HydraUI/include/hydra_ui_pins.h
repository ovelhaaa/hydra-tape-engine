#pragma once

// ============================================================================
// HYDRA UI HARDWARE PIN DEFINITIONS & COLLISION CHECKS
// ============================================================================
// Centralized GPIO map for ESP32-S3 N16R8.
// Legacy OLED (SSD1306 I2C + AUX button on GPIO7) is preserved in the default
// OLED profile. The ST7789 240x240 profile reassigns GPIO7 to ST7789 BL and
// disables AUX_BUTTON_PIN to eliminate the GPIO7 hardware conflict.
// ============================================================================

#ifndef HYDRA_I2S_BCLK_PIN
#define HYDRA_I2S_BCLK_PIN 15
#endif
#ifndef HYDRA_I2S_LRCK_PIN
#define HYDRA_I2S_LRCK_PIN 16
#endif
#ifndef HYDRA_I2S_DOUT_PIN
#define HYDRA_I2S_DOUT_PIN 17
#endif
#ifndef HYDRA_I2S_DIN_PIN
#define HYDRA_I2S_DIN_PIN 18
#endif
#ifndef HYDRA_RGB_LED_PIN
#define HYDRA_RGB_LED_PIN 48
#endif

// --- Rotary Encoder (shared across UI profiles) ---
#define HYDRA_ENCODER_A_PIN   4
#define HYDRA_ENCODER_B_PIN   5
#define HYDRA_ENCODER_SW_PIN  6

#define HYDRA_ENCODER_DEBOUNCE_MS    35
#define HYDRA_ENCODER_LONG_PRESS_MS  800
#define HYDRA_ENCODER_COUNTS_PER_DETENT 4

#if defined(HYDRA_UI_BACKEND_ST7789)

// --- ST7789 1.54" 240x240 SPI Display (validated in smk-s3) ---
#define HYDRA_ST7789_MOSI_PIN  11
#define HYDRA_ST7789_SCLK_PIN  12
#define HYDRA_ST7789_CS_PIN    10
#define HYDRA_ST7789_DC_PIN    9
#define HYDRA_ST7789_RST_PIN   8
#define HYDRA_ST7789_BL_PIN    7

// AUX button is explicitly disabled in ST7789 MVP to avoid GPIO7 collision.
#define HYDRA_HAS_AUX_BUTTON   0

#define HYDRA_ST7789_WIDTH         240
#define HYDRA_ST7789_HEIGHT        240
#define HYDRA_ST7789_X_OFFSET      0
#define HYDRA_ST7789_Y_OFFSET      0
#define HYDRA_ST7789_SWAP_XY       false
#define HYDRA_ST7789_MIRROR_X      false
#define HYDRA_ST7789_MIRROR_Y      false
#define HYDRA_ST7789_INVERT_COLOR  true
#define HYDRA_ST7789_BL_ACTIVE_LOW false
#define HYDRA_ST7789_SPI_HZ        40000000
#define HYDRA_ST7789_MIN_FRAME_MS  66  // ~15 FPS target

// Compile-time collision & reserved-pin checks
namespace hydra::ui::pins {

constexpr int kReservedPins[] = {
    0,                          // Boot strap
    HYDRA_I2S_BCLK_PIN,         // 15: I2S BCLK
    HYDRA_I2S_LRCK_PIN,         // 16: I2S LRCK
    HYDRA_I2S_DOUT_PIN,         // 17: I2S DOUT
    HYDRA_I2S_DIN_PIN,          // 18: I2S DIN
    19, 20,                     // Native USB D-/D+
    33, 34, 35, 36, 37,         // Octal PSRAM (OPI)
    43, 44,                     // UART0 TX/RX
    45, 46,                     // Strapping pins
    HYDRA_RGB_LED_PIN           // 48: Onboard RGB LED
};

constexpr int kUiPins[] = {
    HYDRA_ST7789_MOSI_PIN,
    HYDRA_ST7789_SCLK_PIN,
    HYDRA_ST7789_CS_PIN,
    HYDRA_ST7789_DC_PIN,
    HYDRA_ST7789_RST_PIN,
    HYDRA_ST7789_BL_PIN,
    HYDRA_ENCODER_A_PIN,
    HYDRA_ENCODER_B_PIN,
    HYDRA_ENCODER_SW_PIN
};

constexpr bool hasNoCollisions() {
    for (int i = 0; i < (int)(sizeof(kUiPins) / sizeof(kUiPins[0])); ++i) {
        if (kUiPins[i] < 0) return false;
        for (int j = i + 1; j < (int)(sizeof(kUiPins) / sizeof(kUiPins[0])); ++j) {
            if (kUiPins[i] == kUiPins[j]) return false;
        }
        for (int r : kReservedPins) {
            if (kUiPins[i] == r) return false;
        }
    }
    return true;
}

static_assert(hasNoCollisions(), "ST7789/Encoder GPIO collision with reserved or duplicate pin!");

} // namespace hydra::ui::pins

#endif // HYDRA_UI_BACKEND_ST7789
