# HYDRA TAPE ENGINE 2.0 — AUDIO VALIDATION & EQUIVALENCE PROTOCOL

## 1. Metodologia de Validação e Filosofia de Testes

Para garantir que o Hydra 2.0 mantenha rigor acústico e estabilidade em tempo real sem degradar funcionalidades consagradas, a suíte de validação é estruturada em três camadas complementares:

```text
+--------------------------------------------------------------------------+
|                         CAMADAS DE TESTE DO HYDRA                        |
+--------------------------------------------------------------------------+
| 1. TESTES DE CORREÇÃO MATEMÁTICA (Unitários rápidos, execução em CI)     |
|    - Razão de frequência e transposição tonal (+12 semitons a 2.0x).     |
|    - Complementaridade de ganhos triangulares (Gain1 + Gain2 == 1.0).    |
|    - Defasagem angular estrita de 180° entre leitores Dual-Tap.          |
|    - Ausência de NaN / Inf e integridade de limites sob estresse.        |
+--------------------------------------------------------------------------+
| 2. TESTES DE REGRESSÃO E EQUIVALÊNCIA (Bit-accurate e tolerância fina)   |
|    - Comparação determinística contra Golden CSVs para modos Forward.    |
|    - Equivalência numérica cruzada entre Nativo (GCC/Clang) e WASM.      |
|    - Validação de estabilidade de ABI e paridade de IDs de parâmetros.   |
+--------------------------------------------------------------------------+
| 3. TESTES AUDITIVOS E RENDERS LONGOS (Análise espectral e escuta)        |
|    - Renders em áudio WAV de longa duração (10 a 30 segundos).           |
|    - Estímulos variados: impulsos Dirac, senoides puras e acordes.       |
|    - Comparação A/B entre 2-Taps (Musical) e 1-Tap (Bug Cru).            |
|    - Análise espectral de aliasing sob transposição 2.5x.                |
+--------------------------------------------------------------------------+
```

---

## 2. Testes de Correção Matemática e DSP

### 2.1. Teste de Transposição Ascendente (Pitch-Up Verification)
- **Estímulo:** Senoide pura contínua de 440.0 Hz (Lá 4).
- **Configuração:** `speedMultiplier = 1.0`, grão = 80 ms, `dryWet = 100%`, `feedback = 0.0`.
- **Critério de Aceitação:** A frequência fundamental dominante da saída deve ser de aproximadamente **880.0 Hz (Lá 5)**, comprovando transposição tonal ascendente de $+12\text{ semitons}$ ($2.0\times$).
- **Tolerância:** Erro espectral $< \pm 2.5\%$, compatível com os limites da taxa de repetição do grão.

### 2.2. Teste de Complementaridade da Envoltória Dual-Tap
- **Critério Matemático:**
  $$\forall \text{phase}_1 \in [0.0, 1.0]: \quad \text{gain}_1(\text{phase}_1) + \text{gain}_2(\text{phase}_2) = 1.000000$$
  $$\text{onde } \text{phase}_2 = \text{fmod}(\text{phase}_1 + 0.5, 1.0)$$
- **Tolerância Numérica:** Erro absoluto máximo $< 1.0 \times 10^{-6}$.

### 2.3. Teste de Defasagem Estéreo (Stereo Warp Offset)
- **Configuração:** `stereoWarp = 1.0` (offset de $0.25$ no ciclo de grão, equivalente a $90^\circ$).
- **Critério:** Os picos de envoltória e instantes de crossfade do canal direito devem estar defasados no tempo em relação ao canal esquerdo por um quarto do período do grão, mantendo correlação espectral de pitch idêntica ($0\text{ cents}$ de diferença entre canais).

### 2.4. Teste de Estabilidade e Auto-Oscilação Segura
- **Configuração:** `feedback = 95.0%`, sinal de entrada = impulso de 0 dBFS no instante zero seguido de silêncio absoluto por 5 segundos.
- **Critério:**
  - O sinal de saída deve sustentar realimentação densa sem divergência assintótica.
  - Pico máximo de amplitude estritamente contido em $[-0.99, +0.99]$ pelo limitador suave.
  - Zero ocorrências de `NaN` ou `Infinity`.

---

## 3. Protocolo de Validação no ESP32-S3 Hardware

Para atestar o comportamento em hardware físico (ESP32-S3 DevKitC-1 N16R8):

1. **Monitoramento de Underruns de I2S DMA:**
   - Taxa de transferência configurada a 44.1 kHz, 16 bits estéreo, blocos de DMA de 32 a 64 frames.
   - O indicador de underrun deve permanecer em zero durante operação ininterrupta de 60 minutos sob comutação contínua de parâmetros.
2. **Tempo de Execução por Bloco (CPU Load):**
   - A chamada `TapeCore::processStereo` para um bloco de 32 amostras deve consumir menos de $450\,\mu\text{s}$ no núcleo dedicado (orçamento disponível a 44.1 kHz: $725\,\mu\text{s}$).
   - Folga de processamento garantida $> 35\%$.
3. **Integridade da PSRAM:**
   - Ausência de corrupção de áudio decorrente de concorrência com o barramento WiFi/Bluetooth quando a linha de atraso é mapeada em `MALLOC_CAP_SPIRAM`.
