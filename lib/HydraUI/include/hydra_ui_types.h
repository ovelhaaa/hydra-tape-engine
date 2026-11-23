#pragma once

#include <cstdint>
#include <cstddef>

namespace hydra::ui {

// ============================================================================
// INPUT EVENTS (Display-independent)
// ============================================================================
enum class UiEventType : uint8_t {
    NONE = 0,
    ROTATE,        // delta != 0 (+CW, -CCW)
    CLICK,         // Short press release
    LONG_PRESS,    // Held >= long_press_ms
    POT_CHANGED,   // Future analog pot (potId, normValue 0..1)
    FOOTSWITCH,    // Future footswitch (switchId, pressed)
    PARAM_CHANGED  // External parameter/snapshot change notification
};

struct UiEvent {
    UiEventType type = UiEventType::NONE;
    int32_t delta = 0;          // For ROTATE
    uint8_t controlId = 0;      // For POT_CHANGED / FOOTSWITCH
    float normValue = 0.0f;     // For POT_CHANGED (0.0..1.0)
    bool pressed = false;       // For FOOTSWITCH
    uint64_t timestampUs = 0;   // Monotonic timestamp for Tap Tempo
};

// ============================================================================
// SCREENS & FOCUS
// ============================================================================
enum class UiScreen : uint8_t {
    SPLASH = 0,
    HOME,
    MENU,
    PARAM_LIST,
    PARAM_EDIT,
    TAP_TEMPO,
    PRESETS
};

enum class HomeFocus : uint8_t {
    TIME = 0,
    FB   = 1,
    MIX  = 2,
    MENU = 3,
    COUNT = 4
};

// ============================================================================
// MENU GROUPS (Exact 10 groups specified for Fase UI-01)
// ============================================================================
enum class UiGroupId : uint8_t {
    MODO = 0,
    DELAY,
    FITA,
    MOVIMENTO,
    REVERSE,
    WARP,
    ESPACO,
    TEMPO,
    PRESETS,
    SISTEMA,
    COUNT
};

// ============================================================================
// SEMANTIC MODES (Mapped to DSP parameter combinations without clobbering)
// ============================================================================
enum class SemanticModeId : uint8_t {
    TAPE_DELAY = 0,
    TAPE_SATURATOR,
    REVERSE,
    WARP,
    REVERSE_WARP,
    COUNT
};

// ============================================================================
// PARAMETER IDENTIFIERS (Covers all hydra_dsp_param_id + host controls)
// ============================================================================
enum class UiParamId : uint16_t {
    // Delay group
    DELAY_ACTIVE = 0,
    DELAY_TIME_MS,
    FEEDBACK,
    DRY_WET,
    ACTIVE_HEADS,
    PING_PONG,
    FREEZE,

    // Fita (Tape) group
    TAPE_SPEED,
    TAPE_AGE,
    DRIVE,
    NOISE,
    HEAD_BUMP,
    AZIMUTH,
    DROPOUT,
    TONE,
    GUITAR_FOCUS,

    // Movimento (Modulation) group
    FLUTTER_DEPTH,
    FLUTTER_RATE,
    WOW_DEPTH,
    WOW_RATE,

    // Reverse group
    REVERSE_ACTIVE,
    REVERSE_MODE,
    REVERSE_DIVISION,
    REVERSE_SMEAR,

    // Warp group
    WARP_ACTIVE,
    WARP_SPEED,
    WARP_GRAIN_MS,
    WARP_STEREO,
    WARP_FEEDBACK,
    WARP_MODE,

    // Espaco (Space / Reverb) group
    SPRING_ACTIVE,
    SPRING_MIX,
    SPRING_DECAY,
    SPRING_DAMPING,
    FREEVERB_ACTIVE,
    FREEVERB_ROOM,
    FREEVERB_DAMPING,
    FREEVERB_WET,
    FREEVERB_WIDTH,

    // Tempo group
    BPM,
    ACTION_TAP_TEMPO,
    HEADS_MUSICAL,
    TEMPO_SOURCE,
    DELAY_UNIT_BPM,

    // Modo group (semantic actions)
    MODE_TAPE_DELAY,
    MODE_TAPE_SATURATOR,
    MODE_REVERSE,
    MODE_WARP,
    MODE_REVERSE_WARP,
    HOST_FRIPP_MODE,
    HOST_BUBBLES_MODE,

    // Sistema group
    SW_BYPASS,
    MASTER_VOL,
    AUDIO_SOURCE,
    GEN_ACTIVE,
    MELODY_WAVEFORM,
    MELODY_SCALE,
    MELODY_KEY,
    MELODY_MOOD,
    MELODY_RHYTHM,
    MELODY_ENO,
    ACTION_RESET_DSP,

    COUNT
};

// ============================================================================
// COMMANDS EMITTED BY UI MODEL TO BRIDGE/ADAPTER
// ============================================================================
enum class UiCommandType : uint8_t {
    NONE = 0,
    SET_PARAM,
    APPLY_MODE,
    TOGGLE_BYPASS,
    TAP_TEMPO,
    LOAD_PRESET,
    RESET_DSP
};

struct UiCommand {
    UiCommandType type = UiCommandType::NONE;
    UiParamId paramId = UiParamId::DELAY_TIME_MS;
    float value = 0.0f;
    SemanticModeId modeId = SemanticModeId::TAPE_DELAY;
    uint8_t presetIndex = 0;
    uint64_t timestampUs = 0;
};

// ============================================================================
// FUTURE PERIPHERAL ADAPTER STATE (Soft takeover / pickup for pots)
// ============================================================================
struct PotPickupState {
    bool initialized = false;
    bool latched = true;
    float lastRawNorm = 0.0f;
    float targetNorm = 0.0f;
};

} // namespace hydra::ui
