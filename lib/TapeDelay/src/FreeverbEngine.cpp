#include "TapeDelay.h"
#include <math.h>
#include <string.h>
#include "esp_attr.h"
#include "esp_heap_caps.h"

// ============================================================================
// FREEVERB ENGINE - STANDARD IMPLEMENTATION
// Based on public domain Freeverb by Jezar at Dreampoint
// Reimplemented for ESP32S3 SPIRAM
// ============================================================================

namespace {
float* allocateFilterBuffer(int samples) {
    const size_t bytes = samples * sizeof(float);
    auto* buffer = static_cast<float*>(heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (!buffer) buffer = static_cast<float*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM));
    return buffer;
}
}

FreeverbEngine::FreeverbEngine(float fs) : sampleRate(fs), allocated(false) {
    sampleRateRatio = fs / 44100.0f; // Assuming tunings are for 44.1kHz

    // Allocate buffers for combs
    for (int i = 0; i < numcombs; ++i) {
        int sizeL = (int)(combTuningsL[i] * sampleRateRatio);
        int sizeR = (int)((combTuningsL[i] + STEREO_SPREAD) * sampleRateRatio);
        
        auto* bufferL = allocateFilterBuffer(sizeL);
        auto* bufferR = allocateFilterBuffer(sizeR);
        // Each filter owns its buffer, including when the other allocation fails.
        combL[i].setbuffer(bufferL, sizeL);
        combR[i].setbuffer(bufferR, sizeR);
        if (!bufferL || !bufferR) {
            Serial.printf("FreeverbEngine: Failed to allocate comb buffer %d\n", i);
            return;
        }
        combL[i].mute();
        combR[i].mute();
    }

    // Allocate buffers for allpasses
    for (int i = 0; i < numallpasses; ++i) {
        int sizeL = (int)(allpassTuningsL[i] * sampleRateRatio);
        int sizeR = (int)((allpassTuningsL[i] + STEREO_SPREAD) * sampleRateRatio);

        auto* bufferL = allocateFilterBuffer(sizeL);
        auto* bufferR = allocateFilterBuffer(sizeR);
        allpassL[i].setbuffer(bufferL, sizeL);
        allpassR[i].setbuffer(bufferR, sizeR);
        if (!bufferL || !bufferR) {
            Serial.printf("FreeverbEngine: Failed to allocate allpass buffer %d\n", i);
            return;
        }
        allpassL[i].mute();
        allpassR[i].mute();
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
    const float wetGain = wet * scaleWet;
    wet1 = wetGain * (width * 0.5f + 0.5f);
    wet2 = wetGain * (0.5f - width * 0.5f);
    
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
    float final_wetL = out_allpassL * wet1 + out_allpassR * wet2;
    float final_wetR = out_allpassR * wet1 + out_allpassL * wet2;

    *outL = inL * dry + final_wetL;
    *outR = inR * dry + final_wetR;
}

IRAM_ATTR void FreeverbEngine::processStereoBlock(const float* inL, const float* inR, float* outL, float* outR, int frames) {
    if (!allocated) {
        if (outL != inL) memcpy(outL, inL, sizeof(float) * frames);
        if (outR != inR) memcpy(outR, inR, sizeof(float) * frames);
        return;
    }

    int offset = 0;
    while (offset < frames) {
        const int chunk = ((frames - offset) > 128) ? 128 : (frames - offset);
        float monoIn[128];
        float accL[128];
        float accR[128];

        for (int f = 0; f < chunk; ++f) {
            monoIn[f] = (inL[offset + f] + inR[offset + f]) * fixedGain;
            accL[f] = 0.0f;
            accR[f] = 0.0f;
        }

        for (int i = 0; i < numcombs; ++i) {
            combL[i].processBlockAccum(monoIn, accL, chunk);
            combR[i].processBlockAccum(monoIn, accR, chunk);
        }

        for (int i = 0; i < numallpasses; ++i) {
            allpassL[i].processBlockInPlace(accL, chunk);
            allpassR[i].processBlockInPlace(accR, chunk);
        }

        const float w1 = wet1;
        const float w2 = wet2;
        const float dryGain = dry;
        for (int f = 0; f < chunk; ++f) {
            const float xL = inL[offset + f];
            const float xR = inR[offset + f];
            const float aL = accL[f];
            const float aR = accR[f];
            outL[offset + f] = xL * dryGain + (aL * w1 + aR * w2);
            outR[offset + f] = xR * dryGain + (aR * w1 + aL * w2);
        }
        offset += chunk;
    }
}
