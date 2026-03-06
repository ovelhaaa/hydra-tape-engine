#ifndef DISPLAY_UI_H
#define DISPLAY_UI_H

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "hardware_pins.h"

// ============================================================================
// SCREEN STATE MACHINE
// ============================================================================
enum ScreenState {
    SCREEN_MODE_SELECT,    // Home: Select effect mode
    SCREEN_GROUP_SELECT,   // Select parameter group
    SCREEN_PARAM_LIST,     // List parameters with values
    SCREEN_PARAM_EDIT,     // Edit single parameter
    SCREEN_PRESETS,        // Preset selection
    SCREEN_SPLASH          // Boot splash
};

// ============================================================================
// EFFECT MODES
// ============================================================================
enum EffectMode {
    MODE_SATURATOR = 0,
    MODE_DELAY,
    MODE_REVERB,
    MODE_REVERSE,
    MODE_REV_REVERB,
    MODE_FREEZE,
    MODE_FRIPPERTRONICS,
    MODE_BUBBLES,         // Chase Bliss-style reverse with artifacts
    MODE_COUNT
};

// ============================================================================
// PARAMETER GROUPS
// ============================================================================
enum ParamGroup {
    GROUP_DELAY = 0,
    GROUP_TONE,
    GROUP_MODULATION,
    GROUP_MELODY,
    GROUP_REVERB,
    GROUP_FRIPP,      // Frippertronics/Eno parameters
    GROUP_BUBBLES,    // Bubbles effect parameters
    GROUP_FREEVERB,   // Freeverb (post-processing reverb)
    GROUP_SETUP,
    GROUP_PRESETS,
    GROUP_COUNT
};

// ============================================================================
// INPUT EVENTS
// ============================================================================
enum InputEvent {
    INPUT_NONE = 0,
    INPUT_ROTATE_CW,      // Clockwise rotation
    INPUT_ROTATE_CCW,     // Counter-clockwise rotation
    INPUT_CLICK,          // Encoder button click
    INPUT_LONG_PRESS,     // Encoder button long press
    INPUT_AUX_CLICK       // Auxiliary button click
};

// ============================================================================
// SCALE NAMES (as requested)
// ============================================================================
static const char* SCALE_NAMES[] = {
    "Chromatic",
    "Major",
    "Minor",
    "Pentatonic",
    "Blues"
};

// ============================================================================
// NOTE NAMES (MIDI to note string, as requested)
// ============================================================================
static const char* NOTE_NAMES[] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

// Helper: Convert MIDI note to string like "C4", "D#3"
inline String midiToNote(int midi) {
    int octave = (midi / 12) - 1;
    int note = midi % 12;
    return String(NOTE_NAMES[note]) + String(octave);
}

// ============================================================================
// WAVEFORM NAMES
// ============================================================================
static const char* WAVEFORM_NAMES[] = {
    "Sine",
    "Saw",
    "Triangle",
    "Square"
};

// ============================================================================
// MODE NAMES
// ============================================================================
static const char* MODE_NAMES[] = {
    "SATURATOR",
    "DELAY",
    "REVERB",
    "REVERSE",
    "REV REVERB",
    "FREEZE",
    "FRIPP...",
    "BUBBLES"    // Chase Bliss-style
};

// ============================================================================
// GROUP NAMES
// ============================================================================
static const char* GROUP_NAMES[] = {
    "Delay",
    "Tone",
    "Modulation",
    "Melody",
    "Reverb",
    "Fripp",       // Frippertronics/Eno
    "Bubbles",     // Chase Bliss-style
    "FreeVerb",    // Post-processing reverb
    "Setup",
    "Presets"
};

// ============================================================================
// PRESET DEFINITIONS
// ============================================================================
struct PresetDef {
    const char* name;
    const char* description;
};

static const PresetDef PRESETS[] = {
    {"Clean",    "Pristine tape"},
    {"Lo-Fi",    "Aged & wobbly"},
    {"Dub",      "Spacey echoes"},
    {"Broken",   "Destroyed tape"},
    {"Ambient",  "Long tails"},
    {"Slapback", "50s rock echo"},
    {"Shoegaze", "Washy & modulated"},
    {"ENO",      "Generative ambient"}
};
static const int PRESET_COUNT = sizeof(PRESETS) / sizeof(PresetDef);

// ============================================================================
// DISPLAY UI CLASS
// ============================================================================
class DisplayUI {
public:
    DisplayUI();
    
    // Lifecycle
    bool begin();
    void update();
    
    // Input handling
    void handleInput(InputEvent event);
    void setEncoderDelta(int delta);
    
    // State
    ScreenState getScreen() const { return currentScreen; }
    EffectMode getMode() const { return currentMode; }
    
    // Dirty flag for efficient rendering
    void markDirty() { needsRedraw = true; }
    bool isDirty() const { return needsRedraw; }

private:
    // Display driver
    Adafruit_SSD1306* display;
    
    // State machine
    ScreenState currentScreen;
    EffectMode currentMode;
    ParamGroup currentGroup;
    
    // Menu cursor positions
    int modeMenuCursor;
    int groupMenuCursor;
    int paramMenuCursor;
    int presetMenuCursor;
    
    // Edit state
    int editParamIndex;
    float editValue;
    
    // Rendering
    bool needsRedraw;
    uint32_t lastDrawTime;
    
    // Scroll offset for long lists
    int scrollOffset;
    
    // Animation state
    uint8_t splashAnimFrame;
    
    // Screen renderers
    void renderSplash();
    void renderModeSelect();
    void renderGroupSelect();
    void renderParamList();
    void renderParamEdit();
    void renderPresets();
    
    // UI helpers
    void drawHeader(const char* title);
    void drawMenuItem(int y, const char* text, bool selected);
    void drawProgressBar(int x, int y, int w, int h, float value);
    void drawIcon(int x, int y, uint8_t iconIndex);
    
    // Navigation helpers
    void goBack();
    void enterSelected();
    void activateMode(EffectMode mode);
    int getVisibleModeCount();
    int getVisibleGroupCount();
    int getParamCount();
    int getAvailableGroups(ParamGroup* outGroups);
    ParamGroup getGroupAtIndex(int index);
};

// ============================================================================
// ENCODER HANDLER (Interrupt-based)
// ============================================================================
class EncoderHandler {
public:
    static void begin();
    static int getDelta();      // Returns accumulated delta since last call
    static bool getClick();     // Returns true if clicked (clears flag)
    static bool getLongPress(); // Returns true if long pressed
    static bool getAuxClick();  // Returns true if aux button clicked
    
private:
    static volatile bool clicked;
    static volatile bool longPressed;
    static volatile bool auxClicked;
    static volatile uint32_t buttonDownTime;
    
    static void IRAM_ATTR buttonISR();
    static void IRAM_ATTR auxButtonISR();
};

#endif // DISPLAY_UI_H
