#if defined(HYDRA_UI_BACKEND_ST7789)

#include "hydra_ui_esp32_bridge.h"
#include "hydra_dsp_esp32_adapter.h"
#include "TapeDelay.h"
#include "MelodyGen.h"
#include <Arduino.h>
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <algorithm>
#include <cstring>

// Globals owned by src/main.cpp
extern SemaphoreHandle_t paramMutex;
extern HydraDspEsp32Adapter* tape;
extern bool isBypassed;
extern float p_masterVol;
extern float p_flutterDepth;
extern float p_wowDepth;
extern float p_dropoutSeverity;
extern float p_drive;
extern float p_noise;
extern float p_flutterRate;
extern float p_wowRate;
extern float p_tapeSpeed;
extern float p_tapeAge;
extern float p_headBump;
extern float p_azimuth;
extern float p_bpm;
extern bool p_headsMusical;
extern bool p_guitarFocus;
extern float p_tone;
extern bool p_freeze;
extern bool p_reverse;
extern int p_reverseMode;
extern int p_reverseDivision;
extern bool p_reverseSmear;
extern bool p_spring;
extern bool p_pingPong;
extern float p_springMix;
extern float p_springDecay;
extern float p_springDamping;
extern bool p_warpActive;
extern float p_warpSpeed;
extern float p_warpGrainMs;
extern float p_warpStereo;
extern float p_warpFeedback;
extern int p_warpMode;
extern bool p_delayActive;
extern float p_delayTime;
extern float p_feedback;
extern float p_mix;
extern int p_activeHeads;
extern bool p_delayUnitBpm;
extern bool p_frippMode;
extern bool p_bubblesMode;
extern bool p_freeverbEnabled;
extern FreeverbParams globalFreeverbParams;
extern volatile bool p_genActive;
extern int p_waveform;
extern int p_scale;
extern int p_pitch;
extern float p_mood;
extern float p_rhythm;
extern bool p_enoMode;
extern int getAudioSourceIndex();
extern void setAudioSourceIndex(int idx);

extern void syncGlobalParams();
extern void loadPreset(String name);

namespace hydra::ui {

namespace {

ST7789Config makeDefaultDisplayConfig() {
    ST7789Config cfg{};
    cfg.spiHost = SPI2_HOST;
    cfg.mosiPin = HYDRA_ST7789_MOSI_PIN;
    cfg.sclkPin = HYDRA_ST7789_SCLK_PIN;
    cfg.csPin   = HYDRA_ST7789_CS_PIN;
    cfg.dcPin   = HYDRA_ST7789_DC_PIN;
    cfg.rstPin  = HYDRA_ST7789_RST_PIN;
    cfg.blPin   = HYDRA_ST7789_BL_PIN;
    cfg.width   = HYDRA_ST7789_WIDTH;
    cfg.height  = HYDRA_ST7789_HEIGHT;
    cfg.xOffset = HYDRA_ST7789_X_OFFSET;
    cfg.yOffset = HYDRA_ST7789_Y_OFFSET;
    cfg.swapXy  = HYDRA_ST7789_SWAP_XY;
    cfg.mirrorX = HYDRA_ST7789_MIRROR_X;
    cfg.mirrorY = HYDRA_ST7789_MIRROR_Y;
    cfg.invertColor = HYDRA_ST7789_INVERT_COLOR;
    cfg.blActiveLow = HYDRA_ST7789_BL_ACTIVE_LOW;
    cfg.clockSpeedHz = HYDRA_ST7789_SPI_HZ;
    return cfg;
}

} // namespace

HydraUiEsp32Bridge::HydraUiEsp32Bridge()
    : model_(catalog_),
      renderer_(true),
      display_(makeDefaultDisplayConfig()) {}

HydraUiEsp32Bridge::~HydraUiEsp32Bridge() = default;

bool HydraUiEsp32Bridge::begin() {
    catalog_.syncFromDspSpecs(hydra_dsp_get_param_specs(),
                              hydra_dsp_get_param_count(),
                              2000.0f);

    if (!display_.begin()) {
        return false;
    }
    if (!encoder_.begin(HYDRA_ENCODER_A_PIN,
                        HYDRA_ENCODER_B_PIN,
                        HYDRA_ENCODER_SW_PIN,
                        HYDRA_ENCODER_COUNTS_PER_DETENT,
                        HYDRA_ENCODER_DEBOUNCE_MS,
                        HYDRA_ENCODER_LONG_PRESS_MS)) {
        return false;
    }

    bootUs_ = static_cast<uint64_t>(esp_timer_get_time());
    fpsWindowStartUs_ = bootUs_;
    model_.reset(true);
    captureSnapshot(bootUs_);
    dirty_ = true;
    return true;
}

void HydraUiEsp32Bridge::captureSnapshot(uint64_t nowUs) {
    (void)nowUs;
    auto& v = snapshot_.values;
    if (xSemaphoreTake(paramMutex, pdMS_TO_TICKS(2)) == pdTRUE) {
        snapshot_.bypassed = isBypassed;
        v[static_cast<size_t>(UiParamId::DELAY_ACTIVE)]    = p_delayActive ? 1.0f : 0.0f;
        v[static_cast<size_t>(UiParamId::DELAY_TIME_MS)]   = p_delayTime;
        v[static_cast<size_t>(UiParamId::FEEDBACK)]        = p_feedback;
        v[static_cast<size_t>(UiParamId::DRY_WET)]         = p_mix;
        v[static_cast<size_t>(UiParamId::ACTIVE_HEADS)]    = static_cast<float>(p_activeHeads);
        v[static_cast<size_t>(UiParamId::PING_PONG)]       = p_pingPong ? 1.0f : 0.0f;
        v[static_cast<size_t>(UiParamId::FREEZE)]          = p_freeze ? 1.0f : 0.0f;

        v[static_cast<size_t>(UiParamId::TAPE_SPEED)]      = p_tapeSpeed;
        v[static_cast<size_t>(UiParamId::TAPE_AGE)]        = p_tapeAge;
        v[static_cast<size_t>(UiParamId::DRIVE)]           = p_drive;
        v[static_cast<size_t>(UiParamId::NOISE)]           = p_noise;
        v[static_cast<size_t>(UiParamId::HEAD_BUMP)]       = p_headBump;
        v[static_cast<size_t>(UiParamId::AZIMUTH)]         = p_azimuth;
        v[static_cast<size_t>(UiParamId::DROPOUT)]         = p_dropoutSeverity;
        v[static_cast<size_t>(UiParamId::TONE)]            = p_tone;
        v[static_cast<size_t>(UiParamId::GUITAR_FOCUS)]    = p_guitarFocus ? 1.0f : 0.0f;

        v[static_cast<size_t>(UiParamId::FLUTTER_DEPTH)]   = p_flutterDepth;
        v[static_cast<size_t>(UiParamId::FLUTTER_RATE)]    = p_flutterRate;
        v[static_cast<size_t>(UiParamId::WOW_DEPTH)]       = p_wowDepth;
        v[static_cast<size_t>(UiParamId::WOW_RATE)]        = p_wowRate;

        v[static_cast<size_t>(UiParamId::REVERSE_ACTIVE)]  = p_reverse ? 1.0f : 0.0f;
        v[static_cast<size_t>(UiParamId::REVERSE_MODE)]    = static_cast<float>(p_reverseMode);
        v[static_cast<size_t>(UiParamId::REVERSE_DIVISION)] = static_cast<float>(p_reverseDivision);
        v[static_cast<size_t>(UiParamId::REVERSE_SMEAR)]   = p_reverseSmear ? 1.0f : 0.0f;

        v[static_cast<size_t>(UiParamId::WARP_ACTIVE)]     = p_warpActive ? 1.0f : 0.0f;
        v[static_cast<size_t>(UiParamId::WARP_SPEED)]      = p_warpSpeed;
        v[static_cast<size_t>(UiParamId::WARP_GRAIN_MS)]   = p_warpGrainMs;
        v[static_cast<size_t>(UiParamId::WARP_STEREO)]     = p_warpStereo;
        v[static_cast<size_t>(UiParamId::WARP_FEEDBACK)]   = p_warpFeedback;
        v[static_cast<size_t>(UiParamId::WARP_MODE)]       = static_cast<float>(p_warpMode);

        v[static_cast<size_t>(UiParamId::SPRING_ACTIVE)]   = p_spring ? 1.0f : 0.0f;
        v[static_cast<size_t>(UiParamId::SPRING_MIX)]      = p_springMix;
        v[static_cast<size_t>(UiParamId::SPRING_DECAY)]    = p_springDecay;
        v[static_cast<size_t>(UiParamId::SPRING_DAMPING)]  = p_springDamping;

        v[static_cast<size_t>(UiParamId::FREEVERB_ACTIVE)] = p_freeverbEnabled ? 1.0f : 0.0f;
        v[static_cast<size_t>(UiParamId::FREEVERB_ROOM)]   = globalFreeverbParams.roomSize;
        v[static_cast<size_t>(UiParamId::FREEVERB_DAMPING)] = globalFreeverbParams.damping;
        v[static_cast<size_t>(UiParamId::FREEVERB_WET)]    = globalFreeverbParams.wet;
        v[static_cast<size_t>(UiParamId::FREEVERB_WIDTH)]  = globalFreeverbParams.width;

        v[static_cast<size_t>(UiParamId::BPM)]             = p_bpm;
        v[static_cast<size_t>(UiParamId::HEADS_MUSICAL)]   = p_headsMusical ? 1.0f : 0.0f;
        v[static_cast<size_t>(UiParamId::DELAY_UNIT_BPM)]  = p_delayUnitBpm ? 1.0f : 0.0f;

        v[static_cast<size_t>(UiParamId::HOST_FRIPP_MODE)] = p_frippMode ? 1.0f : 0.0f;
        v[static_cast<size_t>(UiParamId::HOST_BUBBLES_MODE)] = p_bubblesMode ? 1.0f : 0.0f;

        v[static_cast<size_t>(UiParamId::SW_BYPASS)]       = isBypassed ? 1.0f : 0.0f;
        v[static_cast<size_t>(UiParamId::MASTER_VOL)]      = p_masterVol;
        v[static_cast<size_t>(UiParamId::AUDIO_SOURCE)]    = static_cast<float>(getAudioSourceIndex());
        v[static_cast<size_t>(UiParamId::GEN_ACTIVE)]      = p_genActive ? 1.0f : 0.0f;
        v[static_cast<size_t>(UiParamId::MELODY_WAVEFORM)] = static_cast<float>(p_waveform);
        v[static_cast<size_t>(UiParamId::MELODY_SCALE)]    = static_cast<float>(p_scale);
        v[static_cast<size_t>(UiParamId::MELODY_KEY)]      = static_cast<float>(p_pitch);
        v[static_cast<size_t>(UiParamId::MELODY_MOOD)]     = p_mood;
        v[static_cast<size_t>(UiParamId::MELODY_RHYTHM)]   = p_rhythm;
        v[static_cast<size_t>(UiParamId::MELODY_ENO)]      = p_enoMode ? 1.0f : 0.0f;

        snapshot_.effectiveBpm = p_bpm;
        xSemaphoreGive(paramMutex);
    }

    if (tape) {
        tape->tapTempoGetState(&snapshot_.tapState);
        tape->transportGetState(&snapshot_.transportState);
        uint32_t src = 0;
        if (tape->getTempoSource(&src) == 0) {
            v[static_cast<size_t>(UiParamId::TEMPO_SOURCE)] = static_cast<float>(src);
        }
        if (snapshot_.transportState.bpm >= 30.0f) {
            snapshot_.effectiveBpm = snapshot_.transportState.bpm;
        }
    }

    snapshot_.freeInternalBytes = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    snapshot_.freePsramBytes = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    snapshot_.uiFps = currentFps_;
    snapshot_.generation++;
}

void HydraUiEsp32Bridge::executeCommand(const UiCommand& cmd) {
    switch (cmd.type) {
        case UiCommandType::TAP_TEMPO:
            if (tape) {
                tape->postTapTempoEvent(0, static_cast<int64_t>(cmd.timestampUs));
            }
            return;

        case UiCommandType::RESET_DSP:
            if (tape) {
                tape->postTransportReset();
            }
            return;

        case UiCommandType::TOGGLE_BYPASS:
            if (xSemaphoreTake(paramMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                isBypassed = (cmd.value > 0.5f);
                xSemaphoreGive(paramMutex);
            }
            return;

        case UiCommandType::APPLY_MODE:
            if (xSemaphoreTake(paramMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                p_frippMode = false;
                p_bubblesMode = false;
                switch (cmd.modeId) {
                    case SemanticModeId::TAPE_DELAY:
                        p_delayActive = true;
                        p_reverse = false;
                        p_warpActive = false;
                        break;
                    case SemanticModeId::TAPE_SATURATOR:
                        p_delayActive = false;
                        p_reverse = false;
                        p_warpActive = false;
                        break;
                    case SemanticModeId::REVERSE:
                        p_delayActive = true;
                        p_reverse = true;
                        p_warpActive = false;
                        break;
                    case SemanticModeId::WARP:
                        p_delayActive = true;
                        p_reverse = false;
                        p_warpActive = true;
                        break;
                    case SemanticModeId::REVERSE_WARP:
                        p_delayActive = true;
                        p_reverse = true;
                        p_warpActive = true;
                        break;
                    default:
                        break;
                }
                xSemaphoreGive(paramMutex);
                syncGlobalParams();
            }
            return;

        case UiCommandType::LOAD_PRESET: {
            const auto& preset = UiCatalog::presetAt(cmd.presetIndex);
            if (xSemaphoreTake(paramMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                loadPreset(String(preset.cliName));
                xSemaphoreGive(paramMutex);
                syncGlobalParams();
                captureSnapshot(static_cast<uint64_t>(esp_timer_get_time()));
                model_.onPresetLoaded(snapshot_);
            }
            return;
        }

        case UiCommandType::SET_PARAM:
            break;

        default:
            return;
    }

    // SET_PARAM handling
    bool needsSync = true;
    if (cmd.paramId == UiParamId::TEMPO_SOURCE) {
        if (tape) {
            tape->postSetTempoSource(static_cast<uint32_t>(cmd.value + 0.5f));
        }
        return;
    }

    if (xSemaphoreTake(paramMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        switch (cmd.paramId) {
            case UiParamId::DELAY_ACTIVE:     p_delayActive = (cmd.value > 0.5f); break;
            case UiParamId::DELAY_TIME_MS:    p_delayTime = cmd.value; break;
            case UiParamId::FEEDBACK:         p_feedback = cmd.value; break;
            case UiParamId::DRY_WET:          p_mix = cmd.value; break;
            case UiParamId::ACTIVE_HEADS:     p_activeHeads = static_cast<int>(cmd.value + 0.5f); break;
            case UiParamId::PING_PONG:        p_pingPong = (cmd.value > 0.5f); break;
            case UiParamId::FREEZE:           p_freeze = (cmd.value > 0.5f); break;

            case UiParamId::TAPE_SPEED:       p_tapeSpeed = cmd.value; break;
            case UiParamId::TAPE_AGE:         p_tapeAge = cmd.value; break;
            case UiParamId::DRIVE:            p_drive = cmd.value; break;
            case UiParamId::NOISE:            p_noise = cmd.value; break;
            case UiParamId::HEAD_BUMP:        p_headBump = cmd.value; break;
            case UiParamId::AZIMUTH:          p_azimuth = cmd.value; break;
            case UiParamId::DROPOUT:          p_dropoutSeverity = cmd.value; break;
            case UiParamId::TONE:             p_tone = cmd.value; break;
            case UiParamId::GUITAR_FOCUS:     p_guitarFocus = (cmd.value > 0.5f); break;

            case UiParamId::FLUTTER_DEPTH:    p_flutterDepth = cmd.value; break;
            case UiParamId::FLUTTER_RATE:     p_flutterRate = cmd.value; break;
            case UiParamId::WOW_DEPTH:        p_wowDepth = cmd.value; break;
            case UiParamId::WOW_RATE:         p_wowRate = cmd.value; break;

            case UiParamId::REVERSE_ACTIVE:   p_reverse = (cmd.value > 0.5f); break;
            case UiParamId::REVERSE_MODE:     p_reverseMode = static_cast<int>(cmd.value + 0.5f); break;
            case UiParamId::REVERSE_DIVISION: p_reverseDivision = static_cast<int>(cmd.value + 0.5f); break;
            case UiParamId::REVERSE_SMEAR:    p_reverseSmear = (cmd.value > 0.5f); break;

            case UiParamId::WARP_ACTIVE:      p_warpActive = (cmd.value > 0.5f); break;
            case UiParamId::WARP_SPEED:       p_warpSpeed = cmd.value; break;
            case UiParamId::WARP_GRAIN_MS:    p_warpGrainMs = cmd.value; break;
            case UiParamId::WARP_STEREO:      p_warpStereo = cmd.value; break;
            case UiParamId::WARP_FEEDBACK:    p_warpFeedback = cmd.value; break;
            case UiParamId::WARP_MODE:        p_warpMode = static_cast<int>(cmd.value + 0.5f); break;

            case UiParamId::SPRING_ACTIVE:    p_spring = (cmd.value > 0.5f); break;
            case UiParamId::SPRING_MIX:       p_springMix = cmd.value; break;
            case UiParamId::SPRING_DECAY:     p_springDecay = cmd.value; break;
            case UiParamId::SPRING_DAMPING:   p_springDamping = cmd.value; break;

            case UiParamId::FREEVERB_ACTIVE:  p_freeverbEnabled = (cmd.value > 0.5f); break;
            case UiParamId::FREEVERB_ROOM:    globalFreeverbParams.roomSize = cmd.value; break;
            case UiParamId::FREEVERB_DAMPING: globalFreeverbParams.damping = cmd.value; break;
            case UiParamId::FREEVERB_WET:     globalFreeverbParams.wet = cmd.value; break;
            case UiParamId::FREEVERB_WIDTH:   globalFreeverbParams.width = cmd.value; break;

            case UiParamId::BPM:
                p_bpm = cmd.value;
                if (tape) tape->postTransportSetBpm(p_bpm);
                break;
            case UiParamId::HEADS_MUSICAL:    p_headsMusical = (cmd.value > 0.5f); break;
            case UiParamId::DELAY_UNIT_BPM:   p_delayUnitBpm = (cmd.value > 0.5f); needsSync = false; break;

            case UiParamId::HOST_FRIPP_MODE:  p_frippMode = (cmd.value > 0.5f); break;
            case UiParamId::HOST_BUBBLES_MODE: p_bubblesMode = (cmd.value > 0.5f); break;

            case UiParamId::SW_BYPASS:        isBypassed = (cmd.value > 0.5f); needsSync = false; break;
            case UiParamId::MASTER_VOL:       p_masterVol = cmd.value; break;
            case UiParamId::AUDIO_SOURCE:     setAudioSourceIndex(static_cast<int>(cmd.value + 0.5f)); needsSync = false; break;
            case UiParamId::GEN_ACTIVE:       p_genActive = (cmd.value > 0.5f); needsSync = false; break;
            case UiParamId::MELODY_WAVEFORM:  p_waveform = static_cast<int>(cmd.value + 0.5f); break;
            case UiParamId::MELODY_SCALE:     p_scale = static_cast<int>(cmd.value + 0.5f); break;
            case UiParamId::MELODY_KEY:       p_pitch = static_cast<int>(cmd.value + 0.5f); break;
            case UiParamId::MELODY_MOOD:      p_mood = cmd.value; break;
            case UiParamId::MELODY_RHYTHM:    p_rhythm = cmd.value; break;
            case UiParamId::MELODY_ENO:       p_enoMode = (cmd.value > 0.5f); break;
            default:
                needsSync = false;
                break;
        }
        xSemaphoreGive(paramMutex);
    }

    if (needsSync) {
        syncGlobalParams();
    }
}

void HydraUiEsp32Bridge::tick() {
    uint64_t nowUs = static_cast<uint64_t>(esp_timer_get_time());

    // Auto-dismiss splash after 1.2 seconds
    if (model_.screen() == UiScreen::SPLASH && (nowUs - bootUs_) >= 1200000ULL) {
        model_.dismissSplash();
        dirty_ = true;
    }

    // Poll encoder events
    UiEvent events[4]{};
    size_t evCount = encoder_.poll(nowUs, events, 4);
    for (size_t i = 0; i < evCount; ++i) {
        captureSnapshot(nowUs);
        UiCommand cmds[UiModel::kMaxCommandsPerEvent]{};
        size_t cmdCount = model_.handleEvent(events[i], snapshot_, cmds, UiModel::kMaxCommandsPerEvent);
        for (size_t c = 0; c < cmdCount; ++c) {
            executeCommand(cmds[c]);
        }
        dirty_ = true;
    }

    // Periodic refresh on Tap Tempo or when status banner expires
    if (model_.screen() == UiScreen::TAP_TEMPO && (nowUs - lastDrawUs_) >= 100000ULL) {
        dirty_ = true;
    }
    if (model_.statusBanner()[0] != '\0' && !model_.hasStatusBanner(nowUs)) {
        model_.clearStatusBanner();
        dirty_ = true;
    }

    // Rate-limited render + DMA flush (~15 FPS)
    constexpr uint64_t kMinFrameUs = static_cast<uint64_t>(HYDRA_ST7789_MIN_FRAME_MS) * 1000ULL;
    if (dirty_ && (nowUs - lastDrawUs_) >= kMinFrameUs) {
        captureSnapshot(nowUs);

        uint64_t t0 = static_cast<uint64_t>(esp_timer_get_time());
        renderer_.render(display_.framebuffer(), model_, catalog_, snapshot_, nowUs);
        uint64_t t1 = static_cast<uint64_t>(esp_timer_get_time());
        display_.flushFull();
        uint64_t t2 = static_cast<uint64_t>(esp_timer_get_time());

        lastRenderUs_ = static_cast<uint32_t>(t1 - t0);
        lastFlushUs_ = static_cast<uint32_t>(t2 - t1);
        lastDrawUs_ = nowUs;
        dirty_ = false;

        ++fpsFrameCount_;
        if ((nowUs - fpsWindowStartUs_) >= 1000000ULL) {
            currentFps_ = fpsFrameCount_;
            fpsFrameCount_ = 0;
            fpsWindowStartUs_ = nowUs;
        }
    }
}

} // namespace hydra::ui

#endif // HYDRA_UI_BACKEND_ST7789
