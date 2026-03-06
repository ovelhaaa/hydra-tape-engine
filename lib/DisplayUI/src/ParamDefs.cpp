#include "ParamDefs.h"
#include "DisplayUI.h"

// ============================================================================
// PARAMETER ARRAYS DEFINITIONS
// ============================================================================

// DELAY GROUP (4 params)
ParamDef DELAY_PARAMS[] = {
    {"Time",     "dly", PARAM_FLOAT_MS, 10.0f, 2000.0f, 10.0f, nullptr, "ms"},
    {"Feedback", "fbk", PARAM_FLOAT,    0.0f,  110.0f,  1.0f,  nullptr, "%"},  // 100%+ = self-oscillation
    {"Heads",    "hds", PARAM_INT,      1.0f,  7.0f,    1.0f,  nullptr, ""},
    {"Mix",      "mix", PARAM_FLOAT,    0.0f,  100.0f,  1.0f,  nullptr, "%"}, // Mix moved up
};
const int DELAY_PARAMS_COUNT = 4;

// TONE GROUP (9 params)
ParamDef TONE_PARAMS[] = {
    {"Speed",      "tps", PARAM_FLOAT, 1.0f, 100.0f, 1.0f, nullptr, "%"},
    {"Age",        "tpa", PARAM_FLOAT, 1.0f, 100.0f, 1.0f, nullptr, "%"},
    {"Drive",      "drv", PARAM_FLOAT, 0.0f, 100.0f, 1.0f, nullptr, "%"},
    {"Noise",      "nlv", PARAM_FLOAT, 0.0f, 100.0f, 1.0f, nullptr, "%"},
    {"Head Bump",  "hbp", PARAM_FLOAT, 0.0f, 100.0f, 1.0f, nullptr, "%"},
    {"Azimuth",    "azm", PARAM_FLOAT, 0.0f, 100.0f, 1.0f, nullptr, "%"},
    {"Dropout",    "dps", PARAM_FLOAT, 0.0f, 100.0f, 1.0f, nullptr, "%"},
    {"Tone",       "ton", PARAM_FLOAT, 0.0f, 100.0f, 1.0f, nullptr, "%"},
    {"Gtr Focus",  "gfc", PARAM_BOOL,  0.0f, 1.0f,   1.0f, nullptr, ""},
};
const int TONE_PARAMS_COUNT = 9;

// MODULATION GROUP (4 params)
ParamDef MOD_PARAMS[] = {
    {"Flutter D", "ftd", PARAM_FLOAT,    0.0f, 100.0f, 1.0f,  nullptr, "%"},
    {"Flutter R", "ftr", PARAM_FLOAT_HZ, 3.0f, 15.0f,  0.5f,  nullptr, "Hz"},  // Real Hz range
    {"Wow Depth", "wwd", PARAM_FLOAT,    0.0f, 100.0f, 1.0f,  nullptr, "%"},
    {"Wow Rate",  "wwr", PARAM_FLOAT_HZ, 0.3f, 3.0f,   0.1f,  nullptr, "Hz"},  // Real Hz range
};
const int MOD_PARAMS_COUNT = 4;

// MELODY GROUP (6 params)
ParamDef MELODY_PARAMS[] = {
    {"Waveform", "wvf", PARAM_ENUM,  0.0f, 3.0f,   1.0f, nullptr, ""},
    {"Scale",    "scl", PARAM_ENUM,  0.0f, 4.0f,   1.0f, nullptr, ""},
    {"Key",      "ptc", PARAM_INT,   24.0f, 96.0f, 1.0f, nullptr, ""},  // C1 to C7
    {"Mood",     "moo", PARAM_FLOAT, 0.0f, 100.0f, 1.0f, nullptr, "%"},
    {"Rhythm",   "rtm", PARAM_FLOAT, 0.0f, 100.0f, 1.0f, nullptr, "%"},
    {"ENO Mode", "eno", PARAM_BOOL,  0.0f, 1.0f,   1.0f, nullptr, ""},
};
const int MELODY_PARAMS_COUNT = 6;

// REVERB GROUP (4 params)
ParamDef REVERB_PARAMS[] = {
    {"Enable",  "spr", PARAM_BOOL,  0.0f, 1.0f,   1.0f, nullptr, ""},
    {"Mix",     "spm", PARAM_FLOAT, 0.0f, 100.0f, 5.0f, nullptr, "%"},
    {"Decay",   "spd", PARAM_FLOAT, 0.0f, 100.0f, 1.0f, nullptr, "%"},
    {"Damping", "spf", PARAM_FLOAT, 0.0f, 100.0f, 1.0f, nullptr, "%"},
};
const int REVERB_PARAMS_COUNT = 4;

// FRIPP GROUP (Frippertronics/Eno parameters)
ParamDef FRIPP_PARAMS[] = {
    {"Eno Mode",  "eno", PARAM_BOOL,     0.0f, 1.0f,     1.0f, nullptr, ""},     // Fripp=OFF, Eno=ON
    {"Delay A",   "fda", PARAM_FLOAT_MS, 1000.0f, 7000.0f, 100.0f, nullptr, "ms"},
    {"Delay B",   "fdb", PARAM_FLOAT_MS, 1000.0f, 11000.0f, 100.0f, nullptr, "ms"},
    {"Cross FB",  "xfb", PARAM_FLOAT,    0.0f, 100.0f,  5.0f, nullptr, "%"},
    {"Decay",     "fdc", PARAM_FLOAT,    0.0f, 100.0f,  5.0f, nullptr, "%"},
    {"Drift",     "fdr", PARAM_FLOAT,    0.0f, 100.0f,  5.0f, nullptr, "%"},     // Shimmer
};
const int FRIPP_PARAMS_COUNT = 6;

// BUBBLES GROUP (Chase Bliss-style reverse with artifacts)
ParamDef BUBBLES_PARAMS[] = {
    {"BPM",      "bbpm", PARAM_FLOAT_BPM, 40.0f, 200.0f,  5.0f, nullptr, ""},     // Slow = more artifacts
    {"Feedback", "bfb",  PARAM_FLOAT,     0.0f,  80.0f,   5.0f, nullptr, "%"},    // 0-80%
    {"Mix",      "bmx",  PARAM_FLOAT,     0.0f,  100.0f,  5.0f, nullptr, "%"},    // Wet mix
    {"LPF",      "blpf", PARAM_FLOAT_HZ,  500.0f, 4000.0f, 100.0f, nullptr, "Hz"}, // Feedback filter
    {"Allpass",  "bap",  PARAM_BOOL,      0.0f,  1.0f,    1.0f, nullptr, ""},     // Enable smearing
};
const int BUBBLES_PARAMS_COUNT = 5;

// FREEVERB GROUP (5 params - post-processing reverb)
ParamDef FREEVERB_PARAMS[] = {
    {"Enable",  "fve", PARAM_BOOL,  0.0f, 1.0f,    1.0f, nullptr, ""},     // Enable Freeverb
    {"Room",    "fvr", PARAM_FLOAT, 0.0f, 100.0f,  5.0f, nullptr, "%"},    // Room size (decay)
    {"Damping", "fvd", PARAM_FLOAT, 0.0f, 100.0f,  5.0f, nullptr, "%"},    // HF damping
    {"Wet",     "fvw", PARAM_FLOAT, 0.0f, 100.0f,  5.0f, nullptr, "%"},    // Wet level
    {"Width",   "fvs", PARAM_FLOAT, 0.0f, 100.0f,  5.0f, nullptr, "%"},    // Stereo width
};
const int FREEVERB_PARAMS_COUNT = 5;

// SETUP GROUP (3 params)
ParamDef SETUP_PARAMS[] = {
    {"Master Vol", "vol", PARAM_FLOAT, 0.0f, 300.0f, 5.0f, nullptr, "%"},
    {"Musical",    "mus", PARAM_BOOL,  0.0f, 1.0f,   1.0f, nullptr, ""},   // Sync to BPM divisions
    {"Delay Unit", "dun", PARAM_BOOL,  0.0f, 1.0f,   1.0f, nullptr, ""},   // 0=ms, 1=BPM
};
const int SETUP_PARAMS_COUNT = 3;

// ============================================================================
// Parameter pointer initialization
// Called from main.cpp setup() after globals are initialized
// ============================================================================
void initParamPointers() {
    // DELAY params
    DELAY_PARAMS[0].valuePtr = &p_delayTime;
    DELAY_PARAMS[1].valuePtr = &p_feedback;
    DELAY_PARAMS[2].valuePtr = &p_activeHeads;
    DELAY_PARAMS[3].valuePtr = &p_mix; // Index shifted because Musical moved
    
    // TONE params
    TONE_PARAMS[0].valuePtr = &p_tapeSpeed;
    TONE_PARAMS[1].valuePtr = &p_tapeAge;
    TONE_PARAMS[2].valuePtr = &p_drive;
    TONE_PARAMS[3].valuePtr = &p_noise;
    TONE_PARAMS[4].valuePtr = &p_headBump;
    TONE_PARAMS[5].valuePtr = &p_azimuth;
    TONE_PARAMS[6].valuePtr = &p_dropoutSeverity;
    TONE_PARAMS[7].valuePtr = &p_tone;
    TONE_PARAMS[8].valuePtr = &p_guitarFocus;
    
    // MODULATION params
    MOD_PARAMS[0].valuePtr = &p_flutterDepth;
    MOD_PARAMS[1].valuePtr = &p_flutterRate;
    MOD_PARAMS[2].valuePtr = &p_wowDepth;
    MOD_PARAMS[3].valuePtr = &p_wowRate;
    
    // MELODY params
    MELODY_PARAMS[0].valuePtr = &p_waveform;
    MELODY_PARAMS[1].valuePtr = &p_scale;
    MELODY_PARAMS[2].valuePtr = &p_pitch;
    MELODY_PARAMS[3].valuePtr = &p_mood;
    MELODY_PARAMS[4].valuePtr = &p_rhythm;
    MELODY_PARAMS[5].valuePtr = &p_enoMode;
    
    // REVERB params
    REVERB_PARAMS[0].valuePtr = &p_spring;
    REVERB_PARAMS[1].valuePtr = &p_springMix;
    REVERB_PARAMS[2].valuePtr = &p_springDecay;
    REVERB_PARAMS[3].valuePtr = &p_springDamping;
    
    // FRIPP params (point to globalFrippParams struct members)
    FRIPP_PARAMS[0].valuePtr = &globalFrippParams.enoMode;
    FRIPP_PARAMS[1].valuePtr = &globalFrippParams.delayTimeA;
    FRIPP_PARAMS[2].valuePtr = &globalFrippParams.delayTimeB;
    FRIPP_PARAMS[3].valuePtr = &globalFrippParams.crossFeedback;
    FRIPP_PARAMS[4].valuePtr = &globalFrippParams.decayRate;
    FRIPP_PARAMS[5].valuePtr = &globalFrippParams.driftAmount;
    
    // BUBBLES params (point to globalBubblesParams struct members)
    BUBBLES_PARAMS[0].valuePtr = &globalBubblesParams.bpm;
    BUBBLES_PARAMS[1].valuePtr = &globalBubblesParams.feedback;
    BUBBLES_PARAMS[2].valuePtr = &globalBubblesParams.mix;
    BUBBLES_PARAMS[3].valuePtr = &globalBubblesParams.feedbackLPF;
    BUBBLES_PARAMS[4].valuePtr = &globalBubblesParams.allpassEnabled;
    
    // FREEVERB params (point to globalFreeverbParams struct members)
    FREEVERB_PARAMS[0].valuePtr = &p_freeverbEnabled;
    FREEVERB_PARAMS[1].valuePtr = &globalFreeverbParams.roomSize;
    FREEVERB_PARAMS[2].valuePtr = &globalFreeverbParams.damping;
    FREEVERB_PARAMS[3].valuePtr = &globalFreeverbParams.wet;
    FREEVERB_PARAMS[4].valuePtr = &globalFreeverbParams.width;
    
    // SETUP params
    SETUP_PARAMS[0].valuePtr = &p_masterVol;
    SETUP_PARAMS[1].valuePtr = &p_headsMusical;
    SETUP_PARAMS[2].valuePtr = &p_delayUnitBpm;
    
    Serial.println("Parameter pointers initialized");
}

// ============================================================================
// Get params for a group
// ============================================================================
ParamDef* getParamsForGroup(int group, int* count) {
    switch (group) {
        case GROUP_DELAY:
            *count = DELAY_PARAMS_COUNT;
            return DELAY_PARAMS;
        case GROUP_TONE:
            *count = TONE_PARAMS_COUNT;
            return TONE_PARAMS;
        case GROUP_MODULATION:
            *count = MOD_PARAMS_COUNT;
            return MOD_PARAMS;
        case GROUP_MELODY:
            *count = MELODY_PARAMS_COUNT;
            return MELODY_PARAMS;
        case GROUP_REVERB:
            *count = REVERB_PARAMS_COUNT;
            return REVERB_PARAMS;
        case GROUP_FRIPP:
            *count = FRIPP_PARAMS_COUNT;
            return FRIPP_PARAMS;
        case GROUP_BUBBLES:
            *count = BUBBLES_PARAMS_COUNT;
            return BUBBLES_PARAMS;
        case GROUP_FREEVERB:
            *count = FREEVERB_PARAMS_COUNT;
            return FREEVERB_PARAMS;
        case GROUP_SETUP:
            *count = SETUP_PARAMS_COUNT;
            return SETUP_PARAMS;
        default:
            *count = 0;
            return nullptr;
    }
}

// ============================================================================
// Read value from parameter
// ============================================================================
float readParamValue(ParamDef* param) {
    if (param == nullptr || param->valuePtr == nullptr) return 0.0f;
    
    switch (param->type) {
        case PARAM_FLOAT:
        case PARAM_FLOAT_MS:
        case PARAM_FLOAT_HZ:
        case PARAM_FLOAT_BPM:
            return *((float*)param->valuePtr);
        case PARAM_INT:
        case PARAM_ENUM:
            return (float)*((int*)param->valuePtr);
        case PARAM_BOOL:
            return *((bool*)param->valuePtr) ? 1.0f : 0.0f;
        default:
            return 0.0f;
    }
}

// ============================================================================
// Write value to parameter
// ============================================================================
void writeParamValue(ParamDef* param, float value) {
    if (param == nullptr || param->valuePtr == nullptr) return;
    
    // Clamp to range
    value = constrain(value, param->minVal, param->maxVal);
    
    switch (param->type) {
        case PARAM_FLOAT:
        case PARAM_FLOAT_MS:
        case PARAM_FLOAT_HZ:
        case PARAM_FLOAT_BPM:
            *((float*)param->valuePtr) = value;
            break;
        case PARAM_INT:
        case PARAM_ENUM:
            *((int*)param->valuePtr) = (int)value;
            break;
        case PARAM_BOOL:
            *((bool*)param->valuePtr) = (value > 0.5f);
            break;
    }
    
    // Sync to audio processing
    syncGlobalParams();
}

// ============================================================================
// Format value for display
// ============================================================================
String formatParamValue(ParamDef* param, float value) {
    if (param == nullptr) return "---";
    
    switch (param->type) {
        case PARAM_FLOAT:
            return String((int)value) + param->unit;
            
        case PARAM_FLOAT_MS:
            // Check if this is delay time and BPM mode is on
            if (p_delayUnitBpm && strcmp(param->cli, "dly") == 0) {
                // Convert ms to BPM: BPM = 60000 / delay_ms
                float bpm = 60000.0f / value;
                return String((int)bpm) + "bpm";
            }
            return String((int)value) + "ms";
            
        case PARAM_FLOAT_HZ:
            return String(value, 1) + "Hz";
            
        case PARAM_FLOAT_BPM:
            return String((int)value) + "bpm";
            
        case PARAM_INT:
            // Special case for pitch (MIDI note)
            if (strcmp(param->cli, "ptc") == 0) {
                return midiToNote((int)value);
            }
            return String((int)value);
            
        case PARAM_ENUM:
            // Special cases for enums
            if (strcmp(param->cli, "wvf") == 0) {
                int idx = constrain((int)value, 0, 3);
                return String(WAVEFORM_NAMES[idx]);
            }
            if (strcmp(param->cli, "scl") == 0) {
                int idx = constrain((int)value, 0, 4);
                return String(SCALE_NAMES[idx]);
            }
            return String((int)value);
            
        case PARAM_BOOL:
            // Special case for Delay Unit: show BPM/ms instead of ON/OFF
            if (strcmp(param->cli, "dun") == 0) {
                return (value > 0.5f) ? "BPM" : "ms";
            }
            return (value > 0.5f) ? "ON" : "OFF";
            
        default:
            return String((int)value);
    }
}
