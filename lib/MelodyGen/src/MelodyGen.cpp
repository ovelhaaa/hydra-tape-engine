#include "MelodyGen.h"

MelodyGen::MelodyGen(float fs) : sampleRate(fs) {
    phase = 0;
    frequency = 440.0;
    timer = 0;
    currentWaveform = TRIANGLE;
    
    // Defaults
    currentScale = MINOR;
    rootKey = 0; // C
    bpm = 120.0;
    mood = 0.5;
    rhythmDensity = 0.5;
    
    currentDurationSamples = 12000;
    currentScaleDegree = 0;
    currentOctave = 4;
}

void MelodyGen::setWaveform(Waveform wave) {
    currentWaveform = wave;
}

void MelodyGen::setScale(ScaleType scale) { currentScale = scale; }
void MelodyGen::setKey(int midiRoot) { rootKey = midiRoot % 12; }
void MelodyGen::setBPM(float newBpm) { bpm = (newBpm < 30) ? 30 : newBpm; }
void MelodyGen::setMood(float m) { mood = constrain(m, 0.0f, 1.0f); }
void MelodyGen::setRhythm(float r) { rhythmDensity = constrain(r, 0.0f, 1.0f); }

float MelodyGen::mtof(int midiNote) {
    return 440.0f * powf(2.0f, (midiNote - 69) / 12.0f);
}

int MelodyGen::getInterval(ScaleType scale, int degree) {
    // Simple lookup for scale intervals within one octave
    // Degree should be 0-6 for heptatonic, 0-4 for pentatonic
    int safeDegree = abs(degree);
    int octaveOffset = (degree / (scale == PENTATONIC_MIN ? 5 : 7)) * 12;
    if (degree < 0) octaveOffset -= 12; // Handle negative steps roughly
    
    int intervals[7];
    int count = 7;

    switch (scale) {
        case MAJOR: { int t[]={0,2,4,5,7,9,11}; memcpy(intervals, t, sizeof(t)); break; }
        case MINOR: { int t[]={0,2,3,5,7,8,10}; memcpy(intervals, t, sizeof(t)); break; }
        case PENTATONIC_MIN: { int t[]={0,3,5,7,10}; memcpy(intervals, t, sizeof(t)); count=5; break; }
        case BLUES: { int t[]={0,3,5,6,7,10}; memcpy(intervals, t, sizeof(t)); count=6; break; }
        default: { int t[]={0,2,4,5,7,9,11}; memcpy(intervals, t, sizeof(t)); break; } // Default Major
    }
    
    return intervals[safeDegree % count] + octaveOffset;
}

void MelodyGen::pickNextNote() {
    // 1. Determine Duration based on BPM and Rhythm Density
    float samplesPerBeat = (sampleRate * 60.0f) / bpm;
    int r = esp_random() % 100;
    
    // Higher rhythmDensity = more 16th notes, Lower = more quarter/half notes
    if (r < (rhythmDensity * 80)) currentDurationSamples = samplesPerBeat / 4; // 16th
    else if (r < 90) currentDurationSamples = samplesPerBeat / 2; // 8th
    else currentDurationSamples = samplesPerBeat; // Quarter

    // 2. Determine Octave (Bass vs Melody) based on Mood
    // Low mood = higher chance of bass
    bool isBass = (esp_random() % 100) < ((1.0f - mood) * 40 + 10); 
    currentOctave = isBass ? (2 + (esp_random()%2)) : (4 + (esp_random()%2));

    // 3. Determine Scale Degree (Random Walk)
    // Instead of random note, move -2, -1, 0, +1, +2 steps
    int step = (esp_random() % 5) - 2; 
    currentScaleDegree += step;
    
    // Keep degree somewhat centered to avoid drifting to infinity
    if (currentScaleDegree > 10) currentScaleDegree -= 3;
    if (currentScaleDegree < -5) currentScaleDegree += 3;

    // 4. Calculate Frequency
    int noteInScale = getInterval(currentScale, currentScaleDegree);
    int midiNote = rootKey + (currentOctave * 12) + noteInScale;
    
    // Clamp MIDI to hearing range
    if (midiNote < 24) midiNote = 24;
    if (midiNote > 96) midiNote = 96;

    frequency = mtof(midiNote);
}

float MelodyGen::next() {
    if (++timer >= currentDurationSamples) {
        timer = 0;
        pickNextNote();
    }

    phase += (2.0f * M_PI * frequency) / sampleRate;
    if (phase >= 2.0f * M_PI) phase -= 2.0f * M_PI;

    float out = 0.0f;
    switch(currentWaveform) {
        case SINE:
            out = sinf(phase);
            break;
        case SAWTOOTH:
            out = ((phase / M_PI) - 1.0f) * 0.4f;
            break;
        case TRIANGLE:
            out = 2.0f * fabsf(phase / M_PI - 1.0f) - 1.0f;
            break;
        case SQUARE:
            out = (phase < M_PI) ? 1.0f : -1.0f;
            break;
    }
    return out * 0.5f;
}
