#include "DisplayUI.h"
#include <driver/pulse_cnt.h>
#include <driver/gpio.h>
#include "ParamDefs.h"
#include "TapeDelay.h"  // For FrippEngine class

// ============================================================================
// ENCODER HANDLER STATIC MEMBERS
// ============================================================================
static pcnt_unit_handle_t pcnt_unit = NULL;
// volatile int EncoderHandler::encoderCount = 0; // REMOVED
volatile bool EncoderHandler::clicked = false;
volatile bool EncoderHandler::longPressed = false;
volatile bool EncoderHandler::auxClicked = false;
volatile uint32_t EncoderHandler::buttonDownTime = 0;

// ============================================================================
// ENCODER INTERRUPTS
// ============================================================================
// encoderISR REMOVED - Using Hardware PCNT

void IRAM_ATTR EncoderHandler::buttonISR() {
    static uint32_t lastDebounce = 0;
    uint32_t now = millis();
    
    if (now - lastDebounce < DEBOUNCE_MS) return;
    lastDebounce = now;
    
    if (digitalRead(ENCODER_SW_PIN) == LOW) {
        buttonDownTime = now;
    } else {
        if (buttonDownTime > 0) {
            uint32_t duration = now - buttonDownTime;
            if (duration >= LONG_PRESS_MS) {
                longPressed = true;
            } else {
                clicked = true;
            }
            buttonDownTime = 0;
        }
    }
}

void IRAM_ATTR EncoderHandler::auxButtonISR() {
    static uint32_t lastDebounce = 0;
    uint32_t now = millis();
    
    if (now - lastDebounce < DEBOUNCE_MS) return;
    lastDebounce = now;
    
    if (digitalRead(AUX_BUTTON_PIN) == LOW) {
        auxClicked = true;
    }
}

// ============================================================================
// ENCODER HANDLER METHODS
// ============================================================================
void EncoderHandler::begin() {
    // Button Pins
    pinMode(ENCODER_SW_PIN, INPUT_PULLUP);
    pinMode(AUX_BUTTON_PIN, INPUT_PULLUP);
    
    // Hardware Encoder Setup (Native ESP-IDF PCNT)
    pcnt_unit_config_t unit_config = {};
    unit_config.high_limit = 10000;
    unit_config.low_limit = -10000;
    
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_config, &pcnt_unit));

    pcnt_glitch_filter_config_t filter_config = {
        .max_glitch_ns = 1000, 
    };
    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(pcnt_unit, &filter_config));

    pcnt_chan_config_t chan_config = {
        .edge_gpio_num = ENCODER_DT_PIN,
        .level_gpio_num = ENCODER_CLK_PIN,
    };
    pcnt_channel_handle_t pcnt_chan = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_config, &pcnt_chan));

    // Quadrature decoding
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(pcnt_chan, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

    // Second channel for full quadrature (optional but better)
    pcnt_chan_config_t chan_config2 = {
        .edge_gpio_num = ENCODER_CLK_PIN,
        .level_gpio_num = ENCODER_DT_PIN,
    };
    pcnt_channel_handle_t pcnt_chan2 = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_config2, &pcnt_chan2));
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan2, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(pcnt_chan2, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

    ESP_ERROR_CHECK(pcnt_unit_enable(pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_start(pcnt_unit));

    // Button Interrupts (Still needed)
    // attachInterrupt(digitalPinToInterrupt(ENCODER_CLK_PIN), encoderISR, CHANGE); // REMOVED
    attachInterrupt(digitalPinToInterrupt(ENCODER_SW_PIN), buttonISR, CHANGE);
    attachInterrupt(digitalPinToInterrupt(AUX_BUTTON_PIN), auxButtonISR, FALLING);
}

int EncoderHandler::getDelta() {
    static int lastCount = 0;
    int count = 0;
    pcnt_unit_get_count(pcnt_unit, &count);
    
    // Scale down by 2 or 4? Native PCNT counts edges.
    // KY-040 usually 4 edges per click (Quadrature).
    // Let's divide by 2 for smoother feel if needed, or keeping raw for now.
    // User response "bizarre" suggests missing steps or double steps.
    // Let's return raw diff first, but debouce it? No, hardware filter does that.
    
    int delta = count - lastCount;
    if (delta != 0) {
        // Simple division for usability if it's too fast (4 steps/click)
        if (abs(delta) >= 2) { 
             int effectiveDelta = delta / 2;
             lastCount = count - (delta % 2); // Keep remainder
             return effectiveDelta;
        }
    }
    return 0; // Wait for enough accumulation (2 edges) to trigger 1 step
}

bool EncoderHandler::getClick() {
    if (clicked) {
        clicked = false;
        return true;
    }
    return false;
}

bool EncoderHandler::getLongPress() {
    if (longPressed) {
        longPressed = false;
        return true;
    }
    return false;
}

bool EncoderHandler::getAuxClick() {
    if (auxClicked) {
        auxClicked = false;
        return true;
    }
    return false;
}

// ============================================================================
// DISPLAY UI CONSTRUCTOR
// ============================================================================
DisplayUI::DisplayUI() {
    display = nullptr;
    currentScreen = SCREEN_SPLASH;
    currentMode = MODE_SATURATOR;
    currentGroup = GROUP_TONE;
    
    modeMenuCursor = 0;
    groupMenuCursor = 0;
    paramMenuCursor = 0;
    presetMenuCursor = 0;
    
    editParamIndex = 0;
    editValue = 0.0f;
    
    needsRedraw = true;
    lastDrawTime = 0;
    scrollOffset = 0;
}

// ============================================================================
// BEGIN - Initialize display and encoder
// ============================================================================
bool DisplayUI::begin() {
    // Initialize I2C on custom pins
    Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
    
    // Create display object
    display = new Adafruit_SSD1306(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);
    
    // Initialize display
    if (!display->begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
        Serial.println("SSD1306 init FAILED");
        return false;
    }
    
    Serial.println("SSD1306 init OK");
    
    // Clear and set defaults
    display->clearDisplay();
    display->setTextColor(SSD1306_WHITE);
    display->setTextSize(1);
    display->display();
    
    // Initialize encoder
    EncoderHandler::begin();
    
    // Show splash screen
    currentScreen = SCREEN_SPLASH;
    needsRedraw = true;
    
    return true;
}

// ============================================================================
// UPDATE - Main update loop (call from UI task)
// ============================================================================
void DisplayUI::update() {
    // Check encoder input
    int delta = EncoderHandler::getDelta();
    if (delta != 0) {
        handleInput(delta > 0 ? INPUT_ROTATE_CW : INPUT_ROTATE_CCW);
    }
    
    if (EncoderHandler::getClick()) {
        // User requested Encoder Button to be BACK
        handleInput(INPUT_AUX_CLICK);
    }
    
    if (EncoderHandler::getLongPress()) {
        handleInput(INPUT_LONG_PRESS);
    }
    
    if (EncoderHandler::getAuxClick()) {
        // User requested Normal Button (Aux) to be SELECT
        handleInput(INPUT_CLICK);
    }
    
    // Render if needed (with rate limiting)
    if (!needsRedraw) return;
    
    uint32_t now = millis();
    if (now - lastDrawTime < MIN_DRAW_INTERVAL_MS) return;
    
    display->clearDisplay();
    
    switch (currentScreen) {
        case SCREEN_SPLASH:
            renderSplash();
            break;
        case SCREEN_MODE_SELECT:
            renderModeSelect();
            break;
        case SCREEN_GROUP_SELECT:
            renderGroupSelect();
            break;
        case SCREEN_PARAM_LIST:
            renderParamList();
            break;
        case SCREEN_PARAM_EDIT:
            renderParamEdit();
            break;
        case SCREEN_PRESETS:
            renderPresets();
            break;
    }
    
    display->display();
    needsRedraw = false;
    lastDrawTime = now;
}

// ============================================================================
// INPUT HANDLING
// ============================================================================
void DisplayUI::handleInput(InputEvent event) {
    markDirty();
    
    switch (currentScreen) {
        case SCREEN_SPLASH:
            if (event == INPUT_CLICK || event == INPUT_AUX_CLICK) {
                currentScreen = SCREEN_MODE_SELECT;
            }
            break;
            
        case SCREEN_MODE_SELECT: {
            int modeCount = getVisibleModeCount();
            if (modeCount < 1) modeCount = 1;  // Prevent divide by zero
            if (event == INPUT_ROTATE_CW) {
                modeMenuCursor = (modeMenuCursor + 1) % modeCount;
            } else if (event == INPUT_ROTATE_CCW) {
                modeMenuCursor = (modeMenuCursor - 1 + modeCount) % modeCount;
            } else if (event == INPUT_CLICK) {
                currentMode = (EffectMode)modeMenuCursor;
                activateMode(currentMode);  // Set audio processing flags
                currentScreen = SCREEN_GROUP_SELECT;
                groupMenuCursor = 0;
            }
            break;
        }
            
        case SCREEN_GROUP_SELECT: {
            int groupCount = getVisibleGroupCount();
            if (groupCount < 1) groupCount = 1;  // Prevent divide by zero
            if (event == INPUT_ROTATE_CW) {
                groupMenuCursor = (groupMenuCursor + 1) % groupCount;
            } else if (event == INPUT_ROTATE_CCW) {
                groupMenuCursor = (groupMenuCursor - 1 + groupCount) % groupCount;
            } else if (event == INPUT_CLICK) {
                // Get the actual group from the filtered list
                currentGroup = getGroupAtIndex(groupMenuCursor);
                paramMenuCursor = 0;
                
                // If group is PRESETS, go to preset menu instead
                if (currentGroup == GROUP_PRESETS) {
                    currentScreen = SCREEN_PRESETS;
                } else {
                    currentScreen = SCREEN_PARAM_LIST;
                }
            } else if (event == INPUT_AUX_CLICK) {
                goBack();
            } else if (event == INPUT_LONG_PRESS) {
                // Optional: Jump back to mode select? Or handle differently.
                // For now, long press in group select does nothing.
                if (currentMode == MODE_FRIPPERTRONICS) {
                    currentScreen = SCREEN_MODE_SELECT;
                }
            }
            break;
        }
            
        case SCREEN_PARAM_LIST: {
            int paramCount = getParamCount();
            if (paramCount < 1) paramCount = 1;  // Prevent divide by zero
            if (event == INPUT_ROTATE_CW) {
                paramMenuCursor = (paramMenuCursor + 1) % paramCount;
            } else if (event == INPUT_ROTATE_CCW) {
                paramMenuCursor = (paramMenuCursor - 1 + paramCount) % paramCount;
            } else if (event == INPUT_CLICK) {
                editParamIndex = paramMenuCursor;
                currentScreen = SCREEN_PARAM_EDIT;
            } else if (event == INPUT_AUX_CLICK) {
                goBack();
            } else if (event == INPUT_LONG_PRESS) {
                // Long press goes to mode select (skip groups)
                currentScreen = SCREEN_MODE_SELECT;
            }
            break;
        }
            
        case SCREEN_PARAM_EDIT: {
            // Get current parameter
            int count = 0;
            ParamDef* params = getParamsForGroup(currentGroup, &count);
            
            if (params != nullptr && editParamIndex < count) {
                ParamDef* p = &params[editParamIndex];
                float value = readParamValue(p);
                
                // Check if this is delay time in BPM mode
                bool isBpmMode = (p_delayUnitBpm && p->type == PARAM_FLOAT_MS && strcmp(p->cli, "dly") == 0);
                
                if (event == INPUT_ROTATE_CW) {
                    if (isBpmMode) {
                        // Convert to BPM, increment, convert back
                        float bpm = 60000.0f / value;
                        bpm += 5.0f;  // Step 5 BPM
                        if (bpm > 300.0f) bpm = 300.0f;  // Max 300 BPM
                        if (bpm < 30.0f) bpm = 30.0f;    // Min 30 BPM
                        value = 60000.0f / bpm;
                    } else {
                        value += p->step;
                        if (value > p->maxVal) value = p->maxVal;
                    }
                    writeParamValue(p, value);
                } else if (event == INPUT_ROTATE_CCW) {
                    if (isBpmMode) {
                        // Convert to BPM, decrement, convert back
                        float bpm = 60000.0f / value;
                        bpm -= 5.0f;  // Step 5 BPM
                        if (bpm < 30.0f) bpm = 30.0f;    // Min 30 BPM
                        if (bpm > 300.0f) bpm = 300.0f;  // Max 300 BPM
                        value = 60000.0f / bpm;
                    } else {
                        value -= p->step;
                        if (value < p->minVal) value = p->minVal;
                    }
                    writeParamValue(p, value);
                }
            }
            
            if (event == INPUT_CLICK || event == INPUT_AUX_CLICK) {
                currentScreen = SCREEN_PARAM_LIST;
            } else if (event == INPUT_LONG_PRESS) {
                currentScreen = SCREEN_MODE_SELECT;
            }
            break;
        }
            
        case SCREEN_PRESETS:
            if (event == INPUT_ROTATE_CW) {
                presetMenuCursor = (presetMenuCursor + 1) % PRESET_COUNT;
            } else if (event == INPUT_ROTATE_CCW) {
                presetMenuCursor = (presetMenuCursor - 1 + PRESET_COUNT) % PRESET_COUNT;
            } else if (event == INPUT_CLICK) {
                // Phase 2: Load preset
                currentScreen = SCREEN_GROUP_SELECT;
            } else if (event == INPUT_AUX_CLICK) {
                goBack();
            }
            break;
    }
}

// ============================================================================
// NAVIGATION HELPERS
// ============================================================================
void DisplayUI::goBack() {
    switch (currentScreen) {
        case SCREEN_GROUP_SELECT:
            currentScreen = SCREEN_MODE_SELECT;
            break;
        case SCREEN_PARAM_LIST:
            currentScreen = SCREEN_GROUP_SELECT;
            break;
        case SCREEN_PARAM_EDIT:
            currentScreen = SCREEN_PARAM_LIST;
            break;
        case SCREEN_PRESETS:
            currentScreen = SCREEN_GROUP_SELECT;
            break;
        default:
            break;
    }
    markDirty();
}

void DisplayUI::activateMode(EffectMode mode) {
    // Reset all mode flags first
    p_delayActive = false;
    p_freeze = false;
    p_reverse = false;
    p_reverseSmear = false;
    p_spring = false;
    p_frippMode = false;
    p_bubblesMode = false;  // Reset Bubbles mode
    
    // Activate the selected mode
    switch (mode) {
        case MODE_DELAY:
            p_delayActive = true;
            Serial.println("[UI] Mode: DELAY");
            break;
        case MODE_SATURATOR:
            // Saturator mode - delay off, just tape processing
            Serial.println("[UI] Mode: SATURATOR");
            break;
        case MODE_REVERB:
            p_spring = true;
            Serial.println("[UI] Mode: REVERB");
            break;
        case MODE_REVERSE:
            p_delayActive = true;
            p_reverse = true;
            Serial.println("[UI] Mode: REVERSE");
            break;
        case MODE_REV_REVERB:
            p_delayActive = true;
            p_reverse = true;
            p_reverseSmear = true;
            Serial.println("[UI] Mode: REV REVERB");
            break;
        case MODE_FREEZE:
            p_freeze = true;
            Serial.println("[UI] Mode: FREEZE");
            break;
        case MODE_FRIPPERTRONICS:
            p_frippMode = true;
            if (fripp) fripp->updateParams(globalFrippParams);
            Serial.println("[UI] Mode: FRIPPERTRONICS");
            break;
        case MODE_BUBBLES:
            p_bubblesMode = true;
            if (bubbles) bubbles->updateParams(globalBubblesParams);
            Serial.println("[UI] Mode: BUBBLES");
            break;
        default:
            break;
    }
    
    // Sync mode changes to audio processing
    syncGlobalParams();
}

int DisplayUI::getVisibleModeCount() {
    // All modes including FRIPPERTRONICS
    return MODE_COUNT;
}

// ============================================================================
// MODE-TO-GROUP MAPPING
// Groups available for each effect mode
// ============================================================================
// Returns array of available groups for current mode and count
int DisplayUI::getAvailableGroups(ParamGroup* outGroups) {
    switch (currentMode) {
        case MODE_DELAY:
            // Delay: Delay, Tone, Modulation, Melody, Presets
            outGroups[0] = GROUP_DELAY;
            outGroups[1] = GROUP_TONE;
            outGroups[2] = GROUP_MODULATION;
            outGroups[3] = GROUP_MELODY;
            outGroups[4] = GROUP_FREEVERB;  // Post-processing reverb
            outGroups[5] = GROUP_SETUP;
            outGroups[6] = GROUP_PRESETS;
            return 7;
            
        case MODE_SATURATOR:
            // Saturator: Tone, Modulation, Melody, Freeverb
            outGroups[0] = GROUP_TONE;
            outGroups[1] = GROUP_MODULATION;
            outGroups[2] = GROUP_MELODY;
            outGroups[3] = GROUP_FREEVERB;  // Added
            outGroups[4] = GROUP_SETUP;
            outGroups[5] = GROUP_PRESETS;
            return 6;
            
        case MODE_REVERB:
            // Reverb (Spring): Reverb, Tone, Modulation, Melody, Freeverb
            outGroups[0] = GROUP_REVERB;
            outGroups[1] = GROUP_TONE;
            outGroups[2] = GROUP_MODULATION;
            outGroups[3] = GROUP_MELODY;
            outGroups[4] = GROUP_FREEVERB;  // Added
            outGroups[5] = GROUP_SETUP;
            outGroups[6] = GROUP_PRESETS;
            return 7;
            
        case MODE_REVERSE:
            // Reverse: Delay, Tone, Modulation, Melody, Freeverb
            outGroups[0] = GROUP_DELAY;
            outGroups[1] = GROUP_TONE;
            outGroups[2] = GROUP_MODULATION;
            outGroups[3] = GROUP_MELODY;
            outGroups[4] = GROUP_FREEVERB;  // Added
            outGroups[5] = GROUP_SETUP;
            outGroups[6] = GROUP_PRESETS;
            return 7;
            
        case MODE_REV_REVERB:
            // Reverse Reverb: Delay, Reverb, Tone, Modulation, Freeverb
            outGroups[0] = GROUP_DELAY;
            outGroups[1] = GROUP_REVERB;
            outGroups[2] = GROUP_TONE;
            outGroups[3] = GROUP_MODULATION;
            outGroups[4] = GROUP_FREEVERB;  // Added
            outGroups[5] = GROUP_SETUP;
            outGroups[6] = GROUP_PRESETS;
            return 7;
            
        case MODE_FREEZE:
            // Freeze: Tone, Modulation, Freeverb
            outGroups[0] = GROUP_TONE;
            outGroups[1] = GROUP_MODULATION;
            outGroups[2] = GROUP_FREEVERB; // Added
            outGroups[3] = GROUP_SETUP;
            outGroups[4] = GROUP_PRESETS;
            return 5;
            
        case MODE_FRIPPERTRONICS:
            // Frippertronics/Eno: Fripp, Melody, Tone, Modulation, Freeverb
            outGroups[0] = GROUP_FRIPP;      // Eno mode, delay times, etc.
            outGroups[1] = GROUP_MELODY;     // MelodyGen for input
            outGroups[2] = GROUP_TONE;       // Tape character
            outGroups[3] = GROUP_MODULATION; // Flutter, Wow
            outGroups[4] = GROUP_FREEVERB;   // Added
            outGroups[5] = GROUP_SETUP;
            outGroups[6] = GROUP_PRESETS;
            return 7;
            
        case MODE_BUBBLES:
            // Bubbles: Bubbles params, Melody, Freeverb
            outGroups[0] = GROUP_BUBBLES;    // BPM, feedback, mix, allpass
            outGroups[1] = GROUP_MELODY;     // MelodyGen for input
            outGroups[2] = GROUP_FREEVERB;   // Added
            outGroups[3] = GROUP_SETUP;
            outGroups[4] = GROUP_PRESETS;
            return 5;
            
        default:
            outGroups[0] = GROUP_PRESETS;
            return 1;
    }
}

int DisplayUI::getVisibleGroupCount() {
    ParamGroup groups[8];
    return getAvailableGroups(groups);
}

ParamGroup DisplayUI::getGroupAtIndex(int index) {
    ParamGroup groups[8];
    int count = getAvailableGroups(groups);
    if (index >= 0 && index < count) {
        return groups[index];
    }
    return GROUP_PRESETS;
}

int DisplayUI::getParamCount() {
    switch (currentGroup) {
        case GROUP_DELAY: return DELAY_PARAMS_COUNT;
        case GROUP_TONE: return TONE_PARAMS_COUNT;
        case GROUP_MODULATION: return MOD_PARAMS_COUNT;
        case GROUP_MELODY: return MELODY_PARAMS_COUNT;
        case GROUP_REVERB: return REVERB_PARAMS_COUNT;
        case GROUP_FRIPP: return FRIPP_PARAMS_COUNT;
        case GROUP_BUBBLES: return BUBBLES_PARAMS_COUNT;
        case GROUP_FREEVERB: return FREEVERB_PARAMS_COUNT;
        case GROUP_SETUP: return SETUP_PARAMS_COUNT;
        default: return 1;  // Safety: at least 1 to prevent divide by zero
    }
}

// ============================================================================
// SCREEN RENDERERS
// ============================================================================

void DisplayUI::drawHeader(const char* title) {
    // Yellow zone header (16px)
    display->fillRect(0, 0, OLED_WIDTH, OLED_YELLOW_ZONE, SSD1306_WHITE);
    display->setTextColor(SSD1306_BLACK);
    display->setCursor(4, 4);
    display->print(title);
    display->setTextColor(SSD1306_WHITE);
}

void DisplayUI::drawMenuItem(int y, const char* text, bool selected) {
    if (selected) {
        display->fillRect(0, y, OLED_WIDTH, 12, SSD1306_WHITE);
        display->setTextColor(SSD1306_BLACK);
    } else {
        display->setTextColor(SSD1306_WHITE);
    }
    display->setCursor(8, y + 2);
    display->print(text);
    display->setTextColor(SSD1306_WHITE);
}

void DisplayUI::drawProgressBar(int x, int y, int w, int h, float value) {
    display->drawRect(x, y, w, h, SSD1306_WHITE);
    int fillW = (int)((w - 2) * constrain(value, 0.0f, 1.0f));
    if (fillW > 0) {
        display->fillRect(x + 1, y + 1, fillW, h - 2, SSD1306_WHITE);
    }
}

void DisplayUI::renderSplash() {
    // === YELLOW ZONE: Title with inverted colors ===
    display->fillRect(0, 0, OLED_WIDTH, OLED_YELLOW_ZONE, SSD1306_WHITE);
    display->setTextColor(SSD1306_BLACK);
    display->setTextSize(1);
    display->setCursor(14, 4);
    display->print("HYDRA tape engine");
    display->setTextColor(SSD1306_WHITE);
    
    // === BLUE ZONE: Tape mechanism (y: 16-64) ===
    // Animation frame
    splashAnimFrame++;
    float angle = (splashAnimFrame * 0.12f);  // Rotation speed
    
    // Reel positions (larger, more prominent)
    int lx = 34, ly = 36;   // Left reel center
    int rx = 94, ry = 36;   // Right reel center
    int reelR = 14;         // Reel outer radius
    int hubR = 5;           // Hub radius
    
    // Draw left reel
    display->drawCircle(lx, ly, reelR, SSD1306_WHITE);
    display->drawCircle(lx, ly, reelR - 2, SSD1306_WHITE);
    display->fillCircle(lx, ly, hubR, SSD1306_WHITE);
    
    // Draw right reel
    display->drawCircle(rx, ry, reelR, SSD1306_WHITE);
    display->drawCircle(rx, ry, reelR - 2, SSD1306_WHITE);
    display->fillCircle(rx, ry, hubR, SSD1306_WHITE);
    
    // Draw animated spokes (3 per reel)
    for (int i = 0; i < 3; i++) {
        float a = angle + (i * 2.094f);  // 120 degrees apart
        float cosA = cos(a);
        float sinA = sin(a);
        
        // Left reel spokes (from hub edge to reel edge)
        int lx1 = lx + (int)(hubR * cosA);
        int ly1 = ly + (int)(hubR * sinA);
        int lx2 = lx + (int)((reelR - 3) * cosA);
        int ly2 = ly + (int)((reelR - 3) * sinA);
        display->drawLine(lx1, ly1, lx2, ly2, SSD1306_WHITE);
        
        // Right reel spokes (opposite rotation)
        float aR = -angle + (i * 2.094f);
        cosA = cos(aR);
        sinA = sin(aR);
        int rx1 = rx + (int)(hubR * cosA);
        int ry1 = ry + (int)(hubR * sinA);
        int rx2 = rx + (int)((reelR - 3) * cosA);
        int ry2 = ry + (int)((reelR - 3) * sinA);
        display->drawLine(rx1, ry1, rx2, ry2, SSD1306_WHITE);
    }
    
    // Capstan and pinch roller positions (bottom contacts)
    int cap1X = 50, cap2X = 78;  // Two contact points
    int capY = 56;               // Bottom of blue zone
    int capR = 3;                // Contact point radius
    
    // Draw capstan contacts
    display->fillCircle(cap1X, capY, capR, SSD1306_WHITE);
    display->fillCircle(cap2X, capY, capR, SSD1306_WHITE);
    
    // Tape path: from left reel, down around capstans, up to right reel
    // Left reel to first capstan
    display->drawLine(lx + reelR, ly + 4, cap1X, capY - capR, SSD1306_WHITE);
    // Between capstans (along bottom)
    display->drawLine(cap1X + capR, capY, cap2X - capR, capY, SSD1306_WHITE);
    // Second capstan to right reel
    display->drawLine(cap2X, capY - capR, rx - reelR, ry + 4, SSD1306_WHITE);
    
    // Keep animating while on splash
    markDirty();
}

void DisplayUI::renderModeSelect() {
    drawHeader("> SELECT MODE");
    
    // Show 4 items at a time in blue zone
    int startIdx = (modeMenuCursor / 4) * 4;
    for (int i = 0; i < 4 && (startIdx + i) < getVisibleModeCount(); i++) {
        int idx = startIdx + i;
        bool isSelected = (idx == modeMenuCursor);
        bool isActive = (idx == (int)currentMode);
        
        // Build display string with active indicator
        char line[24];
        if (isActive) {
            snprintf(line, sizeof(line), "*%s", MODE_NAMES[idx]);
        } else {
            snprintf(line, sizeof(line), " %s", MODE_NAMES[idx]);
        }
        
        drawMenuItem(OLED_YELLOW_ZONE + (i * 12), line, isSelected);
    }
}

void DisplayUI::renderGroupSelect() {
    // Header shows current mode
    char header[32];
    snprintf(header, sizeof(header), "# %s", MODE_NAMES[currentMode]);
    drawHeader(header);
    
    // Show filtered parameter groups for current mode
    int visibleCount = getVisibleGroupCount();
    int startIdx = (groupMenuCursor / 4) * 4;
    
    for (int i = 0; i < 4 && (startIdx + i) < visibleCount; i++) {
        int idx = startIdx + i;
        ParamGroup group = getGroupAtIndex(idx);
        drawMenuItem(OLED_YELLOW_ZONE + (i * 12), GROUP_NAMES[group], idx == groupMenuCursor);
    }
}

void DisplayUI::renderParamList() {
    // Header shows group name
    char header[32];
    snprintf(header, sizeof(header), "# %s", GROUP_NAMES[currentGroup]);
    drawHeader(header);
    
    // Get real parameters for this group
    int count = 0;
    ParamDef* params = getParamsForGroup(currentGroup, &count);
    
    if (params == nullptr || count == 0) {
        display->setCursor(4, 20);
        display->print("No parameters");
        return;
    }
    
    int startIdx = (paramMenuCursor / 4) * 4;
    for (int i = 0; i < 4 && (startIdx + i) < count; i++) {
        int idx = startIdx + i;
        ParamDef* p = &params[idx];
        
        // Read current value
        float value = readParamValue(p);
        String valStr = formatParamValue(p, value);
        
        // Format line: "Name     Value"
        char line[22];
        snprintf(line, sizeof(line), "%-9s %s", p->name, valStr.c_str());
        
        drawMenuItem(OLED_YELLOW_ZONE + (i * 12), line, idx == paramMenuCursor);
    }
}

void DisplayUI::renderParamEdit() {
    // Get current parameter
    int count = 0;
    ParamDef* params = getParamsForGroup(currentGroup, &count);
    
    if (params == nullptr || editParamIndex >= count) {
        drawHeader("# ERROR");
        return;
    }
    
    ParamDef* p = &params[editParamIndex];
    float value = readParamValue(p);
    
    // Header: parameter name
    char header[32];
    snprintf(header, sizeof(header), "# %s", p->name);
    drawHeader(header);
    
    // Format value with arrows
    String valStr = formatParamValue(p, value);
    String displayStr = "< " + valStr + " >";
    
    // Calculate if text fits at size 2 (12px per char)
    // OLED is 128px wide, leave some margin
    int charCount = displayStr.length();
    int textSize = 2;
    int charWidth = 12;  // Size 2 = ~12px per char
    
    if (charCount * charWidth > 120) {
        // Too long for size 2, use size 1
        textSize = 1;
        charWidth = 6;  // Size 1 = ~6px per char
    }
    
    display->setTextSize(textSize);
    
    // Center the value
    int textWidth = charCount * charWidth;
    int x = (OLED_WIDTH - textWidth) / 2;
    if (x < 0) x = 0;
    
    // Y position: size 2 at y=28, size 1 at y=32 (vertically centered)
    int y = (textSize == 2) ? 28 : 32;
    display->setCursor(x, y);
    display->print(displayStr);
    display->setTextSize(1);
    
    // Progress bar showing position in range
    float normalized = (value - p->minVal) / (p->maxVal - p->minVal);
    drawProgressBar(10, 52, 108, 8, normalized);
}

void DisplayUI::renderPresets() {
    drawHeader("# PRESETS");
    
    // Show 2 items at a time with 2-line descriptions (24px per item)
    int startIdx = (presetMenuCursor / 2) * 2;
    
    for (int i = 0; i < 2 && (startIdx + i) < PRESET_COUNT; i++) {
        int idx = startIdx + i;
        int yBase = OLED_YELLOW_ZONE + (i * 24);  // 24px per preset slot
        bool selected = (idx == presetMenuCursor);
        
        // Draw selection highlight
        if (selected) {
            display->fillRect(0, yBase, OLED_WIDTH, 22, SSD1306_WHITE);
            display->setTextColor(SSD1306_BLACK);
        } else {
            display->setTextColor(SSD1306_WHITE);
        }
        
        // Line 1: Preset name (bold via larger cursor offset)
        display->setCursor(4, yBase + 2);
        display->print(PRESETS[idx].name);
        
        // Line 2: Description
        display->setCursor(4, yBase + 12);
        display->print(PRESETS[idx].description);
        
        display->setTextColor(SSD1306_WHITE);
    }
}
