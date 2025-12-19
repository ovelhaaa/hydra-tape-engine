#ifndef MELODY_GEN_H
#define MELODY_GEN_H

#include <Arduino.h>
#include <math.h>

// Definições simples para formas de onda e escalas
enum Waveform { SINE, SAWTOOTH, TRIANGLE, SQUARE };
enum ScaleType { CHROMATIC, MAJOR, MINOR, PENTATONIC_MIN, BLUES };

class MelodyGen {
private:
    float phase;
    float frequency;
    float targetFrequency;     
    float sampleRate;
    Waveform currentWaveform;
    float frequencySmoothing;  

    // Estado Musical
    ScaleType currentScale;
    int rootKey;        
    float bpm;
    float mood;         // 0.0 a 1.0 (controla probabilidade de graves/ritmo)
    float rhythmDensity;// 0.0 a 1.0

    // Sequenciador
    long timer;
    long currentDurationSamples;
    int currentScaleDegree; 
    int currentOctave;

    // Envelope Simples (Attack/Release para evitar clicks)
    float envelope;
    int attackSamples;
    int releaseSamples;
    bool noteOn;
    bool isResting;

    // Helpers internos
    float mtof(int midiNote);
    int getInterval(ScaleType scale, int degree);
    void pickNextNote();

public:
    MelodyGen(float fs);
    float next(); // Retorna o próximo sample de áudio

    // Setters usados no main.cpp
    void setWaveform(Waveform wave);
    void setScale(ScaleType scale);
    void setKey(int midiRoot);
    void setBPM(float newBpm);
    void setMood(float m);
    void setRhythm(float r);
};

#endif