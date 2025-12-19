#include "MelodyGen.h"

MelodyGen::MelodyGen(float fs) : sampleRate(fs) {
    phase = 0;
    frequency = 440.0;
    targetFrequency = 440.0;
    timer = 0;
    currentWaveform = SINE; // Começa com triângulo que é suave mas audível
    
    // Padrões
    currentScale = MINOR;
    rootKey = 4; // E
    bpm = 80.0;
    mood = 0.5;
    rhythmDensity = 0.65; // Aumentado para favorecer notas mais longas
    
    currentDurationSamples = 12000;
    currentScaleDegree = 0;
    currentOctave = 4;
    
    // Suavização para portamento (glide) entre notas
    frequencySmoothing = 0.998f; // Aumentado para menos "jumpy"
    
    // Envelope para evitar "clicks"
    envelope = 0.0f;
    noteOn = true;
    isResting = false;
    attackSamples = (int)(sampleRate * 0.005f); // 5ms attack
    releaseSamples = (int)(sampleRate * 0.01f); // 10ms release
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
    int intervals[7];
    int count = 7;

    switch (scale) {
        case MAJOR: { int t[]={0,2,4,5,7,9,11}; memcpy(intervals, t, sizeof(t)); break; }
        case MINOR: { int t[]={0,2,3,5,7,8,10}; memcpy(intervals, t, sizeof(t)); break; }
        case PENTATONIC_MIN: { int t[]={0,3,5,7,10}; memcpy(intervals, t, sizeof(t)); count=5; break; }
        case BLUES: { int t[]={0,3,5,6,7,10}; memcpy(intervals, t, sizeof(t)); count=6; break; }
        default: { int t[]={0,2,4,5,7,9,11}; memcpy(intervals, t, sizeof(t)); break; } 
    }

    int octaveOffset = 0;
    int safeDegree = degree;
    
    // Lógica robusta para oitavas negativas e positivas
    if (degree >= 0) {
        octaveOffset = (degree / count) * 12;
        safeDegree = degree % count;
    } else {
        octaveOffset = ((degree - count + 1) / count) * 12;
        safeDegree = (degree % count);
        if(safeDegree < 0) safeDegree += count;
    }

    return intervals[safeDegree] + octaveOffset;
}

void MelodyGen::pickNextNote() {
    noteOn = true;
    
    // 1. Duração baseada no BPM e chance de pausa
    float samplesPerBeat = (sampleRate * 60.0f) / bpm;
    int r_rest = esp_random() % 100;

    if (r_rest < 15) { // Reduzir chance de pausa para favorecer notas mais longas
        isResting = true;
        currentDurationSamples = (long)(samplesPerBeat); // Pausa de uma semínima
        return;
    }
    isResting = false;

    // Densidade Rítmica
    int r_rhythm = esp_random() % 100;
    if (r_rhythm < (rhythmDensity * 60)) currentDurationSamples = (long)(samplesPerBeat / 2); // 8th
    else if (r_rhythm < 90) currentDurationSamples = (long)samplesPerBeat; // Quarter
    else currentDurationSamples = (long)(samplesPerBeat * 1.5); // dotted Quarter or half

    // 2. Oitava (Grave vs Melodia) baseada no Mood
    bool isBass = (esp_random() % 100) < ((1.0f - mood) * 30 + 5); // Menos probabilidade de baixo extremo
    currentOctave = isBass ? (2 + (esp_random()%2)) : (3 + (esp_random()%2)); // Oitavas um pouco mais centradas

    // 3. Caminhada na escala (Random Walk) com viés para estabilidade
    int step = 0;
    int r_step = esp_random() % 100;
    if (r_step < 75) { // 75% de chance de passo de -1, 0 ou 1
        step = (esp_random() % 3) - 1; // -1, 0, 1
    } else if (r_step < 95) { // 20% de chance de ficar parado
        step = 0;
    } else { // 5% de chance de passo de -2 ou 2
        step = ((esp_random() % 2) * 2 - 1) * 2;
    }

    // Tendência a retornar para a fundamental
    if (currentScaleDegree != 0 && (esp_random() % 100 < 20)) { // Aumenta tendência de voltar para fundamental
        if (currentScaleDegree > 0) step = -1;
        else step = 1;
    }

    currentScaleDegree += step;
    
    // Mantém a melodia numa faixa mais contida e musical
    if (currentScaleDegree > 6) currentScaleDegree = 6; // Range reduzido
    if (currentScaleDegree < -6) currentScaleDegree = -6; // Range reduzido


    // 4. Calcula Frequência
    int noteInScale = getInterval(currentScale, currentScaleDegree);
    int midiNote = rootKey + (currentOctave * 12) + noteInScale;
    
    // Limita ao alcance audível
    if (midiNote < 36) midiNote = 36; // Aumenta limite inferior
    if (midiNote > 84) midiNote = 84; // Reduz limite superior

    targetFrequency = mtof(midiNote);
}

float MelodyGen::next() {
    // Gerenciamento de Tempo
    if (++timer >= currentDurationSamples && currentDurationSamples > 0) {
        timer = 0;
        pickNextNote();
    }
    
    if (isResting) {
        return 0.0f;
    }

    if (noteOn) {
        // Opcional: Resetar fase pode causar click se não tiver envelope rápido,
        // mas garante ataque consistente. Vamos manter fluído.
        // phase = 0; 
        noteOn = false;
    }

    // Suaviza mudança de frequência (Portamento)
    frequency = frequency * frequencySmoothing + targetFrequency * (1.0f - frequencySmoothing);

    // Incrementa fase
    phase += (2.0f * M_PI * frequency) / sampleRate;
    if (phase >= 2.0f * M_PI) phase -= 2.0f * M_PI;

    // Envelope simples (Trapezoidal)
    if (timer < attackSamples) {
        envelope = (float)timer / attackSamples;
    } else if (timer >= currentDurationSamples - releaseSamples && currentDurationSamples > releaseSamples) {
        envelope = (float)(currentDurationSamples - timer) / releaseSamples;
    } else {
        envelope = 1.0f;
    }
    
    envelope = constrain(envelope, 0.0f, 1.0f);

    float out = 0.0f;
    switch(currentWaveform) {
        case SINE:
            out = sinf(phase);
            break;
        case SAWTOOTH:
            // Sawtooth simples: range -1 a 1
            out = ((phase / M_PI) - 1.0f);
            break;
        case TRIANGLE:
            // Triangle: range -1 a 1
            out = 2.0f * fabsf(phase / M_PI - 1.0f) - 1.0f;
            break;
        case SQUARE:
            out = (phase < M_PI) ? 1.0f : -1.0f;
            break;
    }
    
    // Reduz um pouco o ganho global para evitar clip no I2S/Tape Delay depois
    return out * envelope * 0.8f;
}