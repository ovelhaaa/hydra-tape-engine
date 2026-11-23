#if defined(HYDRA_UI_BACKEND_ST7789)

#include "encoder_pcnt_esp32.h"
#include "driver/gpio.h"
#include "esp_log.h"

namespace hydra::ui {

static const char* TAG = "HYDRA_ENC";

bool EncoderPcntEsp32::begin(int pinA,
                             int pinB,
                             int pinSw,
                             int countsPerDetent,
                             uint32_t debounceMs,
                             uint32_t longPressMs) {
    pinSw_ = pinSw;
    countsPerDetent_ = (countsPerDetent > 0) ? countsPerDetent : 4;
    debounceUs_ = static_cast<uint64_t>(debounceMs) * 1000ULL;
    longPressUs_ = static_cast<uint64_t>(longPressMs) * 1000ULL;

    // Configure switch GPIO with internal pull-up
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << pinSw_);
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    if (gpio_config(&io_conf) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure encoder switch GPIO %d", pinSw_);
        return false;
    }

    // Configure PCNT hardware quadrature
    pcnt_unit_config_t unit_config = {};
    unit_config.high_limit = 10000;
    unit_config.low_limit = -10000;
    if (pcnt_new_unit(&unit_config, &pcntUnit_) != ESP_OK) {
        ESP_LOGE(TAG, "pcnt_new_unit failed");
        return false;
    }

    pcnt_glitch_filter_config_t filter_config = {};
    filter_config.max_glitch_ns = 1000;
    if (pcnt_unit_set_glitch_filter(pcntUnit_, &filter_config) != ESP_OK) {
        ESP_LOGE(TAG, "pcnt_unit_set_glitch_filter failed");
        return false;
    }

    pcnt_chan_config_t chan_a = {};
    chan_a.edge_gpio_num = pinB;
    chan_a.level_gpio_num = pinA;
    pcnt_channel_handle_t hA = nullptr;
    if (pcnt_new_channel(pcntUnit_, &chan_a, &hA) != ESP_OK) return false;
    pcnt_channel_set_edge_action(hA, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE);
    pcnt_channel_set_level_action(hA, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);

    pcnt_chan_config_t chan_b = {};
    chan_b.edge_gpio_num = pinA;
    chan_b.level_gpio_num = pinB;
    pcnt_channel_handle_t hB = nullptr;
    if (pcnt_new_channel(pcntUnit_, &chan_b, &hB) != ESP_OK) return false;
    pcnt_channel_set_edge_action(hB, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE);
    pcnt_channel_set_level_action(hB, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);

    if (pcnt_unit_enable(pcntUnit_) != ESP_OK ||
        pcnt_unit_clear_count(pcntUnit_) != ESP_OK ||
        pcnt_unit_start(pcntUnit_) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start PCNT unit");
        return false;
    }

    lastPcntCount_ = 0;
    remainder_ = 0;
    stablePressed_ = (gpio_get_level(static_cast<gpio_num_t>(pinSw_)) == 0);
    rawLast_ = stablePressed_;
    ESP_LOGI(TAG, "Encoder PCNT initialized (A=%d, B=%d, SW=%d)", pinA, pinB, pinSw_);
    return true;
}

size_t EncoderPcntEsp32::poll(uint64_t nowUs, UiEvent* outEvents, size_t maxEvents) {
    if (!outEvents || maxEvents == 0) return 0;
    size_t n = 0;

    // 1. Read quadrature detents
    if (pcntUnit_) {
        int count = 0;
        if (pcnt_unit_get_count(pcntUnit_, &count) == ESP_OK) {
            int diff = count - lastPcntCount_;
            lastPcntCount_ = count;
            if (count > 5000 || count < -5000) {
                pcnt_unit_clear_count(pcntUnit_);
                lastPcntCount_ = 0;
            }
            remainder_ += diff;
            int detents = remainder_ / countsPerDetent_;
            if (detents != 0) {
                remainder_ -= detents * countsPerDetent_;
                if (n < maxEvents) {
                    UiEvent ev{};
                    ev.type = UiEventType::ROTATE;
                    ev.delta = detents;
                    ev.timestampUs = nowUs;
                    outEvents[n++] = ev;
                }
            }
        }
    }

    // 2. Debounced switch with non-duplicating CLICK / LONG_PRESS
    if (pinSw_ >= 0) {
        bool raw = (gpio_get_level(static_cast<gpio_num_t>(pinSw_)) == 0);
        if (raw != rawLast_) {
            rawLast_ = raw;
            rawChangedUs_ = nowUs;
        }
        if ((nowUs - rawChangedUs_) >= debounceUs_ && raw != stablePressed_) {
            stablePressed_ = raw;
            if (stablePressed_) {
                pressStartUs_ = nowUs;
                longFired_ = false;
            } else {
                if (!longFired_ && n < maxEvents) {
                    UiEvent ev{};
                    ev.type = UiEventType::CLICK;
                    ev.timestampUs = pressStartUs_;
                    outEvents[n++] = ev;
                }
                longFired_ = false;
            }
        }
        if (stablePressed_ && !longFired_ && (nowUs - pressStartUs_) >= longPressUs_) {
            longFired_ = true;
            if (n < maxEvents) {
                UiEvent ev{};
                ev.type = UiEventType::LONG_PRESS;
                ev.timestampUs = nowUs;
                outEvents[n++] = ev;
            }
        }
    }

    return n;
}

} // namespace hydra::ui

#endif // HYDRA_UI_BACKEND_ST7789
