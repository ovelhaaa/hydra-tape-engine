#include "hydra_ui_catalog.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace hydra::ui {

namespace {

const char* const kReverseModeLabels[] = {"RHYTHM", "FREE", "LEGACY"};
const char* const kReverseDivLabels[] = {
    "1/4", "1/1", "1/2", "1/8", "1/16", "1/4.", "1/8.", "1/4T", "1/8T"
};
const char* const kWarpModeLabels[] = {"2T-HRM", "MODE 1", "MODE 2", "MODE 3"};
const char* const kTempoSourceLabels[] = {"INT", "TAP", "MIDI"};
const char* const kAudioSourceLabels[] = {"MP3", "SYNTH", "I2S IN"};
const char* const kWaveformLabels[] = {"SINE", "SAW", "TRI", "SQR"};
const char* const kScaleLabels[] = {"CHROM", "MAJOR", "MINOR", "PENTA", "BLUES"};
const char* const kNoteNames[] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

const UiPresetDesc kPresets[] = {
    {"clean",        "Clean",       "Pristine tape"},
    {"lofi",         "Lo-Fi",       "Aged & wobbly"},
    {"dub",          "Dub",         "Spacey echoes"},
    {"broken",       "Broken",      "Destroyed tape"},
    {"warp_octave",  "Warp Oct",    "+12st granular"},
    {"warp_cascade", "Warp Casc",   "Octave feedback"}
};

} // namespace

UiCatalog::UiCatalog() {
    auto set = [&](UiParamId id, int dspId, UiGroupId grp,
                   const char* lbl, const char* shrt, const char* unit,
                   ParamValueKind kind, float minV, float maxV, float step, float defV,
                   const char* const* enums = nullptr, uint8_t enumCnt = 0,
                   bool avail = true, bool accel = false) {
        UiParamDesc& d = params_[static_cast<size_t>(id)];
        d.id = id;
        d.dspParamId = dspId;
        d.group = grp;
        d.label = lbl;
        d.shortLabel = shrt;
        d.unit = unit;
        d.kind = kind;
        d.minVal = minV;
        d.maxVal = maxV;
        d.step = step;
        d.defaultVal = defV;
        d.enumLabels = enums;
        d.enumCount = enumCnt;
        d.available = avail;
        d.allowAccel = accel;
    };

    // --- DELAY ---
    set(UiParamId::DELAY_ACTIVE, HYDRA_DSP_PARAM_DELAY_ACTIVE, UiGroupId::DELAY,
        "Delay Active", "DLY", "", ParamValueKind::BOOL, 0.0f, 1.0f, 1.0f, 0.0f);
    set(UiParamId::DELAY_TIME_MS, HYDRA_DSP_PARAM_DELAY_MS, UiGroupId::DELAY,
        "Delay Time", "TIME", "ms", ParamValueKind::MS, 10.0f, 2000.0f, 10.0f, 500.0f, nullptr, 0, true, true);
    set(UiParamId::FEEDBACK, HYDRA_DSP_PARAM_FEEDBACK, UiGroupId::DELAY,
        "Feedback", "FB", "%", ParamValueKind::PERCENT, 0.0f, 100.0f, 1.0f, 40.0f, nullptr, 0, true, true);
    set(UiParamId::DRY_WET, HYDRA_DSP_PARAM_DRY_WET, UiGroupId::DELAY,
        "Mix (Dry/Wet)", "MIX", "%", ParamValueKind::PERCENT, 0.0f, 100.0f, 1.0f, 50.0f, nullptr, 0, true, true);
    set(UiParamId::ACTIVE_HEADS, HYDRA_DSP_PARAM_ACTIVE_HEADS, UiGroupId::DELAY,
        "Active Heads", "HEADS", "", ParamValueKind::HEADS_MASK, 1.0f, 7.0f, 1.0f, 4.0f);
    set(UiParamId::PING_PONG, HYDRA_DSP_PARAM_PING_PONG, UiGroupId::DELAY,
        "Ping Pong", "P.PONG", "", ParamValueKind::BOOL, 0.0f, 1.0f, 1.0f, 0.0f);
    set(UiParamId::FREEZE, HYDRA_DSP_PARAM_FREEZE, UiGroupId::DELAY,
        "Freeze", "FREEZE", "", ParamValueKind::BOOL, 0.0f, 1.0f, 1.0f, 0.0f);

    // --- FITA (TAPE) ---
    set(UiParamId::TAPE_SPEED, HYDRA_DSP_PARAM_TAPE_SPEED, UiGroupId::FITA,
        "Tape Speed", "SPEED", "%", ParamValueKind::PERCENT, 0.0f, 100.0f, 1.0f, 50.0f, nullptr, 0, true, true);
    set(UiParamId::TAPE_AGE, HYDRA_DSP_PARAM_TAPE_AGE, UiGroupId::FITA,
        "Tape Age", "AGE", "%", ParamValueKind::PERCENT, 0.0f, 100.0f, 1.0f, 40.0f, nullptr, 0, true, true);
    set(UiParamId::DRIVE, HYDRA_DSP_PARAM_DRIVE, UiGroupId::FITA,
        "Drive", "DRIVE", "%", ParamValueKind::PERCENT, 0.0f, 100.0f, 1.0f, 40.0f, nullptr, 0, true, true);
    set(UiParamId::NOISE, HYDRA_DSP_PARAM_NOISE, UiGroupId::FITA,
        "Tape Noise", "NOISE", "%", ParamValueKind::PERCENT, 0.0f, 100.0f, 1.0f, 30.0f, nullptr, 0, true, true);
    set(UiParamId::HEAD_BUMP, HYDRA_DSP_PARAM_HEAD_BUMP, UiGroupId::FITA,
        "Head Bump", "BUMP", "%", ParamValueKind::PERCENT, 0.0f, 100.0f, 1.0f, 30.0f, nullptr, 0, true, true);
    set(UiParamId::AZIMUTH, HYDRA_DSP_PARAM_AZIMUTH, UiGroupId::FITA,
        "Azimuth Err", "AZIM", "%", ParamValueKind::PERCENT, 0.0f, 100.0f, 1.0f, 10.0f, nullptr, 0, true, true);
    set(UiParamId::DROPOUT, HYDRA_DSP_PARAM_DROPOUT, UiGroupId::FITA,
        "Dropouts", "DROP", "%", ParamValueKind::PERCENT, 0.0f, 40.0f, 1.0f, 5.0f, nullptr, 0, true, false);
    set(UiParamId::TONE, HYDRA_DSP_PARAM_TONE, UiGroupId::FITA,
        "Tone", "TONE", "%", ParamValueKind::PERCENT, 0.0f, 100.0f, 1.0f, 50.0f, nullptr, 0, true, true);
    set(UiParamId::GUITAR_FOCUS, HYDRA_DSP_PARAM_GUITAR_FOCUS, UiGroupId::FITA,
        "Guitar Focus", "GTR", "", ParamValueKind::BOOL, 0.0f, 1.0f, 1.0f, 0.0f);

    // --- MOVIMENTO (MODULATION) ---
    set(UiParamId::FLUTTER_DEPTH, HYDRA_DSP_PARAM_FLUTTER_DEPTH, UiGroupId::MOVIMENTO,
        "Flutter Depth", "FLT D", "%", ParamValueKind::PERCENT, 0.0f, 45.0f, 1.0f, 14.0f, nullptr, 0, true, false);
    set(UiParamId::FLUTTER_RATE, HYDRA_DSP_PARAM_FLUTTER_RATE, UiGroupId::MOVIMENTO,
        "Flutter Rate", "FLT R", "Hz", ParamValueKind::HZ, 0.1f, 20.0f, 0.2f, 6.0f, nullptr, 0, true, false);
    set(UiParamId::WOW_DEPTH, HYDRA_DSP_PARAM_WOW_DEPTH, UiGroupId::MOVIMENTO,
        "Wow Depth", "WOW D", "%", ParamValueKind::PERCENT, 0.0f, 35.0f, 1.0f, 10.0f, nullptr, 0, true, false);
    set(UiParamId::WOW_RATE, HYDRA_DSP_PARAM_WOW_RATE, UiGroupId::MOVIMENTO,
        "Wow Rate", "WOW R", "Hz", ParamValueKind::HZ, 0.1f, 5.0f, 0.1f, 0.8f, nullptr, 0, true, false);

    // --- REVERSE ---
    set(UiParamId::REVERSE_ACTIVE, HYDRA_DSP_PARAM_REVERSE, UiGroupId::REVERSE,
        "Reverse", "REV", "", ParamValueKind::BOOL, 0.0f, 1.0f, 1.0f, 0.0f);
    set(UiParamId::REVERSE_MODE, HYDRA_DSP_PARAM_REVERSE_MODE, UiGroupId::REVERSE,
        "Rev Mode", "R.MOD", "", ParamValueKind::ENUM, 0.0f, 2.0f, 1.0f, 0.0f, kReverseModeLabels, 3);
    set(UiParamId::REVERSE_DIVISION, HYDRA_DSP_PARAM_REVERSE_DIVISION, UiGroupId::REVERSE,
        "Rev Division", "R.DIV", "", ParamValueKind::ENUM, 0.0f, 8.0f, 1.0f, 0.0f, kReverseDivLabels, 9);
    set(UiParamId::REVERSE_SMEAR, HYDRA_DSP_PARAM_REVERSE_SMEAR, UiGroupId::REVERSE,
        "Rev Smear", "SMEAR", "", ParamValueKind::BOOL, 0.0f, 1.0f, 1.0f, 0.0f);

    // --- WARP ---
    set(UiParamId::WARP_ACTIVE, HYDRA_DSP_PARAM_WARP_ACTIVE, UiGroupId::WARP,
        "Warp Active", "WARP", "", ParamValueKind::BOOL, 0.0f, 1.0f, 1.0f, 0.0f);
    set(UiParamId::WARP_SPEED, HYDRA_DSP_PARAM_WARP_SPEED, UiGroupId::WARP,
        "Warp Speed", "W.SPD", "x", ParamValueKind::FLOAT, 0.0f, 2.0f, 0.05f, 1.0f);
    set(UiParamId::WARP_GRAIN_MS, HYDRA_DSP_PARAM_WARP_GRAIN_MS, UiGroupId::WARP,
        "Warp Grain", "GRAIN", "ms", ParamValueKind::MS, 20.0f, 400.0f, 5.0f, 80.0f, nullptr, 0, true, true);
    set(UiParamId::WARP_STEREO, HYDRA_DSP_PARAM_WARP_STEREO, UiGroupId::WARP,
        "Warp Stereo", "W.STR", "", ParamValueKind::FLOAT, 0.0f, 1.0f, 0.05f, 0.5f);
    set(UiParamId::WARP_FEEDBACK, HYDRA_DSP_PARAM_WARP_FEEDBACK, UiGroupId::WARP,
        "Warp Feedback", "W.FB", "%", ParamValueKind::PERCENT, 0.0f, 100.0f, 1.0f, 0.0f, nullptr, 0, true, true);
    set(UiParamId::WARP_MODE, HYDRA_DSP_PARAM_WARP_MODE, UiGroupId::WARP,
        "Warp Mode", "W.MOD", "", ParamValueKind::ENUM, 0.0f, 3.0f, 1.0f, 0.0f, kWarpModeLabels, 4);

    // --- ESPACO (SPRING + HOST FREEVERB) ---
    set(UiParamId::SPRING_ACTIVE, HYDRA_DSP_PARAM_SPRING, UiGroupId::ESPACO,
        "Spring Reverb", "SPR", "", ParamValueKind::BOOL, 0.0f, 1.0f, 1.0f, 0.0f);
    set(UiParamId::SPRING_MIX, HYDRA_DSP_PARAM_SPRING_MIX, UiGroupId::ESPACO,
        "Spring Mix", "S.MIX", "%", ParamValueKind::PERCENT, 0.0f, 100.0f, 1.0f, 50.0f, nullptr, 0, true, true);
    set(UiParamId::SPRING_DECAY, HYDRA_DSP_PARAM_SPRING_DECAY, UiGroupId::ESPACO,
        "Spring Decay", "S.DEC", "%", ParamValueKind::PERCENT, 0.0f, 100.0f, 1.0f, 60.0f, nullptr, 0, true, true);
    set(UiParamId::SPRING_DAMPING, HYDRA_DSP_PARAM_SPRING_DAMPING, UiGroupId::ESPACO,
        "Spring Damp", "S.DMP", "%", ParamValueKind::PERCENT, 0.0f, 100.0f, 1.0f, 45.0f, nullptr, 0, true, true);
    set(UiParamId::FREEVERB_ACTIVE, -1, UiGroupId::ESPACO,
        "FreeVerb (Host)", "FV", "", ParamValueKind::BOOL, 0.0f, 1.0f, 1.0f, 0.0f);
    set(UiParamId::FREEVERB_ROOM, -1, UiGroupId::ESPACO,
        "FV Room", "FV.RM", "%", ParamValueKind::PERCENT, 0.0f, 100.0f, 5.0f, 50.0f);
    set(UiParamId::FREEVERB_DAMPING, -1, UiGroupId::ESPACO,
        "FV Damping", "FV.DP", "%", ParamValueKind::PERCENT, 0.0f, 100.0f, 5.0f, 50.0f);
    set(UiParamId::FREEVERB_WET, -1, UiGroupId::ESPACO,
        "FV Wet", "FV.WT", "%", ParamValueKind::PERCENT, 0.0f, 100.0f, 5.0f, 30.0f);
    set(UiParamId::FREEVERB_WIDTH, -1, UiGroupId::ESPACO,
        "FV Width", "FV.WD", "%", ParamValueKind::PERCENT, 0.0f, 100.0f, 5.0f, 100.0f);

    // --- TEMPO ---
    set(UiParamId::BPM, HYDRA_DSP_PARAM_BPM, UiGroupId::TEMPO,
        "BPM", "BPM", "bpm", ParamValueKind::BPM, 30.0f, 300.0f, 1.0f, 120.0f, nullptr, 0, true, true);
    set(UiParamId::ACTION_TAP_TEMPO, -1, UiGroupId::TEMPO,
        "Tap Tempo", "TAP", "", ParamValueKind::ACTION, 0.0f, 0.0f, 1.0f, 0.0f);
    set(UiParamId::HEADS_MUSICAL, HYDRA_DSP_PARAM_HEADS_MUSICAL, UiGroupId::TEMPO,
        "Heads Musical", "SYNC", "", ParamValueKind::BOOL, 0.0f, 1.0f, 1.0f, 0.0f);
    set(UiParamId::TEMPO_SOURCE, -1, UiGroupId::TEMPO,
        "Tempo Source", "T.SRC", "", ParamValueKind::ENUM, 0.0f, 2.0f, 1.0f, 0.0f, kTempoSourceLabels, 3);
    set(UiParamId::DELAY_UNIT_BPM, -1, UiGroupId::TEMPO,
        "Delay Display", "UNIT", "", ParamValueKind::BOOL, 0.0f, 1.0f, 1.0f, 0.0f);

    // --- MODO (Semantic mode commands) ---
    set(UiParamId::MODE_TAPE_DELAY, -1, UiGroupId::MODO,
        "Tape Delay", "DELAY", "", ParamValueKind::ACTION, 0.0f, 0.0f, 1.0f, 0.0f);
    set(UiParamId::MODE_TAPE_SATURATOR, -1, UiGroupId::MODO,
        "Tape Saturator", "SATUR", "", ParamValueKind::ACTION, 0.0f, 0.0f, 1.0f, 0.0f);
    set(UiParamId::MODE_REVERSE, -1, UiGroupId::MODO,
        "Reverse", "REV", "", ParamValueKind::ACTION, 0.0f, 0.0f, 1.0f, 0.0f);
    set(UiParamId::MODE_WARP, -1, UiGroupId::MODO,
        "Warp", "WARP", "", ParamValueKind::ACTION, 0.0f, 0.0f, 1.0f, 0.0f);
    set(UiParamId::MODE_REVERSE_WARP, -1, UiGroupId::MODO,
        "Reverse + Warp", "R+WRP", "", ParamValueKind::ACTION, 0.0f, 0.0f, 1.0f, 0.0f);
    set(UiParamId::HOST_FRIPP_MODE, -1, UiGroupId::MODO,
        "Fripp (Host)", "FRIPP", "", ParamValueKind::BOOL, 0.0f, 1.0f, 1.0f, 0.0f);
    set(UiParamId::HOST_BUBBLES_MODE, -1, UiGroupId::MODO,
        "Bubbles (Host)", "BUBBL", "", ParamValueKind::BOOL, 0.0f, 1.0f, 1.0f, 0.0f);

    // --- SISTEMA ---
    set(UiParamId::SW_BYPASS, -1, UiGroupId::SISTEMA,
        "SW Bypass", "BYP", "", ParamValueKind::BOOL, 0.0f, 1.0f, 1.0f, 0.0f);
    set(UiParamId::MASTER_VOL, -1, UiGroupId::SISTEMA,
        "Master Vol", "M.VOL", "%", ParamValueKind::PERCENT, 0.0f, 300.0f, 5.0f, 30.0f, nullptr, 0, true, true);
    set(UiParamId::AUDIO_SOURCE, -1, UiGroupId::SISTEMA,
        "Audio Source", "SRC", "", ParamValueKind::ENUM, 0.0f, 2.0f, 1.0f, 1.0f, kAudioSourceLabels, 3);
    set(UiParamId::GEN_ACTIVE, -1, UiGroupId::SISTEMA,
        "Synth Gen", "GEN", "", ParamValueKind::BOOL, 0.0f, 1.0f, 1.0f, 1.0f);
    set(UiParamId::MELODY_WAVEFORM, -1, UiGroupId::SISTEMA,
        "Gen Waveform", "WAVE", "", ParamValueKind::ENUM, 0.0f, 3.0f, 1.0f, 1.0f, kWaveformLabels, 4);
    set(UiParamId::MELODY_SCALE, -1, UiGroupId::SISTEMA,
        "Gen Scale", "SCALE", "", ParamValueKind::ENUM, 0.0f, 4.0f, 1.0f, 3.0f, kScaleLabels, 5);
    set(UiParamId::MELODY_KEY, -1, UiGroupId::SISTEMA,
        "Gen Key (MIDI)", "KEY", "", ParamValueKind::INT, 24.0f, 96.0f, 1.0f, 60.0f);
    set(UiParamId::MELODY_MOOD, -1, UiGroupId::SISTEMA,
        "Gen Mood", "MOOD", "%", ParamValueKind::PERCENT, 0.0f, 100.0f, 5.0f, 50.0f);
    set(UiParamId::MELODY_RHYTHM, -1, UiGroupId::SISTEMA,
        "Gen Rhythm", "RHYTH", "%", ParamValueKind::PERCENT, 0.0f, 100.0f, 5.0f, 50.0f);
    set(UiParamId::MELODY_ENO, -1, UiGroupId::SISTEMA,
        "Gen Eno Mode", "ENO", "", ParamValueKind::BOOL, 0.0f, 1.0f, 1.0f, 0.0f);
    set(UiParamId::ACTION_RESET_DSP, HYDRA_DSP_PARAM_RESET, UiGroupId::SISTEMA,
        "Reset DSP Buffer", "RESET", "", ParamValueKind::ACTION, 0.0f, 0.0f, 1.0f, 0.0f);
}

void UiCatalog::syncFromDspSpecs(const hydra_dsp_param_spec* specs,
                                 uint32_t count,
                                 float runtimeMaxDelayMs) {
    if (!specs || count == 0) return;

    for (size_t i = 0; i < static_cast<size_t>(UiParamId::COUNT); ++i) {
        UiParamDesc& d = params_[i];
        if (d.dspParamId < 0) continue;
        for (uint32_t s = 0; s < count; ++s) {
            if (static_cast<int>(specs[s].id) == d.dspParamId) {
                d.minVal = specs[s].min_value;
                d.maxVal = specs[s].max_value;
                if ((specs[s].flags & HYDRA_DSP_PARAM_SPEC_FLAG_RUNTIME_MAX) != 0 &&
                    runtimeMaxDelayMs > d.minVal) {
                    d.maxVal = runtimeMaxDelayMs;
                }
                d.defaultVal = std::max(d.minVal, std::min(d.maxVal, specs[s].default_value));
                break;
            }
        }
    }
}

const UiParamDesc& UiCatalog::get(UiParamId id) const {
    return params_[static_cast<size_t>(id)];
}

UiParamDesc& UiCatalog::getMutable(UiParamId id) {
    return params_[static_cast<size_t>(id)];
}

size_t UiCatalog::getGroupParams(UiGroupId group, UiParamId* outIds, size_t maxOut) const {
    size_t n = 0;
    for (size_t i = 0; i < static_cast<size_t>(UiParamId::COUNT); ++i) {
        if (params_[i].group == group) {
            if (outIds && n < maxOut) {
                outIds[n] = params_[i].id;
            }
            ++n;
        }
    }
    return n;
}

const char* UiCatalog::groupName(UiGroupId group) {
    switch (group) {
        case UiGroupId::MODO:      return "Modo";
        case UiGroupId::DELAY:     return "Delay";
        case UiGroupId::FITA:      return "Fita";
        case UiGroupId::MOVIMENTO: return "Movimento";
        case UiGroupId::REVERSE:   return "Reverse";
        case UiGroupId::WARP:      return "Warp";
        case UiGroupId::ESPACO:    return "Espaco";
        case UiGroupId::TEMPO:     return "Tempo";
        case UiGroupId::PRESETS:   return "Presets";
        case UiGroupId::SISTEMA:   return "Sistema";
        default:                   return "?";
    }
}

const char* UiCatalog::semanticModeName(SemanticModeId mode) {
    switch (mode) {
        case SemanticModeId::TAPE_DELAY:     return "TAPE DELAY";
        case SemanticModeId::TAPE_SATURATOR: return "SATURATOR";
        case SemanticModeId::REVERSE:        return "REVERSE";
        case SemanticModeId::WARP:           return "WARP";
        case SemanticModeId::REVERSE_WARP:   return "REV+WARP";
        default:                             return "TAPE";
    }
}

const char* UiCatalog::semanticModeShort(SemanticModeId mode) {
    switch (mode) {
        case SemanticModeId::TAPE_DELAY:     return "TAPE DLY";
        case SemanticModeId::TAPE_SATURATOR: return "SATURATE";
        case SemanticModeId::REVERSE:        return "REVERSE";
        case SemanticModeId::WARP:           return "WARP";
        case SemanticModeId::REVERSE_WARP:   return "REV+WARP";
        default:                             return "TAPE";
    }
}

size_t UiCatalog::presetCount() {
    return sizeof(kPresets) / sizeof(kPresets[0]);
}

const UiPresetDesc& UiCatalog::presetAt(size_t idx) {
    if (idx >= presetCount()) return kPresets[0];
    return kPresets[idx];
}

void UiCatalog::formatValue(const UiParamDesc& desc,
                            float value,
                            char* outBuf,
                            size_t outBufSize,
                            bool delayUnitBpm) {
    if (!outBuf || outBufSize == 0) return;

    switch (desc.kind) {
        case ParamValueKind::BOOL:
            if (desc.id == UiParamId::DELAY_UNIT_BPM) {
                std::snprintf(outBuf, outBufSize, "%s", (value > 0.5f) ? "BPM" : "ms");
            } else {
                std::snprintf(outBuf, outBufSize, "%s", (value > 0.5f) ? "ON" : "OFF");
            }
            break;

        case ParamValueKind::ENUM: {
            int idx = static_cast<int>(value + 0.5f);
            if (idx < 0) idx = 0;
            if (desc.enumLabels && idx < desc.enumCount) {
                std::snprintf(outBuf, outBufSize, "%s", desc.enumLabels[idx]);
            } else {
                std::snprintf(outBuf, outBufSize, "%d", idx);
            }
            break;
        }

        case ParamValueKind::HEADS_MASK: {
            int mask = static_cast<int>(value + 0.5f);
            if (mask < 1) mask = 1;
            if (mask > 7) mask = 7;
            std::snprintf(outBuf, outBufSize, "%c %c %c",
                          (mask & 1) ? 'A' : '-',
                          (mask & 2) ? 'B' : '-',
                          (mask & 4) ? 'C' : '-');
            break;
        }

        case ParamValueKind::MS:
            if (delayUnitBpm && desc.id == UiParamId::DELAY_TIME_MS && value > 1.0f) {
                float bpm = 60000.0f / value;
                std::snprintf(outBuf, outBufSize, "%.0f bpm", bpm);
            } else {
                std::snprintf(outBuf, outBufSize, "%.0f ms", value);
            }
            break;

        case ParamValueKind::PERCENT:
            std::snprintf(outBuf, outBufSize, "%.0f%%", value);
            break;

        case ParamValueKind::HZ:
            std::snprintf(outBuf, outBufSize, "%.1f Hz", value);
            break;

        case ParamValueKind::BPM:
            std::snprintf(outBuf, outBufSize, "%.1f", value);
            break;

        case ParamValueKind::FLOAT:
            std::snprintf(outBuf, outBufSize, "%.2f%s", value, desc.unit ? desc.unit : "");
            break;

        case ParamValueKind::INT: {
            int iv = static_cast<int>(value + 0.5f);
            if (desc.id == UiParamId::MELODY_KEY) {
                int octave = (iv / 12) - 1;
                int note = ((iv % 12) + 12) % 12;
                std::snprintf(outBuf, outBufSize, "%s%d", kNoteNames[note], octave);
            } else {
                std::snprintf(outBuf, outBufSize, "%d", iv);
            }
            break;
        }

        case ParamValueKind::ACTION:
            std::snprintf(outBuf, outBufSize, "[ENTER]");
            break;

        case ParamValueKind::UNAVAILABLE:
        default:
            std::snprintf(outBuf, outBufSize, "N/A");
            break;
    }
}

} // namespace hydra::ui
