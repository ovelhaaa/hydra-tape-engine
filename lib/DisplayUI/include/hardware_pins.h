#ifndef HARDWARE_PINS_H
#define HARDWARE_PINS_H

// ============================================================================
// DISPLAY UI HARDWARE PIN DEFINITIONS
// ============================================================================
// IMPORTANT: These pins are chosen to avoid conflicts with I2S (15,16,17,18)
// and RGB LED (48)
// ============================================================================

// --- I2C for SSD1306 OLED ---
// Using safe GPIO pins that don't conflict with I2S
#define OLED_SDA_PIN      1   // I2C Data
#define OLED_SCL_PIN      2   // I2C Clock
#define OLED_I2C_ADDR     0x3C // Default SSD1306 address

// --- OLED Display Specs ---
#define OLED_WIDTH        128
#define OLED_HEIGHT       64
#define OLED_YELLOW_ZONE  16  // Top 16 pixels (yellow on dual-color display)
#define OLED_BLUE_ZONE    48  // Bottom 48 pixels (blue)

// --- Rotary Encoder ---
// Using GPIO 4, 5, 6 (safe, no conflicts)
#define ENCODER_CLK_PIN   4   // Encoder Clock (A)
#define ENCODER_DT_PIN    5   // Encoder Data (B)
#define ENCODER_SW_PIN    6   // Encoder Switch (push button)

// --- Auxiliary Button ---
#define AUX_BUTTON_PIN    7   // Back/Cancel button

// --- Button Configuration ---
#define DEBOUNCE_MS       50  // Debounce time in milliseconds
#define LONG_PRESS_MS     800 // Long press threshold

// --- Display Update ---
#define MIN_DRAW_INTERVAL_MS  50  // 20 FPS max to save CPU

// ============================================================================
// PIN CONFLICT TABLE (for reference)
// ============================================================================
// I2S (Audio DAC):
//   GPIO 15 - I2S_BCLK
//   GPIO 16 - I2S_LRCK
//   GPIO 17 - I2S_DOUT
//   GPIO 18 - I2S_DIN
//
// RGB LED:
//   GPIO 48 - RGB_PIN
//
// Serial:
//   GPIO 43 - TX
//   GPIO 44 - RX
//
// Strapping Pins (avoid):
//   GPIO 0  - Boot button
//   GPIO 45 - Strapping
//   GPIO 46 - Strapping
//
// PSRAM (if using PSRAM, these may be occupied):
//   GPIO 35-37 (SPIRAM)
// ============================================================================

#endif // HARDWARE_PINS_H
