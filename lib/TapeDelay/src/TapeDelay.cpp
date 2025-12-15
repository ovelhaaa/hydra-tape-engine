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

    // Fallback para RAM interna (buffer menor) se falhar
    if (!delayLine) {
        Serial.println("WARN: PSRAM falhou/inexistente. Usando RAM interna (Delay curto).");
        bufferSize = (int32_t)(fs * 0.4f); // Max 400ms na RAM interna segura
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

    // Init default params to avoid garbage if updateParams not called immediately
    currentParams = {};
    currentParams.tapeSpeed = 0.5f;
    currentParams.tapeAge = 0.5f;
    currentParams.headBumpAmount = 0.5f;
    currentParams.bpm = 120.0f;
    currentParams.headsMusical = false;
    updateFilters();
    flutterLPF.setLowpass(fs, 20.0f, 0.707f); // Flutter filter mais lento para inércia real
}

TapeModel::~TapeModel() {
    if (delayLine) heap_caps_free(delayLine);
}

void TapeModel::updateFilters() {
    // Head bump e Rolloff dinâmicos
    float speedMod = currentParams.tapeSpeed; // 0.0 to 1.0

    float bumpFreq = 80.0f + (speedMod * 100.0f);
    float bumpGain = currentParams.headBumpAmount * 4.0f;
    headBump.setLowShelf(sampleRate, bumpFreq, 0.8f, bumpGain);

    // Fita velha = menos agudos, Fita rápida = mais agudos
    float rolloffFreq = 4000.0f + (speedMod * 12000.0f) - (currentParams.tapeAge * 3000.0f);
    if (rolloffFreq < 2000.0f) rolloffFreq = 2000.0f;

    tapeRolloff.setHighShelf(sampleRate, rolloffFreq, 0.6f, -12.0f); // Cut forte
    outputLPF.setLowpass(sampleRate, 19000.0f, 0.707f);
}

void TapeModel::updateParams(const TapeParams& newParams) {
    currentParams = newParams;
    dropout.setSeverity(newParams.dropoutSeverity);
    // Não chamamos updateFilters aqui para economizar CPU,
    // chame updateFilters() explicitamente se mudar Speed/Age
}

AUDIO_INLINE float TapeModel::readTapeAt(float delaySamples) {
    if (bufferSize == 0) return 0.0f;

    // Proteção de limites
    if (delaySamples < 2.0f) delaySamples = 2.0f;
    if (delaySamples > bufferSize - 4.0f) delaySamples = (float)bufferSize - 4.0f;

    float readPos = (float)writeHead - delaySamples;
    // Fast wrapping
    if (readPos < 0.0f) readPos += bufferSize;
    else if (readPos >= bufferSize) readPos -= bufferSize;

    int32_t r = (int32_t)readPos;
    float f = readPos - r;

    // Bitwise modulo para potências de 2 seria mais rápido,
    // mas aqui usamos lógica genérica segura:
    int32_t i1 = r;
    int32_t i2 = (r > 0) ? r - 1 : bufferSize - 1;
    int32_t i0 = (r < bufferSize - 1) ? r + 1 : 0;
    int32_t i3 = (i0 < bufferSize - 1) ? i0 + 1 : 0;

    // Otimização: Se usar SPIRAM, acesso é lento.
    // Ler todos de uma vez pode ajudar se houver cache line prefetch
    float d1 = delayLine[i1];
    float d0 = delayLine[i0];
    float d2 = delayLine[i2];
    float d3 = delayLine[i3];

    // Hermite Interpolation (4-point)
    float c0 = d1;
    float c1 = 0.5f * (d0 - d2);
    float c2 = d2 - 2.5f * d1 + 2.0f * d0 - 0.5f * d3;
    float c3 = 0.5f * (d3 - d1) + 1.5f * (d1 - d0);

    return ((c3 * f + c2) * f + c1) * f + c0;
}

// Marcado com IRAM_ATTR para garantir performance no ESP32
IRAM_ATTR float TapeModel::process(float input) {
    if (!delayLine) return input;

    TapeParams* p = &currentParams;

    // 1. MODULAÇÃO MECÂNICA
    // Incremento de fase otimizado (sem branch prediction fail frequente)
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

    // 2. AZIMUTH DRIFT (LFO Triangular Otimizado)
    // Substituindo a lógica pesada de asin/sin por um contador triangular simples
    azimuthPhase += (0.2f / sampleRate); // Frequencia fixa lenta
    if (azimuthPhase > 1.0f) azimuthPhase = 0.0f;

    // Gera onda triangular 0..1..0 a partir da fase linear
    float tri = (azimuthPhase < 0.5f) ? (azimuthPhase * 2.0f) : (2.0f - azimuthPhase * 2.0f);
    // Mapeia para desvio
    float azimuthMod = 0.8f + (tri * 0.4f); // Varia de 0.8 a 1.2

    // Só processa filtro Allpass se necessário (poupando CPU)
    bool useAzimuth = (p->azimuthError > 0.05f);
    if (useAzimuth) {
        azimuthFilter.setCoeff(-0.7f * p->azimuthError * azimuthMod);
    }

    // 3. LEITURA (Tape Heads)
    float tapeSignal = 0.0f;
    // Otimização: Cache calculation
    if (!p->delayActive) {
        // Modo "Thru" (apenas cor de fita)
        tapeSignal = readTapeAt(200.0f + mod * 50.0f);
    } else {
        // Multi-cabeçote (Space Echo style)
        // Se estiver em modo musical, usamos durações em função do BPM
        if (p->headsMusical) {
            float beatMs = 60000.0f / p->bpm; // duração de uma semínima (quarter) em ms
            // Head A (short): eighth-note triplet = 1/3 beat
            if (p->activeHeads & 1) tapeSignal += readTapeAt((beatMs * (1.0f/3.0f) * sampleRate * 0.001f) + (mod * 20.0f));
            // Head B (med): dotted eighth = 3/4 beat
            if (p->activeHeads & 2) tapeSignal += readTapeAt((beatMs * 0.75f * sampleRate * 0.001f) + (mod * 30.0f));
            // Head C (long): quarter note = 1 beat
            if (p->activeHeads & 4) tapeSignal += readTapeAt((beatMs * 1.0f * sampleRate * 0.001f) + (mod * 40.0f));
        } else {
            float baseDelay = p->delayActive ? (p->delayTimeMs * sampleRate * 0.001f) : 200.0f;
            if (p->activeHeads & 1) tapeSignal += readTapeAt((baseDelay * 0.33f) + (mod * 20.0f));
            if (p->activeHeads & 2) tapeSignal += readTapeAt((baseDelay * 0.66f) + (mod * 30.0f));
            if (p->activeHeads & 4) tapeSignal += readTapeAt(baseDelay + (mod * 40.0f));
        }

        // Compensação de ganho simples para multi-head
        if (p->activeHeads > 4) tapeSignal *= 0.6f;
    }

    // 4. CADEIA DE DEGRADAÇÃO (Coloração)

    // A. Dropout (fita velha falhando contato)
    float dropoutGain = dropout.process();
    tapeSignal *= dropoutGain;

    // B. Tape Noise (modulado pelo dropout - "signal dependant noise" reverso)
    if (p->noise > 0.001f) {
        // Mais ruído quando o sinal cai (dropout) ou ganho automático do amp de reprodução
        float hiss = noiseGen.next() * p->noise * (1.0f + (1.0f - dropoutGain));
        tapeSignal += hiss;
    }

    // C. Azimuth Phase Smearing
    if (useAzimuth) {
        tapeSignal = azimuthFilter.process(tapeSignal);
    }

    // D. Eq da Fita (Head Bump & High Rolloff)
    tapeSignal = headBump.process(tapeSignal);
    tapeSignal = tapeRolloff.process(tapeSignal);
    tapeSignal = outputLPF.process(tapeSignal);

    // 5. FEEDBACK LOOP
    // O sinal que volta para a fita passa pelo Saturador
    float feedbackSig = 0.0f;
    if (p->delayActive) {
        // Aplica feedback
        feedbackSig = tapeSignal * p->feedback;

        // CRÍTICO: DC Block no feedback loop previne saturação silenciosa
        feedbackSig = dcBlocker.process(feedbackSig);
    }

    // 6. GRAVAÇÃO NA FITA (Write Head)
    // Drive de entrada + Feedback -> Saturador -> Fita
    float recordSignal = (input * p->drive) + feedbackSig;

    // Hard limit safety antes do saturador complexo
    if (recordSignal > 4.0f) recordSignal = 4.0f;
    else if (recordSignal < -4.0f) recordSignal = -4.0f;

    delayLine[writeHead] = saturator(recordSignal);

    // Incremento circular
    writeHead++;
    if (writeHead >= bufferSize) writeHead = 0;

    // 7. MIX FINAL (Dry/Wet)
    return (input * (1.0f - p->dryWet)) + (tapeSignal * p->dryWet);
}
