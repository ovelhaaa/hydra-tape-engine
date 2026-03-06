#include "TapeDelay.h"
#include <math.h>
#include <string.h>
#include "esp_heap_caps.h"

// ============================================================================
// FREEVERB ENGINE - STANDARD IMPLEMENTATION
// Based on public domain Freeverb by Jezar at Dreampoint
// Reimplemented for ESP32S3 SPIRAM
// ============================================================================

FreeverbEngine::FreeverbEngine(float fs) : sampleRate(fs), allocated(false) {
    sampleRateRatio = fs / 44100.0f; // Assuming tunings are for 44.1kHz

    // Allocate buffers for combs
    for (int i = 0; i < numcombs; ++i) {
        int sizeL = (int)(combTuningsL[i] * sampleRateRatio);
        int sizeR = (int)((combTuningsL[i] + STEREO_SPREAD) * sampleRateRatio);
        
        // Try Internal RAM first for speed/bandwidth
        combLBuf[i] = (float*)heap_caps_malloc(sizeL * sizeof(float), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!combLBuf[i]) {
            combLBuf[i] = (float*)heap_caps_malloc(sizeL * sizeof(float), MALLOC_CAP_SPIRAM);
        }

        combRBuf[i] = (float*)heap_caps_malloc(sizeR * sizeof(float), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!combRBuf[i]) {
            combRBuf[i] = (float*)heap_caps_malloc(sizeR * sizeof(float), MALLOC_CAP_SPIRAM);
        }

        if (combLBuf[i] && combRBuf[i]) {
            combL[i].setbuffer(combLBuf[i], sizeL);
            combR[i].setbuffer(combRBuf[i], sizeR);
            combL[i].mute();
            combR[i].mute();
        } else {
            // Handle allocation failure
            Serial.printf("FreeverbEngine: Failed to allocate comb buffer %d\n", i);
            allocated = false;
            return;
        }
    }

    // Allocate buffers for allpasses
    for (int i = 0; i < numallpasses; ++i) {
        int sizeL = (int)(allpassTuningsL[i] * sampleRateRatio);
        int sizeR = (int)((allpassTuningsL[i] + STEREO_SPREAD) * sampleRateRatio);

        // Try Internal RAM first
        allpassLBuf[i] = (float*)heap_caps_malloc(sizeL * sizeof(float), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!allpassLBuf[i]) {
            allpassLBuf[i] = (float*)heap_caps_malloc(sizeL * sizeof(float), MALLOC_CAP_SPIRAM);
        }

        allpassRBuf[i] = (float*)heap_caps_malloc(sizeR * sizeof(float), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!allpassRBuf[i]) {
            allpassRBuf[i] = (float*)heap_caps_malloc(sizeR * sizeof(float), MALLOC_CAP_SPIRAM);
        }

        if (allpassLBuf[i] && allpassRBuf[i]) {
            allpassL[i].setbuffer(allpassLBuf[i], sizeL);
            allpassR[i].setbuffer(allpassRBuf[i], sizeR);
            allpassL[i].mute();
            allpassR[i].mute();
        } else {
            // Handle allocation failure
            Serial.printf("FreeverbEngine: Failed to allocate allpass buffer %d\n", i);
            allocated = false;
            return;
        }
    }

    allocated = true;
    // Set default parameters
    setRoomSize(50.0f);
    setDamping(50.0f);
    setWet(30.0f);
    setDry(70.0f);
    setWidth(100.0f);
    update();
}

FreeverbEngine::~FreeverbEngine() {
    for (int i = 0; i < numcombs; ++i) {
        if (combLBuf[i]) heap_caps_free(combLBuf[i]);
        if (combRBuf[i]) heap_caps_free(combRBuf[i]);
    }
    for (int i = 0; i < numallpasses; ++i) {
        if (allpassLBuf[i]) heap_caps_free(allpassLBuf[i]);
        if (allpassRBuf[i]) heap_caps_free(allpassRBuf[i]);
    }
}

void FreeverbEngine::mute() {
    for (int i = 0; i < numcombs; ++i) {
        combL[i].mute();
        combR[i].mute();
    }
    for (int i = 0; i < numallpasses; ++i) {
        allpassL[i].mute();
        allpassR[i].mute();
    }
}

void FreeverbEngine::update() {
    // Recalculate parameters based on current settings
    roomsize1 = roomsize * scaleRoom + offsetRoom;
    damp1 = damp * scaleDamp;
    wet1 = wet * scaleWet;
    wet2 = wet * scaleWet; // For stereo, can be different
    
    for (int i = 0; i < numcombs; ++i) {
        combL[i].setfeedback(roomsize1);
        combR[i].setfeedback(roomsize1);
        combL[i].setdamp(damp1);
        combR[i].setdamp(damp1);
    }
    
    for (int i = 0; i < numallpasses; ++i) {
        allpassL[i].setfeedback(0.5f); // Allpass feedback is fixed at 0.5 for Freeverb
        allpassR[i].setfeedback(0.5f);
    }
}

void FreeverbEngine::setRoomSize(float value) {
    roomsize = value / 100.0f; // Scale 0-100 to 0-1
    update();
}

void FreeverbEngine::setDamping(float value) {
    damp = value / 100.0f; // Scale 0-100 to 0-1
    update();
}

void FreeverbEngine::setWet(float value) {
    wet = value / 100.0f; // Scale 0-100 to 0-1
    update();
}

void FreeverbEngine::setDry(float value) {
    dry = value / 100.0f; // Scale 0-100 to 0-1
    update();
}

void FreeverbEngine::setWidth(float value) {
    width = value / 100.0f; // Scale 0-100 to 0-1
    update();
}

void FreeverbEngine::updateParams(const FreeverbParams& params) {
    setRoomSize(params.roomSize);
    setDamping(params.damping);
    setWet(params.wet);
    setDry(params.dry);
    setWidth(params.width);
    // 'enabled' parameter is handled externally, not by update()
}

void FreeverbEngine::processStereo(float inL, float inR, float* outL, float* outR) {
    if (!allocated) {
        *outL = inL;
        *outR = inR;
        return;
    }

    float input = (inL + inR) * fixedGain; // Mono sum input for reverb core

    // Accumulate comb filter outputs
    float out_combL = 0.0f;
    float out_combR = 0.0f;
    for (int i = 0; i < numcombs; ++i) {
        out_combL += combL[i].process(input);
        out_combR += combR[i].process(input);
    }

    // Process through allpass filters
    float out_allpassL = out_combL;
    float out_allpassR = out_combR;
    for (int i = 0; i < numallpasses; ++i) {
        out_allpassL = allpassL[i].process(out_allpassL);
        out_allpassR = allpassR[i].process(out_allpassR);
    }

    // Apply stereo width and mix with dry signal
    float final_wetL = out_allpassL * (wet1 * (width / 2.0f + 0.5f)) + out_allpassR * (wet2 * (0.5f - width / 2.0f));
    float final_wetR = out_allpassR * (wet2 * (width / 2.0f + 0.5f)) + out_allpassL * (wet1 * (0.5f - width / 2.0f));

    *outL = inL * dry + final_wetL;
    *outR = inR * dry + final_wetR;
}
