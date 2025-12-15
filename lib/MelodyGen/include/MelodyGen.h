#ifndef MELODY_GEN_H
#define MELODY_GEN_H

#include <math.h>
#include <Arduino.h>

enum Waveform { SINE, SAWTOOTH, TRIANGLE, SQUARE };

enum ScaleType { CHROMATIC, MAJOR, MINOR, PENTATONIC_MIN, BLUES };

class MelodyGen {
private:
    float phase;
    float frequency;
    float sampleRate;
    Waveform currentWaveform;

    // Music Theory State
    ScaleType currentScale;
    int rootKey;        // MIDI note (0 = C, 1 = C#, etc.)
    float bpm;
    float mood;         // 0.0 to 1.0
    float rhythmDensity;// 0.0 to 1.0

    // Sequencer State
    long timer;
    long currentDurationSamples;
    int currentScaleDegree; // Position in the scale
    int currentOctave;

    // Helpers
    float mtof(int midiNote);
    int getInterval(ScaleType scale, int degree);
    void pickNextNote();

public:
    MelodyGen(float fs);
    float next();
    void setWaveform(Waveform wave);
    void setScale(ScaleType scale);
    void setKey(int midiRoot);
    void setBPM(float newBpm);
    void setMood(float m);
    void setRhythm(float r);
};

#endif
