#ifndef MELODY_GEN_H
#define MELODY_GEN_H

#include <Arduino.h>
#include <math.h>

// Definições simples para formas de onda e escalas
enum Waveform { SINE, SAWTOOTH, TRIANGLE, SQUARE };
enum ScaleType { CHROMATIC, MAJOR, MINOR, PENTATONIC_MIN, BLUES };
enum MelodyMode { MODE_NORMAL, MODE_ENO }; // Update V3: Eno Ambient Mode

class MelodyGen {
private:
  // --- UPGRADE V8: DSP OPTIMIZATIONS ---
  // Recursive Sine State (No sinf() per sample)
  float oscSin, oscCos;
  float sinInc, cosInc;

  // Unison State (Recursive)
  float uniSin, uniCos;
  float uniSinInc, uniCosInc;

  // Triangle Integrator (No division/fabsf)
  float tri;
  float triInc;
  float saw;
  
  // Normalization counter (prevents amplitude drift)
  int normCounter;

  // Legacy / Other members
  float phase;
  float frequency;
  float targetFrequency;
  float sampleRate;
  Waveform currentWaveform;
  MelodyMode mode; // Current Mode
  float frequencySmoothing;

  // Musical State
  ScaleType currentScale;
  int rootKey;
  float bpm;
  float mood;          // 0.0 to 1.0 (controls bass/rhythm probability)
  float rhythmDensity; // 0.0 to 1.0

  // Sequencer
  long timer;
  long currentDurationSamples;
  int currentScaleDegree;
  int currentOctave;

  // --- UPGRADE: MOTIF & RHYTHMIC MEMORY ---
  int motif[4];           // Melodic motif buffer
  int motifIndex;         // Current step in motif
  float rhythmPattern[4]; // Current rhythm cell
  int rhythmIndex;        // Step in rhythm cell
  int phraseCounter;      // Counts measure/phrases for Call & Response
  float currentAccent;    // Dynamics for current note

  // --- UPGRADE V4: ORGANIC AUDIO ---
  float detune;       // Micro-detune for unison
  float timbre;       // Current morph position (0.0=Sine, 1.0=Rich)
  float timbreTarget; // Target morph position
  float tone;         // Filter cutoff coefficient
  float toneState;    // Filter state
  float drift;        // Frequency instability factor

  inline float fastSat(float x) {
    if (x > 1.0f)
      return 1.0f;
    if (x < -1.0f)
      return -1.0f;
    return x;
  }

  // static float envLUT[256];
  // static bool lutInitialized;
  // void initLUT();

  // inline float softSat(float x) { return x / (1.0f + fabsf(x)); } //
  // Deprecated

  // --- UPGRADE V5: STEREO & ENVELOPE ---
  float env;         // Current envelope value (0.0-1.0)
  float envTarget;   // Target value (0.0 or 1.0)
  float envAttack;   // Attack coefficient
  float envRelease;  // Release coefficient
  float stereoDrift; // L/R phase/timbre offset

  bool noteOn;
  bool isResting;

  // Helpers internos
  float mtof(int midiNote);
  int getInterval(ScaleType scale, int degree);
  void pickNextNote();

public:
  MelodyGen(float fs);
  void nextStereo(float *outL, float *outR); // Generates Stereo Output

  // Setters usados no main.cpp
  void setWaveform(Waveform wave);
  void setScale(ScaleType scale);
  void setKey(int midiRoot);
  void setBPM(float newBpm);
  void setMood(float m);
  void setRhythm(float r);
  void setMode(MelodyMode m) { mode = m; }
};

#endif