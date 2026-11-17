# HYDRA GLOBAL TRANSPORT CLOCK (GTC)
## Sample-Accurate Musical Synchronization Infrastructure

**Versão da API:** 1.5.0  
**Autor:** Hydra Core DSP Engineering Team  
**Status:** VALIDATED & RELEASE READY  
**Componentes Principais:** `dsp/core/global_transport_clock.hpp`, `dsp/core/tape_core.hpp`, `include/hydra_dsp.h`

---

## 1. Visão Geral e Filosofia Arquitetural

O **Global Transport Clock (GTC)** estabelece uma infraestrutura temporal unificada, determinística e sample-accurate para todos os módulos de processamento de áudio do Hydra Tape Engine.

Historicamente, cada efeito (delay clássico, reverse echo, granular warp) mantinha temporizadores analógicos e contadores discretos próprios, desacoplados entre si. Com o GTC:
- **Centralização do Tempo Musical:** BPM global, contagem de beats, contagem de compassos, fases rítmicas e fronteiras de ciclo derivam de uma fonte canônica e monotônica única.
- **Descentralização do Processamento de Áudio:** Cada efeito permanece autônomo em suas linhas de atraso, filtros, saturação e modulações analógicas.
- **Isolamento de Modulações Livres:** Wow/flutter estocástico, ruído mecânico e modulações livres permanecem intencionalmente não-sincronizados, preservando o calor e a imperfeição física da fita magnética.

---

## 2. Convenções Temporais e Representação Matemática

### 2.1 Unidade Canônica
A posição musical é representada em **beats de semínima (quarter-note beats)** com origem monotônica no instante $t = 0.0$:
- $\text{beat } 0.0$ corresponde ao marco zero do transporte.
- $\text{beatIndex} = \lfloor m \rfloor \in \mathbb{Z}$.
- $\text{beatPhase} = m - \lfloor m \rfloor \in [0.0, 1.0)$.

### 2.2 Política de Amostragem (Frame Start Convention)
Todas as consultas de estado (`musicalPosition()`, `beatIndex()`, `beatPhase()`, `barIndex()`, `barPhase()`, `isSubdivisionBoundary()`) **refletem o estado exato no INÍCIO do frame de áudio corrente**, antes do avanço do relógio (`tick()`).
O avanço do relógio ocorre estritamente ao final do processamento da amostra:
```cpp
// 1. Consulta o estado no início da amostra
bool isBoundary = clock.isSubdivisionBoundary(subdiv);
ReverseFrame frame = reverse.next(isBoundary, syncToClock);

// 2. Processa o DSP da amostra
processAudioSample(...);

// 3. Avança o relógio para a próxima amostra
clock.tick();
```
Essa convenção elimina ambiguidades de um frame (*off-by-one errors*) entre processamento por amostra e em bloco.

### 2.3 Ausência Absoluta de Deriva Cumulativa (Zero Drift Formulation)
Em vez de acumulações sucessivas de ponto flutuante susceptíveis a erro de arredondamento IEEE 754, o GTC utiliza uma equação fechada com respeito à contagem inteira de amostras de 64 bits desde o último locate/BPM change:
$$m(n) = m_{\text{origin}} + (n - n_{\text{origin}}) \cdot \Delta m$$
onde $\Delta m = \frac{\text{BPM}}{60.0 \cdot f_s}$.

Em uma sessão contínua de 100 milhões de frames (~34,7 minutos a 48 kHz, correspondendo a $\frac{100\,000\,000}{48\,000 \cdot 60} \approx 34,72$ min), a deriva máxima é $< 10^{-9}$ beats ($< 10^{-7}$ amostras).

---

## 3. Subdivisões Rítmicas Racionais

O GTC suporta 10 subdivisões musicais estruturadas via razões inteiras exatas $(p, q)$, onde o ciclo do evento ocorre quando $\lfloor \frac{p \cdot m}{q} \rfloor$ incrementa:

| Subdivisão | Enum ID | Duração Nominal ($b_S$) | $p$ | $q$ | Relação de Ciclo |
|---|:---:|:---:|:---:|:---:|---|
| **1/1 (Whole)** | `Whole (0)` | 4.0 beats | 1 | 4 | $\lfloor m / 4 \rfloor$ |
| **1/2 (Half)** | `Half (1)` | 2.0 beats | 1 | 2 | $\lfloor m / 2 \rfloor$ |
| **1/4 (Quarter)** | `Quarter (2)` | 1.0 beat | 1 | 1 | $\lfloor m \rfloor$ |
| **1/8 (Eighth)** | `Eighth (3)` | 0.5 beat | 2 | 1 | $\lfloor 2m \rfloor$ |
| **1/16 (Sixteenth)** | `Sixteenth (4)` | 0.25 beat | 4 | 1 | $\lfloor 4m \rfloor$ |
| **1/32 (Thirty-Second)** | `ThirtySecond (5)` | 0.125 beat | 8 | 1 | $\lfloor 8m \rfloor$ |
| **1/4 Dotted** | `DottedQuarter (6)` | 1.5 beats | 2 | 3 | $\lfloor 2m / 3 \rfloor$ |
| **1/8 Dotted** | `DottedEighth (7)` | 0.75 beat | 4 | 3 | $\lfloor 4m / 3 \rfloor$ |
| **1/4 Triplet** | `TripletQuarter (8)` | 2/3 beat (~0.667) | 3 | 2 | $\lfloor 3m / 2 \rfloor$ |
| **1/8 Triplet** | `TripletEighth (9)` | 1/3 beat (~0.333) | 3 | 1 | $\lfloor 3m \rfloor$ |

- **Ancoragem:** A grade é ancorada na origem global do transporte, sem reposicionamentos implícitos.
- **Compatibilidade:** Mapeamento direto de/para `hydra_dsp_reverse_division` (0..8) com `GlobalTransportClock::fromReverseDivision()`.

---

## 4. Fórmulas de Compasso (Time Signatures)

Suporte nativo para fórmulas métricas com numerador $N \in [1, 32]$ e denominador $D \in \{2, 4, 8, 16\}$:
- **4/4:** 4 semínimas por compasso ($B_{\text{bar}} = 4.0$).
- **3/4:** 3 semínimas por compasso ($B_{\text{bar}} = 3.0$).
- **6/8:** 6 colcheias = 3 semínimas por compasso ($B_{\text{bar}} = 3.0$).

Cálculo da métrica:
$$\text{barIndex} = \lfloor m / B_{\text{bar}} \rfloor, \quad \text{barPhase} = \frac{m - \text{barIndex} \cdot B_{\text{bar}}}{B_{\text{bar}}} \in [0.0, 1.0)$$

---

## 5. Dinâmica de Transporte: Play, Pause, Stop, Reset e Locate

| Operação | Estado da Posição Musical | Contador Físico (`absoluteSamplePosition`) | Geração (`transportGeneration`) |
|---|---|---|:---:|
| **PLAY** | Avança linearmente com $n$ | Avança linearmente | Inalterada |
| **PAUSE** | Congela no valor exato ($m$ preservado) | Avança (áudio e caudas continuam) | Inalterada |
| **STOP** | Interrompe o avanço | Avança ou cessa conforme o host | Inalterada |
| **LOCATE($m_0$)** | Reposiciona para $m_0$ | Inalterado | Incrementada ($+1$) |
| **RESET** | Reposiciona para $0.0$ | Inalterado | Incrementada ($+1$) |

> [!NOTE]
> **Separação de Domínios:** O reset do transporte não limpa os buffers de fita nem corta caudas de delay. Os módulos DSP consomem a `transportGeneration` para rearmar fases de sincronização sem descontinuidades de áudio.

---

## 6. Mudanças de BPM com Preservação de Fase

Quando o BPM é alterado em tempo real (ex: $120 \to 90$ BPM no instante $t_c$):
1. A posição musical atual $m(t_c)$ é fotografada como nova origem musical ($m_{\text{origin}} = m(t_c)$).
2. O contador de amostras de transporte torna-se a nova origem ($n_{\text{origin}} = n(t_c)$).
3. A nova taxa de avanço $\Delta m_{\text{new}}$ é aplicada a partir de $t_c$.
4. **Sem salto na posição musical, sem perda de fase, sem fronteiras espúrias.**

---

## 7. Integração com Reverse Tape Echo V2

O Reverse V2 é o primeiro módulo consumidor do GTC.
- **Modo Rhythmic (`reverseMode == 0`):** As fronteiras das janelas de leitura invertida são disparadas no início do frame quando `clock.isSubdivisionBoundary(subdiv)` é verdadeiro.
- **Ativação no Meio do Compasso (Opção B):** Ao ativar o Reverse durante a execução, o leitor sincroniza sua fase com `clock.subdivisionPhase(subdiv)`, iniciando imediatamente a leitura invertida sem silêncio nem atrasos e convergindo para a fronteira do beat.
- **Crossfade Suave:** Mantém o crossfade Hermite smoothstep de ~8 ms na sobreposição de janelas, garantindo ausência de cliques.
- **Modos Free (`reverseMode == 1`) e Legacy (`reverseMode == 2`):** Permanecem 100% preservados e independentes do transporte.

---

## 8. Contratos de ABI e C-API 1.5.0

### Compatibilidade de Estruturas
Os layouts binários históricos permanecem intocados:
- **API 1.2:** `116 bytes` (`hydra_dsp_params_v1_2`)
- **API 1.3:** `140 bytes` (`hydra_dsp_params`)
- **API 1.4:** `148 bytes` (`hydra_dsp_params_v1_4`)

### Funções Dedicadas de Transporte (API 1.5.0)
```c
int hydra_dsp_transport_play(hydra_dsp_handle* handle);
int hydra_dsp_transport_pause(hydra_dsp_handle* handle);
int hydra_dsp_transport_stop(hydra_dsp_handle* handle);
int hydra_dsp_transport_reset(hydra_dsp_handle* handle);
int hydra_dsp_transport_locate(hydra_dsp_handle* handle, double musical_pos, uint64_t sample_pos);
int hydra_dsp_transport_set_bpm(hydra_dsp_handle* handle, float bpm);
int hydra_dsp_transport_set_time_signature(hydra_dsp_handle* handle, uint32_t numerator, uint32_t denominator);
int hydra_dsp_transport_get_state(const hydra_dsp_handle* handle, hydra_dsp_transport_state* out_state);
```

---

## 9. Telemetria e Integração de Hosts

### 9.1 ESP32-S3 CLI
Comandos adicionados ao firmware:
- `tempo <bpm>`: Atualiza o BPM global e sincroniza todos os módulos.
- `transport play`: Inicia avanço musical.
- `transport pause`: Congela avanço musical.
- `transport stop`: Interrompe avanço musical.
- `transport reset`: Reposiciona origem para beat 0.0.
- `transport status`: Exibe telemetria completa (BPM, estado, beat, compasso, posição musical, sample rate, amostras absolutas).

### 9.2 Web Audio & AudioWorklet
- O worklet consulta o estado de transporte a cada 16 blocos (~42 ms) via buffer pré-alocado na memória WASM e emite eventos `transportTelemetry`.
- Painel compacto com indicadores visuais de compasso (`● ○ ○ ○`), posição numérica em tempo real e badge `SYNC: LOCKED`.

---

## 10. Pacote de Validação Auditiva (Hydra Listening Lab)

Pacote gerado em `artifacts/listening-lab/gtc-reverse-sync/`:
- **01_reverse_beat_zero:** Reverse acionado no beat zero.
- **02_reverse_mid_bar:** Reverse acionado no meio do compasso.
- **03_reverse_bpm_change:** Transição 120 $\to$ 90 BPM com preservação de fase.
- **04_reverse_subdivision_change:** Troca dinâmica de 1/4 para 1/8.
- **05_reverse_time_sig_4_4:** Métrica de 4/4.
- **06_reverse_time_sig_3_4:** Métrica de 3/4.
- **07_reverse_time_sig_6_8:** Métrica de 6/8.
- **08_two_consumers_sync:** Dois consumidores independentes sincronizados.
- **09_play_pause_resume:** Pausa e retomada sem descontinuidades de áudio.
- **10_reset_locate_history:** Relocate preservando o histórico de áudio.

Todos os renders acompanham marcadores temporais, hashes SHA-256 e o arquivo `manifest.json`.

---

## 11. Validação Final de Release, Build Xtensa e Robustez Numérica (v1.5.1)

### 11.1 Causa e Resolução do Build Xtensa ESP32-S3
- **Diagnóstico:** O compilador Xtensa GCC 14.2.0 (`toolchain-xtensa-esp-elf`) define `int32_t` como `long int`. Em expressões como `std::max(0, safeMax)` e `std::clamp(targetAge, 0, safeMax)`, onde `0` é literal `int` e `safeMax` é `int32_t`, a dedução de tipo falhava com `no matching function for call to max(int, const int32_t&)`.
- **Correção:** Tipagem explícita com templates fixos em `dsp/core/reverse_windows.hpp`:
  - `std::min<int32_t>(...)`
  - `std::max<int32_t>(0, safeMax)`
  - `std::clamp<int32_t>(targetAge, 0, safeAge)`
- **Verificação:** Build de firmware com PlatformIO concluído com sucesso total (`esp32-s3-devkitc-1 SUCCESS`), gerando a imagem flashable de 600.959 bytes.

### 11.2 Política de Entradas Inválidas e Hardening (NaN / Inf / Zero / Negativos)
- **Política Estrita de Rejeição:** Valores não finitos (`std::isnan`, `std::isinf`), negativos ou fora de domínio em chamadas de configuração do relógio são sumariamente rejeitados, mantendo 100% íntegro o estado anterior (BPM anterior, posição musical anterior, geração de transporte e contadores inalterados).
- **Código de Retorno C-API:** As funções `hydra_dsp_transport_set_bpm`, `hydra_dsp_transport_locate` e `hydra_dsp_transport_set_time_signature` retornam `-1` imediatamente em caso de argumentos inválidos/não-finitos.
- **Parametrização DSP:** Chamadas a `hydra_dsp_set_parameter` para `HYDRA_DSP_PARAM_BPM` com valores não finitos ou $\le 0.0$ retornam `-1`.

### 11.3 Política de Histórico Limitado e Diagnóstico
- **Clamping Seguro:** Quando a subdivisão rítmica solicitada em determinado BPM excede a capacidade física do buffer (`requested > maxHop_`), a janela é limitada com segurança ao `maxHop_` disponível, sinalizando `isClamped = 1` e ajustando `effectiveMs`.
- **API de Diagnóstico (`hydra_dsp_get_reverse_diagnostics`):** Exposição transparente ao host dos valores:
  1. `requested_ms`: Duração nominal solicitada pela subdivisão musical.
  2. `effective_ms`: Duração real aplicada ao leitor reverso.
  3. `is_clamped`: Flag booleana indicando se houve limitação por falta de histórico.
  4. `max_history_ms`: Capacidade máxima total do buffer de histórico alocado.

### 11.4 Layout de Memória e Telemetria Web
- A estrutura `hydra_dsp_transport_state` possui tamanho exato de **72 bytes**, com alinhamento de 64 bits para campos `double musical_position`, `uint64_t absolute_sample_position`, `uint64_t transport_sample_position`, `int64_t beat_index` e `int64_t bar_index`.
- O worklet Web (`host/web/hydra-processor.js`) consome o buffer via `DataView` com decodificação little-endian explícita, prevenindo desalinhamentos ou exceções de offset na memória WASM.

### 11.5 Auditoria de Ponto Flutuante e Desempenho Físico (ESP32-S3 N16R8)
- **Impacto de Double no Xtensa LX7:** As operações em `double` do GTC ocorrem via incrementos pré-calculados e equações fechadas. No loop de áudio por amostra, apenas incrementos inteiros (`tick()`) e aritméticas básicas são avaliados. O microbenchmarking físico revelou que `floorf` consome apenas ~71 ciclos e o estágio do DSP ocupa menos de 55% do orçamento por bloco.
- **Ensaio Físico Sustentado (5 minutos contínuos na placa real ESP32-S3 @ 240 MHz):**
  - **Amostras / Blocos:** 103.360 blocos de 128 frames estéreo a 44,1 kHz (300.003,9 ms de áudio ininterrupto).
  - **Orçamento por bloco:** **2.902,49 µs**.
  - **Cadeia Completa sob Carga Máxima** (TapeCore + Delay + Reverse V2 Rhythmic + Accidental Warp + Spring Reverb + GTC ativo):
    - **Média DSP:** `1.589,41 µs` (**54,76%** do orçamento).
    - **P50:** `1.594 µs`.
    - **P95:** `1.665 µs`.
    - **P99:** `1.677 µs` (**57,78%** do orçamento).
    - **Pico Máximo:** `1.887 µs` (margem livre de > 1.015 µs para o teto de overrun).
    - **Overruns de DSP:** **0** (`0,00%`).
    - **Falhas de I2S (DMA Delta):** **0**.
    - **Estabilidade de Memória:** `HeapStart = 124.160 B`, `HeapEnd = 124.160 B`, `HeapMin = 124.160 B` (delta de 0 bytes; zero fragmentação, zero alocação dinâmica em tempo de execução).
    - **PSRAM Livre:** `1.667.736 B` constante.

### 11.6 Divergência Histórica Native/WASM (Issue #48) e Aceite Excepcional
- A divergência numérica entre Native e WASM permanece em `max_abs = 3.6681e-5` e `RMSE = 1.585565e-5` (limites do teste: $\le 10^{-5}$ e $\le 10^{-6}$), herdada de iterações anteriores de interpolação e funções transcendentais (Issue #48).
- Os thresholds oficiais não foram relaxados e os testes continuam ativos.
- **Aceite Excepcional para Release da Fase 3:** O responsável pelo projeto formalizou a aceitação excepcional desta divergência pré-existente exclusivamente para a conclusão da PR #50, visto que nenhuma nova regressão numérica foi introduzida pelo GTC. A issue #48 permanece formalmente aberta para resolução técnica posterior.

### 11.7 Validação Estrita de Fórmulas de Compasso
- **Contrato de Validação Compartilhado (`isValidTimeSignature`):**
  - **Numeradores aceitos:** Inteiros no intervalo fechado $[1, 32]$.
  - **Denominadores aceitos:** Apenas potências musicais permitidas: $\{2, 4, 8, 16\}$.
  - **Casos estritamente rejeitados:** $0/4$, $33/4$, $4/0$, $4/3$, $4/32$, denominadores ímpares ou fora da lista suportada.
- **Invariância de Estado:** Em qualquer chamada inválida, o estado interno anterior é preservado integralmente: numerador, denominador, `beatsPerBar`, posição musical, contadores e geração permanecem idênticos.
- **Retorno C-API:** `hydra_dsp_transport_set_time_signature` retorna `0` em caso de sucesso e `-1` em caso de rejeição ou ponteiro nulo.

### 11.8 Visibilidade de Histórico Limitado e Precedência de DSP
- **Visibilidade na Web UI:**
  - Quando o Reverse Rítmico é limitado pelo buffer de histórico (`is_clamped == 1`), a interface exibe um banner explícito: `REVERSE TIME LIMITED — requested XXX ms / effective YYY ms`.
  - O indicador de sincronização (`transportSyncBadge`) transiciona de `SYNC: LOCKED` (`success`) para `SYNC: LIMITED` (`warning`).
- **Visibilidade no ESP32-S3 CLI:**
  - O comando `transport status` imprime: `  Reverse Sync        : LIMITED (Effective: %.1f ms)` quando o leitor reverso opera sob limitação de capacidade.
- **Precedência DSP (Accidental Warp vs Reverse V2):**
  - No `TapeCore`, a condição de ativação do leitor reverso é estritamente: `wantsReverse = delayActive && !warpActive && reverse && !legacyReverse`.
  - O Accidental Warp possui precedência operacional sobre o Reverse V2: caso ambos os efeitos estejam ligados simultaneamente, o processamento de áudio do Warp assume o controle e o Reverse V2 permanece em repouso sem gerar conflitos ou artefatos.
