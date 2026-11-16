# HYDRA TAPE ENGINE 2.0 — DSP ARCHITECTURE SPECIFICATION

## 1. Princípios Arquiteturais & Diretrizes Estruturais

A arquitetura de processamento do Hydra 2.0 adota uma abordagem estritamente unificada:
- **Núcleo Canônico Único:** Todo o processamento reside em `dsp/core/tape_core.cpp`, exposto externamente pela interface C em `include/hydra_dsp.h` e encapsulado em `TapeCore::Impl`.
- **Memória de Fita Compartilhada:** Não existem buffers redundantes ou motores paralelos isolados para Tape, Reverse e Warp. A mídia física simulada (linha de retardo magnética estéreo) é única. Leituras clássicas (3-head), reversas e granulares (Warp) atuam como cabeças virtuais sobre o mesmo trilho de memória circular.
- **Coloração e Degradação Compartilhadas:** Os estágios de saturação magnética, resposta de frequência das cabeças (head bump e gap loss), desvio mecânico (wow/flutter) e realimentação condicionada são compartilhados entre os modos para garantir coerência tímbrica e eficiência computacional em microcontroladores.
- **Determinismo e Tempo Real:** Zero alocações dinâmicas de memória (`malloc`, `new`, `realloc`), zero chamadas de sistema bloqueantes e zero operações de I/O em tempo de processamento. Todas as funções no caminho de áudio possuem complexidade estrita $O(1)$.

---

## 2. Diagrama de Fluxo de Áudio (End-to-End Signal Chain)

```text
===================================================================================
                               HYDRA DSP SIGNAL FLOW
===================================================================================

                [ Left Input ]                     [ Right Input ]
                       |                                  |
               +-------v-------+                  +-------v-------+
               |   Input HPF   |                  |   Input HPF   |
               | (Guitar/Full) |                  | (Guitar/Full) |
               +-------+-------+                  +-------+-------+
                       |                                  |
               +-------v-------+                  +-------v-------+
               |   Input LPF   |                  |   Input LPF   |
               +-------+-------+                  +-------+-------+
                       |                                  |
           [ Conditioned Input L ]            [ Conditioned Input R ]
                       |                                  |
                       v                                  v
         +-------------+-------------+      +-------------+-------------+
         | Record Drive Scale        |      | Record Drive Scale        |
         | + Feedback Summation      |      | + Feedback Summation      |
         +-------------+-------------+      +-------------+-------------+
                       |                                  |
               +-------v-------+                  +-------v-------+
               |   DC Blocker  |                  |   DC Blocker  |
               +-------+-------+                  +-------+-------+
                       |                                  |
               +-------v-------+                  +-------v-------+
               | TapeMagnetics |                  | TapeMagnetics |
               | (Tanh LUT+m)  |                  | (Tanh LUT+m)  |
               +-------+-------+                  +-------+-------+
                       |                                  |
                       v                                  v
     [ WRITE HEAD L ] -> [ BUFFER L ]       [ WRITE HEAD R ] -> [ BUFFER R ]
     ================================       ================================
             CIRCULAR STEREO TAPE MEMORY WITH 4-SAMPLE GUARD BAND
     ================================       ================================
           |             |            \       /            |             |
           |             |             \     /             |             |
      [Mode A: 3-Head] [Mode B: Rev]  [Mode C: Warp]  [Mode B: Rev] [Mode A: 3-Head]
      Hermite4 Interp  Windowed Rev    Dual-Tap Warp   Windowed Rev  Hermite4 Interp
           |             |                   |             |             |
           +-------------+-------------------+-------------+-------------+
                                     |
                           [ Raw Playback Signal ]
                                     |
                          +----------v----------+
                          |   Azimuth Allpass   |
                          +----------+----------+
                                     |
                          +----------v----------+
                          |  Noise & Hiss Sum   |
                          | (Ducked by Input)   |
                          +----------+----------+
                                     |
                          +----------v----------+
                          |  Dropout Multiplier |
                          +----------+----------+
                                     |
                          +----------v----------+
                          |  Repro Head Bump EQ |
                          +----------+----------+
                                     |
                          +----------v----------+
                          |   Tape Rolloff EQ   |
                          +----------+----------+
                                     |
                          +----------v----------+
                          | Playback Gap Loss   |
                          +----------+----------+
                                     |
                 +-------------------+-------------------+
                 |                                       |
                 v                                       v
         [ Feedback Node ]                        [ Dry/Wet Mixer ]
                 |                                       |
         +-------v-------+                       +-------v-------+
         | Feedback HPF  |                       | Spring Reverb |
         +-------+-------+                       +-------+-------+
                 |                                       |
         +-------v-------+                       +-------v-------+
         | Head Bump EQ  |                       | Output Limiter|
         +-------+-------+                       +-------+-------+
                 |                                       |
         +-------v-------+                               v
         | Gap Loss Filter                       [ OUTPUT L / R ]
         +-------+-------+
                 |
         +-------v-------+
         | Pre-Emphasis  |
         +-------+-------+
                 |
         +-------v-------+
         | Dual Staged   |
         | Saturation    |
         +-------+-------+
                 |
         +-------v-------+
         | De-Emphasis   |
         +-------+-------+
                 |
         +-------v-------+
         | Compressor &  |
         | Gain Limit    |
         +-------+-------+
                 |
                 +---------> (Return to Record Summation)
```

---

## 3. Gestão de Memória, Alocação e Orçamento de Recursos (ESP32-S3)

### 3.1. Orçamento de Memória em Hardware
A plataforma primária é o **ESP32-S3 N16R8**:
- **Flash:** 16 MB com particionamento LittleFS e bootloader seguro.
- **SRAM Interna (DIRAM):** ~320 KB livres para o runtime do sistema.
- **Octal PSRAM (OPI):** 8 MB acessível via barramento SPI de alta velocidade com cache de memória integrado.

```text
+-------------------------------------------------------------------------+
| ESP32-S3 MEMORY MAP (HYDRA TAPE ENGINE 2.0)                             |
+------------------------------------+------------------------------------+
| INTERNAL SRAM (Fast Data Cache)    | OCTAL PSRAM (8 MB High Capacity)   |
+------------------------------------+------------------------------------+
| - TapeCore::Impl Control Struct    | - delayLine (Canal Esquerdo):      |
| - TanhLUT (1024 floats = 4 KB)     |   fs * maxDelayMs * sizeof(float)  |
| - Filter coefficients & states     |   Ex: 48 kHz * 2.0 s = 96.000 spls |
| - LCG noise states & modulation    |   96.004 * 4 bytes = 384 KB        |
| - Biquad internal states (z1, z2)  | - delayLineR (Canal Direito):      |
| - Audio I2S DMA buffers (32 spls)  |   96.004 * 4 bytes = 384 KB        |
| Total SRAM usada: < 32 KB          | Total PSRAM usada (2s): ~768 KB    |
| (Margem livre: > 280 KB)           | (Margem livre: > 7.2 MB)           |
+------------------------------------+------------------------------------+
```

### 3.2. Estratégia de Alocação e Guard Band
O buffer circular de atraso inclui uma zona de proteção (`TAPE_BUFFER_GUARD = 4 amostras`) logo após o término da capacidade útil ($N = \text{bufferSize}$):
```cpp
bufferCapacity = bufferSize + TAPE_BUFFER_GUARD;
```
Ao atualizar a cabeça de gravação nos primeiros índices ($0 \le \text{writeHead} < 4$), as amostras são espelhadas para os índices de guarda ($N, N+1, N+2, N+3$). Isso viabiliza que o interpolador cúbico **Hermite4** execute leituras contínuas com ponteiros fracionários sem exigir ramificações de `modulo` ($O(1)$ branches) para os quatro pontos adjacentes ($y_{-1}, y_0, y_1, y_2$).

### 3.3. Cuidados de Latência e Cache de PSRAM
O barramento Octal PSRAM possui latência de acesso consideravelmente superior à SRAM interna (cerca de 20 a 35 ciclos de clock vs. 1 ciclo). Para mitigar penalidades de cache miss no ESP32-S3:
1. As leituras nos buffers de retardo ocorrem de forma contígua em blocos processados sequencialmente.
2. Nenhuma tabela de saturação, coeficientes de biquad ou variáveis temporárias reside na PSRAM; estes são obrigatoriamente alocados na memória interna rápida.
3. A macro `HYDRA_TAPE_CORE_USE_PSRAM` invoca `heap_caps_malloc(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)`, mantendo fallback automático para SRAM interna caso a alocação em PSRAM não esteja disponível em ambientes de teste.

---

## 4. Estratégias Especializadas de Leitura de Fita

### 4.1. Leitura Forward Tradicional (3 Cabeças)
A distância de cada cabeça em relação ao ponteiro de gravação $\text{writeHead}$ é dada por:
$$d_k = \text{clamp}(d_{base} \cdot \alpha_k + \text{mod}(t), 2, \text{bufferSize} - 4)$$
Onde $\text{mod}(t)$ representa a componente dinâmica combinada de wow, flutter e scrape. A interpolação fracionária utiliza o polinômio cúbico de Hermite:
$$y(f) = \left(\left(c_3 \cdot f + c_2\right) \cdot f + c_1\right) \cdot f + c_0$$
$$c_0 = y_0$$
$$c_1 = 0.5 \cdot (y_1 - y_{-1})$$
$$c_2 = y_{-1} - 2.5 \cdot y_0 + 2 \cdot y_1 - 0.5 \cdot y_2$$
$$c_3 = 0.5 \cdot (y_2 - y_{-1}) + 1.5 \cdot (y_0 - y_1)$$

### 4.2. Leitura Reversa Janelada (Mode B)
Ao invés de ler no mesmo sentido do transporte mecânico, a cabeça reversa aplica incremento progressivo à distância de atraso dentro de uma janela rítmica finita $W$:
$$\text{distância}(t) = d_{base} - \text{counter}(t)$$
À medida que o tempo avança, a distância aumenta, o que equivale a retroceder no passado da fita gravada, gerando a inversão temporal efetiva do trecho de áudio.

### 4.3. Leitura Granular Accidental Warp (Mode C)
O Accidental Warp adota uma abordagem de leitura virtual acelerada através de dois pontos de escuta independentes sobre o mesmo buffer circular, com ganho triangular defasado em 180° e avanço assimétrico do ponteiro, produzindo transposição **ASCENDENTE** calibrada. Os detalhes matemáticos integrais estão formalizados em `docs/ACCIDENTAL_WARP_SPEC.md`.

---

## 5. Estágio de Realimentação e Degradação Magnética

Para preservar a estabilidade incondicional sob ganho unitário e realimentação infinita:
1. **Filtro Passa-Altas de Feedback (`feedbackHPF`):** Biquad passa-altas calibrado em 300 Hz ($Q = 0.5$), eliminando acúmulo de energia subsônica e saturação por offset contínuo.
2. **Head Bump & Rolloff de Feedback:** Ressonância em baixas frequências (60 Hz a 200 Hz conforme velocidade da fita) e gap loss de polo único (`feedbackGapLoss`), escurecendo progressivamente as repetições.
3. **Pré-Ênfase e Desênfase:** O sinal é realçado em altas frequências ($\times 1.12$) antes da saturação e atenuado de forma rigorosamente complementar ($\times \frac{1}{1.12}$) imediatamente após, gerando harmônicos naturais sem aspereza estridente.
4. **Saturação de Duplo Estágio (`fastSatRational`):** Modelagem racional de saturação suave:
   $$S(x) = \text{clamp}\left(x \cdot \frac{27 + x^2}{27 + 9x^2}, -1.0, 1.0\right)$$
5. **Compressor Suave de Realimentação (`feedbackCompressor`):** Atua como soft-knee dinâmico acima de $|x| = 0.6$, assegurando que feedback próximo a 100% resulte em auto-oscilação rica e controlada, nunca em estalo digital ou estouro de amplitude.

---

## 6. Governança da API Pública C e Estabilidade de ABI

A biblioteca externa consome exclusivamente `include/hydra_dsp.h`. Para manter compatibilidade binária e semântica com o host WebAssembly e o firmware ESP32:
- **Enumeração Rígida de Parâmetros:** IDs existentes de $0$ a $29$ são imutáveis e jamais renumerados.
- **Extensão de Parâmetros de Modo Warp:** Parâmetros adicionais são anexados a partir do índice $30$, acompanhados da atualização correspondente de `HYDRA_DSP_PARAM_COUNT`.
- **Versionamento Semântico da API:**
  - `HYDRA_DSP_API_VERSION_MAJOR`: Alterado apenas em caso de ruptura incompatível de ABI.
  - `HYDRA_DSP_API_VERSION_MINOR`: Incrementado na adição de parâmetros ou modos backward-compatible.
  - `HYDRA_DSP_API_VERSION_PATCH`: Incrementado em correções internas de precisão ou otimizações.
