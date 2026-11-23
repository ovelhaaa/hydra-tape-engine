#pragma once

#if defined(HYDRA_UI_BACKEND_ST7789)

#include <cstdint>
#include <cstddef>
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace hydra::ui {

struct ST7789Config {
    spi_host_device_t spiHost = SPI2_HOST;
    int mosiPin = 11;
    int sclkPin = 12;
    int csPin   = 10;
    int dcPin   = 9;
    int rstPin  = 8;
    int blPin   = 7;
    int16_t width  = 240;
    int16_t height = 240;
    uint16_t xOffset = 0;
    uint16_t yOffset = 0;
    bool swapXy      = false;
    bool mirrorX     = false;
    bool mirrorY     = false;
    bool invertColor = true;
    bool blActiveLow = false;
    int clockSpeedHz = 40000000;
};

class ST7789DriverEsp32 {
public:
    static constexpr int16_t kTransferRows = 16;
    static constexpr TickType_t kTransferTimeoutTicks = pdMS_TO_TICKS(100);

    explicit ST7789DriverEsp32(const ST7789Config& cfg);
    ~ST7789DriverEsp32();

    bool begin();
    void setBrightness(uint8_t value);

    uint16_t* framebuffer() { return framebuffer_; }
    size_t framebufferBytes() const {
        return static_cast<size_t>(cfg_.width) * cfg_.height * sizeof(uint16_t);
    }
    size_t dmaWindowBytes() const {
        return transferBufPixels_ * sizeof(uint16_t);
    }

    // Flush full framebuffer (or bounded region) over internal DMA window
    bool flushFull();
    bool flushRegion(int16_t x, int16_t y, int16_t w, int16_t h);

    uint32_t transferFailures() const { return transferFailures_; }

private:
    ST7789Config cfg_;
    uint16_t* framebuffer_ = nullptr;
    uint16_t* transferBuf_ = nullptr;
    size_t transferBufPixels_ = 0;

    esp_lcd_panel_io_handle_t ioHandle_ = nullptr;
    esp_lcd_panel_handle_t panelHandle_ = nullptr;
    bool spiBusInit_ = false;
    bool blInit_ = false;
    uint8_t brightness_ = 220;

    SemaphoreHandle_t transferDoneSem_ = nullptr;
    StaticSemaphore_t transferDoneStorage_{};
    uint32_t transferFailures_ = 0;

    static bool onColorTransferDone(esp_lcd_panel_io_handle_t panel_io,
                                    esp_lcd_panel_io_event_data_t* event_data,
                                    void* user_ctx);
    bool transferRegion(int16_t x, int16_t y, int16_t w, int16_t h);
};

} // namespace hydra::ui

#endif // HYDRA_UI_BACKEND_ST7789
