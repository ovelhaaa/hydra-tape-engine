# ACCIDENTAL WARP — DSP MATHEMATICAL & SONIC SPECIFICATION

## 1. Identidade Sonora e Princípio Físico

O **Accidental Warp** é o principal diferencial experimental do Hydra Tape Engine. Trata-se de um processador granular de tempo contínuo que produz **transposição tonal exclusivamente ASCENDENTE (Pitch-Up)**.

Historicamente, versões preliminares da interface de usuário continham terminologias equivocadas sugerindo "sub-oitavas" ou "congelamento estático". Esta especificação corrige formalmente essa definição:
- **O Accidental Warp acelera a leitura da fita em relação à velocidade de gravação.**
- O resultado é uma cascata de repetições ascendentes (arpejos tonais que sobem continuamente de afinação a cada iteração no laço de realimentação).
- O caráter do efeito não é o de um pitch shifter clássico baseado em FFT (Phase Vocoder) ou WSOLA asséptico, mas sim o de um dispositivo de fita com cabeças rotativas que varrem a fita em contra-movimento, gerando micro-glitches rítmicos, dobras harmônicas e saturação orgânica.

---

## 2. Relação Matemática de Velocidade e Transposição

No protótipo original de referência do simulador AudioWorklet:
- O ponteiro de gravação físico avança rigorosamente a $1.0\text{ amostra/frame}$.
- O ponteiro virtual de leitura avança em taxa adicional determinada por $\text{speedMultiplier}$.
- A velocidade efetiva de leitura sobre o áudio gravado na fita é aproximadamente:
  $$v_{eff} \approx 1.0 + \text{speedMultiplier}$$

| `speedMultiplier` | Velocidade Efetiva ($v_{eff}$) | Relação de Frequência | Intervalo Musical Transposto | Caráter Perceptual |
| :---: | :---: | :---: | :---: | :--- |
| **0.0** | $1.0\times$ | $1.0 : 1$ | Uníssono (0 semitons) | Delay normal, repetições na mesma afinação da fonte. |
| **0.5** | $1.5\times$ | $3 : 2$ | Quinta Justa Superior (+7 semitons) | Cascata de quintas harmônicas brilhantes. |
| **1.0** | $2.0\times$ | $2 : 1$ | Oitava Superior (+12 semitons) | Oitavas ascendentes puras (efeito shimmer analógico). |
| **1.5** | $2.5\times$ | $5 : 2$ | Décima Maior / 1 Oitava + 3ª Maior (+16 semitons) | Transposição harmônica ultra-rápida e brilhante. |

> [!IMPORTANT]
> É categoricamente proibido reinterpretar `speedMultiplier` como transposição descendente (pitch-down). A física do algoritmo requer que valores positivos acelerem a leitura relativa, elevando a frequência fundamental do sinal reproduzido.

---

## 3. Topologia de Leitura Dual-Tap com Crossfade Complementar

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

### 3.1. Formulação Matemática por Amostra
Para cada amostra $n$, dado o avanço da posição virtual $\text{virtualPos}$ e o tamanho do grão em amostras $\text{grainPeriodSamples} = \text{grainPeriodMs} \times 0.001 \times f_s$:

1. **Avanço da Posição Virtual:**
   $$\text{virtualPos} \leftarrow \text{virtualPos} + \text{speedMultiplier}$$
2. **Cálculo da Fase do Tap 1 (Normalizada entre 0.0 e 1.0):**
   $$\text{phase}_1 = \text{fmod}\left(\frac{\text{virtualPos}}{\text{grainPeriodSamples}}, 1.0\right)$$
   $$\text{se } \text{phase}_1 < 0 \implies \text{phase}_1 \leftarrow \text{phase}_1 + 1.0$$
3. **Cálculo da Fase do Tap 2 (Defasagem Estrita de 180°):**
   $$\text{phase}_2 = \text{fmod}(\text{phase}_1 + 0.5, 1.0)$$
4. **Envoltórias de Ganho Triangulares Complementares:**
   $$\text{gain}_1 = 1.0 - |2.0 \cdot \text{phase}_1 - 1.0|$$
   $$\text{gain}_2 = 1.0 - |2.0 \cdot \text{phase}_2 - 1.0|$$
5. **Distâncias de Atraso em Relação ao Ponteiro de Gravação:**
   $$\text{delay}_1 = (1.0 - \text{phase}_1) \cdot \text{grainPeriodSamples}$$
   $$\text{delay}_2 = (1.0 - \text{phase}_2) \cdot \text{grainPeriodSamples}$$
6. **Leitura Interpolada e Recombinação:**
   $$s_1 = \text{readTapeHermite4}(\text{delay}_1)$$
   $$s_2 = \text{readTapeHermite4}(\text{delay}_2)$$
   $$y_{\text{warp}} = s_1 \cdot \text{gain}_1 + s_2 \cdot \text{gain}_2$$

---

## 4. Diferenciação de Modos: 2-Taps (Musical) vs. 1-Tap (Bug Cru)

O protótipo de referência implementa dois modos de operação que são mantidos no motor para validação acústica:

1. **2-Taps (Musical — Padrão de Produção):**
   - Utiliza a interpolação cruzada descrita na Seção 3.
   - Crossfade suave entre os dois grãos, mascarando os momentos de reinicialização de ponteiro.
   - Textura límpida e harmônica, ideal para cascatas musicais e pads ressonantes.
2. **1-Tap (Bug Cru — Referência de Laboratório):**
   - Desativa o segundo leitor e o janelamento triangular ($\text{gain}_1 = 1.0, \text{gain}_2 = 0.0$).
   - A leitura varre continuamente a distância de atraso e, ao atingir a fronteira do grão ($\text{phase}_1 = 1.0$), salta bruscamente de volta para a distância inicial.
   - Gera estalos rítmicos percussivos, batimentos e artefatos de descontinuidade que conferem uma textura lofi/broken altamente expressiva.

---

## 5. Stereo Warp (Espacialização por Deslocamento de Fase)

Para obter uma imagem estéreo expansiva e dinâmica sem incorrer em desafinação assimétrica permanente entre os ouvidos (o que desorientaria a harmonia):
- A velocidade de transposição $\text{speedMultiplier}$ é **idêntica** em ambos os canais, preservando a exata mesma afinação.
- O canal direito recebe uma **defasagem no ciclo do grão**:
  $$\text{phase}_{R} = \text{fmod}\left(\text{phase}_{L} + \text{stereoWarp} \cdot 0.25, 1.0\right)$$
- Com $\text{stereoWarp} = 0.0$, ambos os canais transitam em uníssono de fase (imagem estéreo mono-compatível centrada).
- Com $\text{stereoWarp} = 1.0$, os cruzamentos de grãos do canal direito ocorrem $90^\circ$ defasados em relação ao canal esquerdo, criando uma ilusão de movimento orbital contínuo e textura percussiva alternada.

---

## 6. Laço de Feedback do Warp & Proteção de Estabilidade

O sinal gerado pela leitura acelerada do Warp precisa ser realimentado na cabeça de gravação magnética para gerar as cascatas infinitas. A cadeia de condicionamento dedicada segue o fluxo:

$$\text{Warp Read} \longrightarrow \text{HPF (120 Hz)} \longrightarrow \text{LPF (8 kHz)} \longrightarrow \text{Soft Saturation} \longrightarrow \text{Feedback Gain} \longrightarrow \text{Tape Write}$$

1. **Filtragem Passa-Altas (HPF):** Remove o acúmulo de subgraves e "thumps" decorrentes da modulação dos grãos.
2. **Filtragem Passa-Baixas Anti-Aliasing (LPF):** Leituras em velocidade $2\times$ ou $2.5\times$ dobram as frequências do sinal lido. Sem filtragem antes da regravação na fita, componentes acima de $f_s / 4$ gerariam rebatimento de aliasing agressivo na realimentação.
3. **Soft Saturation:** Aplicação de compressão racional `fastSatRational` para evitar que picos ressonantes excedam $1.0$, mantendo a saturação redonda e musical.
4. **Limite Seguro de Realimentação:** O ganho máximo de feedback é calibrado abaixo de $+0.92$, evitando runaways incontroláveis.

---

## 7. Registro de Limitação do Workspace

> [!NOTE]
> O arquivo HTML original do simulador fornecido pelo usuário (*"Accidental Warp Delay — Simulador DSP"*) não se encontrava pré-alocado na árvore de arquivos do repositório local.
> Conforme estipulado nas diretrizes do projeto, esta limitação é formalmente documentada aqui. A implementação em C++ nativo e as suites de validação utilizam integralmente a especificação matemática e paramétrica descrita nesta documentação. Comparações adicionais com o arquivo HTML original serão consolidadas na fase de refinamento em escuta crítica.
