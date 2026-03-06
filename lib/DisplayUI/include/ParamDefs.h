#ifndef PARAM_DEFS_H
#define PARAM_DEFS_H

#include <Arduino.h>

// ============================================================================
// PARAMETER DEFINITION STRUCTURE
// Maps UI elements to actual global variables
// ============================================================================

enum ParamType {
    PARAM_FLOAT,      // 0.0 - 1.0 range (percentage)
    PARAM_FLOAT_MS,   // Milliseconds (10 - 2000)
    PARAM_FLOAT_HZ,   // Hertz (0.1 - 20)
    PARAM_FLOAT_BPM,  // BPM (30 - 300)
    PARAM_INT,        // Integer value
    PARAM_BOOL,       // ON/OFF toggle
    PARAM_ENUM        // Selection from list
};

struct ParamDef {
    const char* name;       // Display name
    const char* cli;        // CLI command
    ParamType type;         // Value type
    float minVal;           // Minimum value
    float maxVal;           // Maximum value
    float step;             // Increment step
    void* valuePtr;         // Pointer to actual variable
    const char* unit;       // Unit string (%, ms, Hz, etc.)
};

// ============================================================================
// EXTERNAL PARAMETER REFERENCES (defined in main.cpp)
// ============================================================================
extern float p_delayTime;
extern float p_feedback;
extern int p_activeHeads;
extern bool p_headsMusical;

extern float p_tapeSpeed;
extern float p_tapeAge;
extern float p_drive;
extern float p_noise;
extern float p_headBump;
extern float p_azimuth;
extern float p_dropoutSeverity;
extern float p_tone;
extern bool p_guitarFocus;

extern float p_flutterDepth;
extern float p_flutterRate;
extern float p_wowDepth;
extern float p_wowRate;

extern int p_waveform;
extern int p_scale;
extern int p_pitch;
extern float p_mood;
extern float p_rhythm;
extern bool p_enoMode;

extern bool p_spring;
extern float p_springMix;
extern float p_springDecay;
extern float p_springDamping;

extern bool p_freeze;
extern bool p_reverse;
extern bool p_reverseSmear;
extern bool p_delayActive;
extern float p_bpm;
extern float p_mix;
extern float p_masterVol;
extern float masterVolume;
extern bool p_delayUnitBpm;  // false=ms, true=BPM

// Frippertronics mode
extern bool p_frippMode;
#include "TapeDelay.h"  // For FrippParams and FrippEngine
extern FrippEngine* fripp;
extern FrippParams globalFrippParams;

// ============================================================================
// PARAMETER ARRAYS BY GROUP (declared extern, defined in ParamDefs.cpp)
// ============================================================================

extern ParamDef DELAY_PARAMS[];
extern const int DELAY_PARAMS_COUNT;

extern ParamDef TONE_PARAMS[];
extern const int TONE_PARAMS_COUNT;

extern ParamDef MOD_PARAMS[];
extern const int MOD_PARAMS_COUNT;

extern ParamDef MELODY_PARAMS[];
extern const int MELODY_PARAMS_COUNT;

extern ParamDef REVERB_PARAMS[];
extern const int REVERB_PARAMS_COUNT;

extern ParamDef FRIPP_PARAMS[];
extern const int FRIPP_PARAMS_COUNT;

extern ParamDef BUBBLES_PARAMS[];
extern const int BUBBLES_PARAMS_COUNT;

// Bubbles mode state
extern bool p_bubblesMode;
class BubblesEngine;  // Forward declaration
extern BubblesEngine* bubbles;
struct BubblesParams;
extern BubblesParams globalBubblesParams;

// Freeverb state (post-processing reverb)
extern ParamDef FREEVERB_PARAMS[];
extern const int FREEVERB_PARAMS_COUNT;
extern bool p_freeverbEnabled;
class FreeverbEngine;  // Forward declaration
extern FreeverbEngine* freeverb;
struct FreeverbParams;
extern FreeverbParams globalFreeverbParams;

extern ParamDef SETUP_PARAMS[];
extern const int SETUP_PARAMS_COUNT;

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

// Initialize parameter pointers (call from setup)
void initParamPointers();

// Get param array for a group
ParamDef* getParamsForGroup(int group, int* count);

// Format value for display
String formatParamValue(ParamDef* param, float value);

// Read current value from parameter
float readParamValue(ParamDef* param);

// Write value to parameter (also syncs to audio)
void writeParamValue(ParamDef* param, float value);

// Sync all p_ variables to globalParams (defined in main.cpp)
extern void syncGlobalParams();

#endif // PARAM_DEFS_H
