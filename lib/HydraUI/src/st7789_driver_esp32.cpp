#if defined(HYDRA_UI_BACKEND_ST7789)

#include "st7789_driver_esp32.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "driver/ledc.h"
#include "esp_attr.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace hydra::ui {

static const char* TAG = "HYDRA_ST7789";

ST7789DriverEsp32::ST7789DriverEsp32(const ST7789Config& cfg) : cfg_(cfg) {}

ST7789DriverEsp32::~ST7789DriverEsp32() {
    if (blInit_) setBrightness(0);
    if (panelHandle_) {
        esp_lcd_panel_disp_on_off(panelHandle_, false);
        esp_lcd_panel_del(panelHandle_);
        panelHandle_ = nullptr;
    }
    if (ioHandle_) {
        esp_lcd_panel_io_del(ioHandle_);
        ioHandle_ = nullptr;
    }
    if (spiBusInit_) {
        spi_bus_free(cfg_.spiHost);
        spiBusInit_ = false;
    }
    if (transferBuf_) {
        free(transferBuf_);
        transferBuf_ = nullptr;
    }
    if (framebuffer_) {
        free(framebuffer_);
        framebuffer_ = nullptr;
    }
}

bool ST7789DriverEsp32::begin() {
    if (cfg_.width <= 0 || cfg_.height <= 0) return false;

    const size_t fbSize = framebufferBytes();
    framebuffer_ = static_cast<uint16_t*>(
        heap_caps_malloc(fbSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!framebuffer_) {
        ESP_LOGE(TAG, "Failed to allocate PSRAM framebuffer (%zu bytes)", fbSize);
        return false;
    }
    std::memset(framebuffer_, 0, fbSize);

    const int16_t rows = std::min(cfg_.height, kTransferRows);
    transferBufPixels_ = static_cast<size_t>(cfg_.width) * rows;
    transferBuf_ = static_cast<uint16_t*>(
        heap_caps_malloc(transferBufPixels_ * sizeof(uint16_t),
                         MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (!transferBuf_) {
        ESP_LOGE(TAG, "Failed to allocate internal DMA window (%zu bytes)",
                 transferBufPixels_ * sizeof(uint16_t));
        return false;
    }

    transferDoneSem_ = xSemaphoreCreateBinaryStatic(&transferDoneStorage_);
    if (!transferDoneSem_) return false;

    // Keep backlight OFF until a valid black frame has reached GRAM
    if (cfg_.blPin >= 0) {
        ledc_timer_config_t ledc_timer = {};
        ledc_timer.speed_mode = LEDC_LOW_SPEED_MODE;
        ledc_timer.duty_resolution = LEDC_TIMER_8_BIT;
        ledc_timer.timer_num = LEDC_TIMER_0;
        ledc_timer.freq_hz = 5000;
        ledc_timer.clk_cfg = LEDC_AUTO_CLK;
        if (ledc_timer_config(&ledc_timer) != ESP_OK) return false;

        ledc_channel_config_t ledc_channel = {};
        ledc_channel.gpio_num = cfg_.blPin;
        ledc_channel.speed_mode = LEDC_LOW_SPEED_MODE;
        ledc_channel.channel = LEDC_CHANNEL_0;
        ledc_channel.intr_type = LEDC_INTR_DISABLE;
        ledc_channel.timer_sel = LEDC_TIMER_0;
        ledc_channel.duty = cfg_.blActiveLow ? 255 : 0;
        ledc_channel.hpoint = 0;
        if (ledc_channel_config(&ledc_channel) != ESP_OK) return false;
        blInit_ = true;
    }

    spi_bus_config_t buscfg = {};
    buscfg.sclk_io_num = cfg_.sclkPin;
    buscfg.mosi_io_num = cfg_.mosiPin;
    buscfg.miso_io_num = -1;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = transferBufPixels_ * sizeof(uint16_t);
    if (spi_bus_initialize(cfg_.spiHost, &buscfg, SPI_DMA_CH_AUTO) != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize failed");
        return false;
    }
    spiBusInit_ = true;

    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.dc_gpio_num = static_cast<gpio_num_t>(cfg_.dcPin);
    io_config.cs_gpio_num = static_cast<gpio_num_t>(cfg_.csPin);
    io_config.pclk_hz = cfg_.clockSpeedHz;
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;
    io_config.spi_mode = 0;
    io_config.trans_queue_depth = 1;
    io_config.on_color_trans_done = onColorTransferDone;
    io_config.user_ctx = this;

    if (esp_lcd_new_panel_io_spi(
            static_cast<esp_lcd_spi_bus_handle_t>(cfg_.spiHost),
            &io_config, &ioHandle_) != ESP_OK) {
        return false;
    }

    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = static_cast<gpio_num_t>(cfg_.rstPin);
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
    panel_config.bits_per_pixel = 16;

    if (esp_lcd_new_panel_st7789(ioHandle_, &panel_config, &panelHandle_) != ESP_OK) {
        return false;
    }

    if (esp_lcd_panel_reset(panelHandle_) != ESP_OK ||
        esp_lcd_panel_init(panelHandle_) != ESP_OK ||
        esp_lcd_panel_invert_color(panelHandle_, cfg_.invertColor) != ESP_OK ||
        esp_lcd_panel_swap_xy(panelHandle_, cfg_.swapXy) != ESP_OK ||
        esp_lcd_panel_mirror(panelHandle_, cfg_.mirrorX, cfg_.mirrorY) != ESP_OK) {
        return false;
    }
    esp_lcd_panel_set_gap(panelHandle_, cfg_.xOffset, cfg_.yOffset);

    if (!flushFull()) {
        return false;
    }
    if (esp_lcd_panel_disp_on_off(panelHandle_, true) != ESP_OK) {
        return false;
    }
    setBrightness(brightness_);
    ESP_LOGI(TAG, "ST7789 initialized (%dx%d, fb=%zuB PSRAM, dma=%zuB internal)",
             cfg_.width, cfg_.height, fbSize, dmaWindowBytes());
    return true;
}

void ST7789DriverEsp32::setBrightness(uint8_t value) {
    brightness_ = value;
    if (blInit_ && cfg_.blPin >= 0) {
        uint32_t duty = cfg_.blActiveLow ? (255u - value) : value;
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    }
}

bool ST7789DriverEsp32::flushFull() {
    return transferRegion(0, 0, cfg_.width, cfg_.height);
}

bool ST7789DriverEsp32::flushRegion(int16_t x, int16_t y, int16_t w, int16_t h) {
    return transferRegion(x, y, w, h);
}

bool ST7789DriverEsp32::transferRegion(int16_t x, int16_t y, int16_t w, int16_t h) {
    if (!panelHandle_ || !framebuffer_ || !transferBuf_ || !transferDoneSem_ || w <= 0 || h <= 0) {
        return false;
    }
    int16_t x1 = std::max<int16_t>(0, x);
    int16_t y1 = std::max<int16_t>(0, y);
    int16_t x2 = std::min<int16_t>(cfg_.width, x + w);
    int16_t y2 = std::min<int16_t>(cfg_.height, y + h);
    if (x1 >= x2 || y1 >= y2) return true;

    const int16_t regionW = x2 - x1;
    const size_t rowBytes = static_cast<size_t>(regionW) * sizeof(uint16_t);

    for (int16_t chunkY = y1; chunkY < y2;) {
        const int16_t rows = std::min<int16_t>(kTransferRows, y2 - chunkY);
        const size_t chunkPixels = static_cast<size_t>(regionW) * rows;
        if (chunkPixels > transferBufPixels_) {
            ++transferFailures_;
            return false;
        }
        for (int16_t r = 0; r < rows; ++r) {
            const uint16_t* src = &framebuffer_[static_cast<size_t>(chunkY + r) * cfg_.width + x1];
            std::memcpy(&transferBuf_[static_cast<size_t>(r) * regionW], src, rowBytes);
        }
        while (xSemaphoreTake(transferDoneSem_, 0) == pdTRUE) {}
        if (esp_lcd_panel_draw_bitmap(panelHandle_, x1, chunkY, x2, chunkY + rows, transferBuf_) != ESP_OK) {
            ++transferFailures_;
            return false;
        }
        if (xSemaphoreTake(transferDoneSem_, kTransferTimeoutTicks) != pdTRUE) {
            ++transferFailures_;
            return false;
        }
        chunkY += rows;
    }
    return true;
}

bool IRAM_ATTR ST7789DriverEsp32::onColorTransferDone(
    esp_lcd_panel_io_handle_t panel_io,
    esp_lcd_panel_io_event_data_t* event_data,
    void* user_ctx) {
    (void)panel_io;
    (void)event_data;
    auto* self = static_cast<ST7789DriverEsp32*>(user_ctx);
    if (!self || !self->transferDoneSem_) return false;
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(self->transferDoneSem_, &woken);
    return woken == pdTRUE;
}

} // namespace hydra::ui

#endif // HYDRA_UI_BACKEND_ST7789
