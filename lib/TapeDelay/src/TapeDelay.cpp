#include "TapeDelay.h"

TapeModel::TapeModel(float fs, float maxDelayTimeMs)
    : sampleRate(fs), noiseGen(fs), flutterPhase(0), wowPhase(0), azimuthPhase(0)
{
    // Cálculo de buffer seguro
    bufferSize = (int32_t)(fs * (maxDelayTimeMs / 1000.0f));
    size_t bytes = bufferSize * sizeof(float);

    // Tenta alocar na PSRAM primeiro
    delayLine = (float*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
    usesSPIRAM = true;

    // Fallback para RAM interna
    if (!delayLine) {
        Serial.println("WARN: PSRAM falhou. Usando RAM interna.");
        bufferSize = (int32_t)(fs * 0.4f); 
        delayLine = (float*)heap_caps_malloc(bufferSize * sizeof(float), MALLOC_CAP_INTERNAL);
        usesSPIRAM = false;
    }

    if (delayLine) {
        memset(delayLine, 0, bufferSize * sizeof(float));
    } else {
        Serial.println("CRITICAL: Falha total de memória!");
        bufferSize = 0;
    }

    writeHead = 0;

    // Init default params
    currentParams = {};
    currentParams.tapeSpeed = 0.5f;
    currentParams.tapeAge = 0.5f;
    currentParams.headBumpAmount = 0.5f;
    currentParams.bpm = 90.0f;
    currentParams.headsMusical = true;
    
    updateFilters();
    flutterLPF.setLowpass(fs, 15.0f, 0.707f); 
}

TapeModel::~TapeModel() {
    if (delayLine) heap_caps_free(delayLine);
}

void TapeModel::updateFilters() {
    float speedMod = currentParams.tapeSpeed; 
    float ageMod = currentParams.tapeAge;

    // 1. Head Bump (Graves) - Mantido igual pois dá o "corpo"
    float bumpFreq = 60.0f + (speedMod * 60.0f);
    float bumpGain = currentParams.headBumpAmount * 6.0f;
    headBump.setLowShelf(sampleRate, bumpFreq, 0.7f, bumpGain);

    // --- AQUI ESTÁ A MUDANÇA (DARK MODE) ---
    
    // Frequência Base (Teto):
    // Reduzi drasticamente. Fita rápida agora é ~10.5kHz (antes era >16k).
    // Fita lenta agora é ~1.5kHz (bem abafado).
    float baseFreq = 1500.0f + (speedMod * 9000.0f);

    // Age Factor (Envelhecimento):
    // O parâmetro AGE agora destrói os agudos.
    // Se age = 1.0 (100%), reduz a frequência de corte em 90%.
    float ageFactor = 1.0f - (ageMod * 0.90f); 
    
    float finalCutoff = baseFreq * ageFactor;
    
    // Limite mínimo de 400Hz para garantir que ainda seja "áudio" e não apenas "hum"
    if (finalCutoff < 400.0f) finalCutoff = 400.0f;

    // Corte Duplo para eliminar o brilho digital:
    // 1. High Shelf super agressivo (-50dB)
    tapeRolloff.setHighShelf(sampleRate, finalCutoff, 0.5f, -50.0f); 

    // 2. Low Pass Filter (LPF) alinhado para limpar o que sobrar
    outputLPF.setLowpass(sampleRate, finalCutoff, 0.707f);
}

void TapeModel::updateParams(const TapeParams& newParams) {
    currentParams = newParams;
    dropout.setSeverity(newParams.dropoutSeverity);
    updateFilters(); 
}

AUDIO_INLINE float TapeModel::readTapeAt(float delaySamples) {
    if (bufferSize == 0) return 0.0f;

    if (delaySamples < 2.0f) delaySamples = 2.0f;
    if (delaySamples > bufferSize - 4.0f) delaySamples = (float)bufferSize - 4.0f;

    float readPos = (float)writeHead - delaySamples;
    
    if (readPos < 0.0f) readPos += bufferSize;
    else if (readPos >= bufferSize) readPos -= bufferSize;

    int32_t r = (int32_t)readPos;
    float f = readPos - r;

    int32_t i1 = r;
    int32_t i2 = (r > 0) ? r - 1 : bufferSize - 1;
    int32_t i0 = (r < bufferSize - 1) ? r + 1 : 0;
    int32_t i3 = (i0 < bufferSize - 1) ? i0 + 1 : 0;

    float d1 = delayLine[i1];
    float d0 = delayLine[i0];
    float d2 = delayLine[i2];
    float d3 = delayLine[i3];

    // Hermite Interpolation
    float c0 = d1;
    float c1 = 0.5f * (d0 - d2);
    float c2 = d2 - 2.5f * d1 + 2.0f * d0 - 0.5f * d3;
    float c3 = 0.5f * (d3 - d1) + 1.5f * (d1 - d0);

    return ((c3 * f + c2) * f + c1) * f + c0;
}

IRAM_ATTR float TapeModel::process(float input) {
    if (!delayLine) return input;

    TapeParams* p = &currentParams;

    // --- MODULAÇÃO 
        float flutterInc = 6.28318f * p->flutterRate / sampleRate;
    flutterPhase += flutterInc;
    if (flutterPhase > 6.28318f) flutterPhase -= 6.28318f;

    float wowInc = 6.28318f * p->wowRate / sampleRate;
    wowPhase += wowInc;
    if (wowPhase > 6.28318f) wowPhase -= 6.28318f;

    // Wow e Flutter
    float rawMod = (sinf(flutterPhase) * p->flutterDepth) + (sinf(wowPhase) * p->wowDepth);
    // Filtramos o movimento para simular a massa do capstan
    float mod = flutterLPF.process(rawMod);


    azimuthPhase += (0.2f / sampleRate); 
    if (azimuthPhase > 1.0f) azimuthPhase = 0.0f;
    float tri = (azimuthPhase < 0.5f) ? (azimuthPhase * 2.0f) : (2.0f - azimuthPhase * 2.0f);
    float azimuthMod = 0.5f + (tri * 1.5f); 

    bool useAzimuth = (p->azimuthError > 0.01f);
    if (useAzimuth) {
        azimuthFilter.setCoeff(-0.90f * p->azimuthError * azimuthMod);
    }

    // --- LEITURA (MANTIDA IGUAL) ---
    float tapeSignal = 0.0f;
    float modDepth = 2.0f; // Wow intenso mantido

    if (!p->delayActive) {
        tapeSignal = readTapeAt(200.0f + mod * 40.0f * modDepth);
    } else {
        if (p->headsMusical) {
            float beatMs = 60000.0f / p->bpm; 
            if (p->activeHeads & 1) tapeSignal += readTapeAt((beatMs * 0.333f * sampleRate * 0.001f) + (mod * 40.0f * modDepth));
            if (p->activeHeads & 2) tapeSignal += readTapeAt((beatMs * 0.75f * sampleRate * 0.001f)  + (mod * 60.0f * modDepth));
            if (p->activeHeads & 4) tapeSignal += readTapeAt((beatMs * 1.0f * sampleRate * 0.001f)   + (mod * 80.0f * modDepth));
        } else {
            float baseDelay = p->delayActive ? (p->delayTimeMs * sampleRate * 0.001f) : 200.0f;
            if (p->activeHeads & 1) tapeSignal += readTapeAt((baseDelay * 0.33f) + (mod * 40.0f * modDepth));
            if (p->activeHeads & 2) tapeSignal += readTapeAt((baseDelay * 0.66f) + (mod * 60.0f * modDepth));
            if (p->activeHeads & 4) tapeSignal += readTapeAt(baseDelay + (mod * 80.0f * modDepth));
        }
        if (p->activeHeads > 4) tapeSignal *= 0.6f;
    }

    // --- DEGRADAÇÃO & FILTROS (AQUI OS FILTROS NOVOS AGEM) ---
    float dropoutGain = dropout.process();
    tapeSignal *= dropoutGain;

    if (p->noise > 0.001f) {
        float hiss = noiseGen.next() * p->noise * (1.0f + (2.0f * (1.0f - dropoutGain)));
        tapeSignal += hiss;
    }

    if (useAzimuth) {
        tapeSignal = azimuthFilter.process(tapeSignal);
    }

    // Equalização "Dark" aplicada aqui
    tapeSignal = headBump.process(tapeSignal);
    tapeSignal = tapeRolloff.process(tapeSignal);
    tapeSignal = outputLPF.process(tapeSignal);

    // --- FEEDBACK & DRIVE (MANTIDO IGUAL) ---
    float feedbackSig = 0.0f;
    if (p->delayActive) {
        feedbackSig = tapeSignal * p->feedback;
        feedbackSig = dcBlocker.process(feedbackSig);
    }

    float recordSignal = (input * p->drive) + feedbackSig;

    if (recordSignal > 4.0f) recordSignal = 4.0f;
    else if (recordSignal < -4.0f) recordSignal = -4.0f;

    delayLine[writeHead] = saturator(recordSignal);

    writeHead++;
    if (writeHead >= bufferSize) writeHead = 0;

    return (input * (1.0f - p->dryWet)) + (tapeSignal * p->dryWet);
}