# ACCIDENTAL WARP V1.1 — DSP MATHEMATICAL & SONIC SPECIFICATION

## 1. Identidade Sonora e Princípio Físico

O **Accidental Warp** é o principal diferencial experimental do Hydra Tape Engine. Trata-se de um processador granular de tempo contínuo que produz **transposição tonal exclusivamente ASCENDENTE (Pitch-Up)**.

Historicamente, versões preliminares da interface de usuário continham terminologias equivocadas sugerindo "sub-oitavas" ou "congelamento estático". Esta especificação corrige formalmente essa definição:
- **O Accidental Warp acelera a leitura da fita em relação à velocidade de gravação.**
- O resultado é uma cascata de repetições ascendentes (arpejos tonais que sobem continuamente de afinação a cada iteração no laço de realimentação).
- O caráter do efeito não é o de um pitch shifter clássico baseado em FFT (Phase Vocoder) ou WSOLA asséptico, mas sim o de um dispositivo de fita com cabeças rotativas que varrem a fita em contra-movimento, gerando micro-glitches rítmicos, dobras harmônicas e saturação orgânica.

---

## 2. Relação Matemática de Velocidade e Transposição

No protótipo original de referência do simulador (`docs/simulators/accidental_warp_simulator.html`):
- O ponteiro de gravação físico avança rigorosamente a $1.0\text{ amostra/frame}$.
- O ponteiro virtual de leitura avança em taxa adicional determinada por $\text{speedMultiplier}$.
- A velocidade efetiva de leitura sobre o áudio gravado na fita é rigorosamente:
  $$v_{eff} = 1.0 + \text{speedMultiplier}$$

| `speedMultiplier` | Velocidade Efetiva ($v_{eff}$) | Relação de Frequência | Intervalo Musical Transposto | Caráter Perceptual |
| :---: | :---: | :---: | :---: | :--- |
| **0.0** | $1.0\times$ | $1.0 : 1$ | Uníssono (0 semitons) | Delay normal, repetições na mesma afinação da fonte. |
| **0.5** | $1.5\times$ | $3 : 2$ | Quinta Justa Superior (+7 semitons) | Cascata de quintas harmônicas brilhantes. |
| **1.0** | $2.0\times$ | $2 : 1$ | Oitava Superior (+12 semitons) | Oitavas ascendentes puras (efeito shimmer analógico). |
| **1.5** | $2.5\times$ | $5 : 2$ | Décima Maior / 1 Oitava + 3ª Maior (+16 semitons) | Transposição harmônica ultra-rápida e brilhante. |

> [!IMPORTANT]
> É categoricamente proibido reinterpretar `speedMultiplier` como transposição descendente (pitch-down). A física do algoritmo requer que valores positivos acelerem a leitura relativa, elevando a frequência fundamental do sinal reproduzido.

---

## 3. Topologia de Leitura Dual-Tap com Crossfade Complementar e Continuidade de Fase

Para viabilizar uma leitura musicalmente contínua sem estalos abruptos de descontinuidade de ponteiro, o algoritmo opera com **dois pontos de leitura virtuais simultâneos (Tap 1 e Tap 2)** sobre a mesma memória de fita:

```text
Fase 1 (Tap 1):  0.0 ----------------> 0.5 ----------------> 1.0 (wrap to 0.0)
Ganho 1:         0.0 ----------------> 1.0 ----------------> 0.0 (triangular)
Distância 1:     Period -------------> 0.5*Period ---------> 0.0

Fase 2 (Tap 2):  0.5 ----------------> 1.0 (wrap) ---------> 0.5 (defasado em 180°)
Ganho 2:         1.0 ----------------> 0.0 ----------------> 1.0 (triangular)
Distância 2:     0.5*Period ---------> 0.0 ----------------> 0.5*Period

Soma dos Ganhos: Gain1 + Gain2 = 1.00000000 (Complementaridade Exata)
```

### 3.1. Rastreamento de Fase Normalizada e Continuidade de Grão

Na V1.1, a posição do leitor virtual é mantida em **fase normalizada** $\text{warpPhase} \in [0.0, 1.0)$:

1. **Suavização do Tamanho de Grão:**
   $$\text{targetGrainSamples} = \max(64.0, \text{warpGrainMs} \times 0.001 \times f_s)$$
   $$\text{smoothedGrainSamples} \leftarrow \text{smoothedGrainSamples} + 0.005 \times (\text{targetGrainSamples} - \text{smoothedGrainSamples})$$

2. **Avanço de Fase Contínuo:**
   $$\Delta \text{phase} = \frac{\text{speedMultiplier}}{\text{smoothedGrainSamples}}$$
   $$\text{warpPhase} \leftarrow \text{warpPhase} + \Delta \text{phase}$$
   $$\text{se } \text{warpPhase} \ge 1.0 \implies \text{warpPhase} \leftarrow \text{warpPhase} - 1.0$$
   $$\text{se } \text{warpPhase} < 0.0 \implies \text{warpPhase} \leftarrow \text{warpPhase} + 1.0$$

Essa formulação garante que, quando o usuário altera `warpGrainMs`, a fase instantânea dos osciladores de grão não salta, eliminando estalos durante giros de potenciômetro.

---

## 4. Modos de Operação (Hermite vs. Raw Int Cast, 2-Taps vs. 1-Tap)

O Hydra 2.0 Accidental Warp V1.1 implementa formalmente os 4 modos extraídos do simulador:

1. **Modo 0: 2-Taps Musical (Hermite — Padrão de Produção):**
   - Interpolação cúbica Hermite de 4 pontos nos dois taps.
   - Crossfade triangular contínuo ($\text{gain}_1 + \text{gain}_2 = 1.0$).
   - Textura suave, shimmer brilhante e ausência de estalos de descontinuidade.
2. **Modo 1: 1-Tap Bug Cru (Hermite):**
   - Single-pointer com compensação de offset de +1000 amostras (quirk do protótipo).
   - Envoltória de desclicagem de 5% nas bordas:
     $$\text{env} = \begin{cases} \text{normPos} / 0.05, & \text{se } \text{normPos} < 0.05 \\ (1.0 - \text{normPos}) / 0.05, & \text{se } \text{normPos} > 0.95 \\ 1.0, & \text{caso contrário} \end{cases}$$
   - Interpolação Hermite. Produz artefatos percussivos rítmicos analógicos.
3. **Modo 2: 1-Tap Bug Cru (Raw Int Cast):**
   - Mesma topologia do Modo 1, porém com truncamento inteiro explícito: $\text{relD} = \lfloor \text{relD} \rfloor$.
   - Leitura de amostras inteiras sem interpolação polinomial.
   - Textura digital quebrada, bit-crushed, com artefatos de sample-skipping.
4. **Modo 3: 2-Taps Musical (Raw Int Cast):**
   - Dual-tap com crossfade complementar de 180°, porém com leitura em posições inteiras truncadas.
   - Mantém a fluidez de envelope sem estalos de fronteira, mas com o "crunch" harmônico característico de clock digital imperfeito.

---

## 5. Stereo Warp (Espacialização por Deslocamento de Fase)

Para obter imagem estéreo expansiva sem introduzir desafinação entre os canais:
- $\text{speedMultiplier}$ é idêntico em ambos os canais (afinação uniforme).
- O canal direito recebe uma defasagem pura no ciclo do grão:
  $$\text{phase}_{R1} = \text{fmod}\left(\text{phase}_{L1} + \text{stereoWarp} \cdot 0.25, 1.0\right)$$
  $$\text{phase}_{R2} = \text{fmod}\left(\text{phase}_{R1} + 0.5, 1.0\right)$$
- Com `warpStereo = 0.0`, imagem mono-compatível centrada.
- Com `warpStereo = 1.0`, os cruzamentos de grão do canal direito operam $90^\circ$ em quadratura em relação ao canal esquerdo, criando ampla textura espacial sem desarmonia.

---

## 6. Arquitetura de Feedback e Separação de Roteamento

### 6.1. Separação Conceitual dos Três Tipos de Feedback

1. **Feedback Convencional de Fita (`feedback`):**
   - Recircula a saída das cabeças de reprodução fixas através da EQ de cabeçote, pre-ênfase, saturação magnética e compressor de fita.
   - Ativo apenas quando `warpActive == false`.
2. **Warp Feedback (`warpFeedback`):**
   - Recircula exclusivamente o sinal processado pelo Accidental Warp.
   - Se `warpFeedback == 0.0f`, a realimentação é estritamente **zero** (não há fallback silencioso para o feedback de fita).
   - Cada recirculação sofre nova transposição ascendente, gerando cascatas tonais crescentes.
   - Níveis calibrados de 0% a 95% com estabilização por compressor de pico, garantindo ausência de NaN, Inf e estouro de amplitude.
3. **Spiral Feedback (Hydra 2.5 Roadmap):**
   - Matriz de roteamento cruzado entre cabeças de fita convencionais e cabeça Warp, permitindo realimentações entrelaçadas em proporções controladas.

### 6.2. Separação de Filtros de Saída vs. Filtros de Feedback

No protótipo original:
- **Sinal de Saída:** O leitor granular envia o áudio lido diretamente para o mixer dry/wet, preservando todo o brilho das frequências agudas e o ar harmônico gerado pela transposição.
- **Sinal de Retorno (Loop de Realimentação):** O retorno é condicionado isoladamente por:
  $$\text{Warp Return} \longrightarrow \text{HPF (120 Hz)} \longrightarrow \text{LPF (2.2 kHz)} \longrightarrow \text{Soft Drive (tanh/rational)} \longrightarrow \text{warpFeedback} \longrightarrow \text{Compressor}$$
  - **HPF (120 Hz, Q=0.707):** Elimina o acúmulo de sub-graves e offsets DC que desestabilizariam o laço.
  - **LPF (2200 Hz, Q=0.707):** Amortece o acúmulo descontrolado de harmônicos ultra-altos em cada ciclo de pitch-up.
  - **Soft Saturation:** Compressão suave que confere compressão de fita analógica e estabilidade estrita.

---

## 7. Compatibilidade de API e Segurança de Memória

A estrutura `hydra_dsp_params` foi expandida na API 1.3 de 29 campos (116 bytes) para 35 campos (140 bytes). Para proteger clientes antigos contra leituras fora dos limites (out-of-bounds reads):
- Introdução de `hydra_dsp_set_params_sized(handle, params, struct_size)`.
- Se `struct_size == HYDRA_DSP_PARAMS_SIZE_V1_2` (116 bytes), os parâmetros do Warp são inicializados automaticamente com valores seguros (`warpActive = 0`, `warpSpeed = 1.0`, etc.) sem acessar memória além do buffer do cliente.
- Tamanhos menores que 116 bytes são rejeitados com código de erro `-2`.
- A função padrão `hydra_dsp_set_params` encaminha com `sizeof(hydra_dsp_params)`, mantendo compatibilidade total de ABI.
