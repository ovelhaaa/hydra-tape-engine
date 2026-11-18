# HYDRA TAPE ENGINE 2.0 — MIDI CLOCK EXTERNAL SYNC V1 SPECIFICATION
## Tempo, Phase & Transport Synchronization (24 PPQN) with Digital PLL & Lock-Free SPSC Handoff

---

## 1. Visão Geral e Princípios Arquiteturais

O **MIDI Clock External Sync V1** implementa a sincronização do **Hydra Tape Engine 2.0** com mensagens MIDI de tempo real (MIDI 1.0 Real-Time Protocol), permitindo que efeitos rítmicos (delay com divisão métrica, cabeças de reprodução multitap e janelas reversas) acompanhem perfeitamente drum machines, sintetizadores, sequenciadores de hardware ou DAWs.

### 1.1 Princípio Cardeal: Fonte Única Autoritativa de Posição Musical

> [!IMPORTANT]
> **O Global Transport Clock (GTC) continua sendo a única fonte autoritativa de tempo, posição e fase musical para todo o pipeline DSP.**
> O receptor MIDI Clock externo atua como uma referência de sincronização para o GTC, e **NÃO** como um segundo transporte concorrente.

O componente `MidiClockSync`:
- **NÃO** processa amostras de áudio.
- **NÃO** mantém um segundo contador de frames DSP ou fase musical paralela.
- **NÃO** força chamadas a `locate()` a cada pulso.
- **NÃO** limpa buffers de delay, não reinicializa cabeças e não corta caudas de reverb/feedback.
- **NÃO** realiza alocações dinâmicas de memória (`new`, `malloc`, `std::vector`) nem operações de I/O em tempo de execução.
- **NÃO** bloqueia ou introduz mutexes no caminho crítico da tarefa de áudio (`audioTask`).

---

## 2. Modos de Fonte de Andamento e Modelo de Arbitragem

O sistema introduz uma seleção explícita da autoridade de andamento musical com desacoplamento rigoroso entre a origem do comando e o valor efetivamente aplicado aos módulos DSP:

```text
manualBpm          -> Último andamento definido manualmente pelo usuário
tapEstimatedBpm    -> Último andamento válido calculado pelo estimador de toques
midiEstimatedBpm   -> Andamento estimado recuperado do fluxo de pulsos MIDI
effectiveBpm       -> Valor ativamente aplicado ao GTC e aos consumidores DSP
tempoSource        -> Autoridade atualmente selecionada (INTERNAL | TAP | MIDI EXTERNAL)
```

| Modo | Valor Enum C-API | Descrição | Comportamento no GTC |
| :--- | :---: | :--- | :--- |
| **INTERNAL** | `0` (`HYDRA_DSP_TEMPO_SOURCE_INTERNAL`) | Controle manual pelo usuário via parâmetro BPM ou CLI. | GTC avança no BPM configurado manualmente (`manualBpm`). Edições manuais atualizam `manualBpm` e `effectiveBpm`. |
| **TAP** | `1` (`HYDRA_DSP_TEMPO_SOURCE_TAP`) | Andamento derivado do estimador Tap Tempo V1. | GTC segue o BPM validado e travado pelo estimador de toques (`tapEstimatedBpm`). Ações manuais não alteram a fonte implicitamente. |
| **MIDI EXTERNAL**| `2` (`HYDRA_DSP_TEMPO_SOURCE_MIDI_EXTERNAL`) | Andamento e transporte derivados do fluxo MIDI Clock externo. | GTC é governado pela taxa e fase estimadas pelo PLL digital. Edições manuais preservam `manualBpm` sem assumir autoridade. |

### 2.1 Arbitragem Estrita de Fonte (Correção Fase 5.1)

Anteriormente, atualizações genéricas em `TapeParams::bpm` ou eventos de toque podiam sobrescrever inadvertidamente o `manualBpm` ou redefinir a fonte de tempo para `Internal`.
A regra de arbitragem foi endurecida:
- `TapeParams::bpm` só atualiza `manualBpm` se o valor sofrer modificação real, e só reflete em `effectiveBpm` quando `tempoSource == Internal`.
- O estimador de Tap Tempo pode monitorar toques durante `MidiExternal` atualizando `tapEstimatedBpm` sem assumir autoridade do transporte musical.
- Ao retornar para `Internal`, o `manualBpm` previamente estabelecido é restaurado de forma limpa e transparente.
- Nenhuma função genérica de atualização altera `tempoSource` implicitamente: a transição exige chamada explícita (`setTempoSource`).

### 2.2 Matriz de Transições e Invariantes Sonoras

Todas as 6 transições de modo (`Internal ↔ Tap`, `Internal ↔ MidiExternal`, `Tap ↔ MidiExternal`) operam sem estalos (*clickless*):
- **Preservação de Memória**: Ao alternar para `MidiExternal`, o andamento manual anterior é preservado. Se o usuário alterar parâmetros manuais enquanto em `MidiExternal`, o valor manual é armazenado em `manualBpm` sem sobrescrever o clock externo; ao retornar para `Internal`, o andamento manual é restaurado imediatamente.
- **Continuidade de Buffers**: Nenhuma troca de modo limpa buffers de retardo, reinicializa a fita ou corta caudas de realimentação.
- **Desacoplamento de Subdivisões**: A seleção da fonte de andamento é completamente independente das subdivisões métricas de cada efeito (ex: colcheia pontuada, semínima, semínima reversa).
- **Proteção do Reverse V2 (Deadband)**: Microflutuações contínuas de 0.001 BPM originadas pelo tracking do PLL poderiam forçar a reconstrução das janelas do Reverse V2. Um deadband de 0.1 BPM protege a estabilidade do Reverse V2 enquanto o GTC acompanha a fase musical de forma contínua.

---

## 3. Protocolo MIDI Real-Time (24 PPQN)

O receptor processa mensagens MIDI de tempo real de 1 byte de acordo com a norma MIDI 1.0:

| Mensagem | Byte Hex | Frequência Nominal | Comportamento no Hydra |
| :--- | :---: | :---: | :--- |
| **Timing Clock** | `0xF8` | 24 pulsos por semínima (24 PPQN) | Alimenta o estimador de intervalo e o detector de erro de fase do PLL. |
| **Start** | `0xFA` | Evento único | Inicia playback do transporte, reseta a referência externa de pulsos (`pulseCount = 0`) e posiciona o GTC em `0.0`. Não limpa buffers. |
| **Continue** | `0xFB` | Evento único | Retoma o playback a partir da posição musical preservada (sem retornar ao beat zero) e realinha a contagem de pulsos à posição atual do GTC. |
| **Stop** | `0xFC` | Evento único | Pausa o avanço do transporte musical. Preserva a posição atual, buffers e caudas de áudio. |

### 3.1 Parser Mínimo Real-Time e Preservação de Running Status

Mensagens de tempo real MIDI podem aparecer intercaladas no meio de mensagens de canal com múltiplos bytes (ex: entre os bytes de dados de um `Note On` ou `Control Change`).
O adaptador `MidiRealtimeParser` implementa:
- Filtragem imediata dos bytes de tempo real (`0xF8`, `0xFA`, `0xFB`, `0xFC`) sem alterar o byte de `runningStatus`.
- Rejeição de bytes fora da especificação de tempo real no receptor de sincronização.
- Entrega tipada e em tempo constante $O(1)$ para a fila SPSC de áudio.

---

## 4. Estimativa de Andamento e PLL Digital de Fase

O MIDI Timing Clock transmite 24 pulsos por semínima. Para um intervalo medido de $\Delta t$ segundos entre pulsos consecutivos:
$$\text{pulseIntervalSeconds} = \frac{\Delta \text{frames}}{f_s}$$
$$\text{BPM} = \frac{60.0}{24.0 \times \text{pulseIntervalSeconds}} = \frac{2.5 \times f_s}{\Delta \text{frames}}$$

### 4.1 Tabela de Intervalos Nominais

| BPM | Pulsos / Segundo | Intervalo @ 44.1 kHz | Intervalo @ 48.0 kHz | Período (ms) |
| :---: | :---: | :---: | :---: | :---: |
| **60** | 24 | 1837.5 amostras | 2000.0 amostras | 41.667 ms |
| **90** | 36 | 1225.0 amostras | 1333.3 amostras | 27.778 ms |
| **120** | 48 | 918.75 amostras | 1000.0 amostras | 20.833 ms |
| **140** | 56 | 787.5 amostras | 857.1 amostras | 17.857 ms |
| **180** | 72 | 612.5 amostras | 666.7 amostras | 13.889 ms |
| **240** | 96 | 459.375 amostras | 500.0 amostras | 10.417 ms |
| **300** | 120 | 367.5 amostras | 400.0 amostras | 8.333 ms |

### 4.2 Filtragem de Jitter e Rejeição de Pulsos Perdidos

- **Buffer Circular de Histórico**: Mantém os últimos 16 intervalos válidos.
- **Rejeição de Outliers e Jitter**: Calcula a média móvel e o desvio padrão (jitter estimado em ms). Variações de jitter até $\pm 5$ ms são filtradas pela inércia da janela móvel.
- **Detecção de Pulso Perdido**: Se um intervalo medido atingir aproximadamente $2\times$ ou $3\times$ o intervalo médio esperado ($\text{ratio} \in [1.7, 2.3]$ ou $[2.7, 3.3]$), o estimador reconhece o pulso faltante, incrementa o contador `missed_pulses` e divide o $\Delta$ adequadamente, evitando saltos bruscos no cálculo de BPM.

### 4.3 PLL Digital Proporcional e Métricas de Erro de Fase (Fase 5.1 Hardening)

Diferente de um simples medidor de BPM, o componente `MidiClockSync` monitora continuamente o erro de fase entre a posição musical esperada do dispositivo mestre e a posição musical autoritativa do GTC. Para evitar ambiguidades onde um deslocamento inteiro de compasso ou batida (ex: 1.0 ou 2.0 batidas) produzia um erro embrulhado nulo (`wrapped == 0.0`), a Fase 5.1 introduz a separação estrita de duas métricas:

$$\text{expectedMusicalPos} = \frac{\text{pulseCount} + 1}{24.0}$$
$$\text{absolutePhaseErrorBeats} = \text{expectedMusicalPos} - \text{currentGtcMusicalPos}$$
$$\text{wrappedPhaseErrorBeats} = \text{absolutePhaseErrorBeats} - \text{round}(\text{absolutePhaseErrorBeats}) \quad \in [-0.5, +0.5]$$

- **`absolutePhaseErrorBeats`**: Reflete a distância real absoluta entre a referência externa e a posição do motor. Utilizado para validação de alinhamento real e proteção contra falsos travamentos em batidas adjacentes.
- **`wrappedPhaseErrorBeats`**: Utilizado exclusivamente para a modulação proporcional fina do PLL.

#### Rejeição Antecipada de Pulsos Duplicados ou Fora de Ordem
Pulsos com `timestampFrames <= lastPulseFrame_` são rejeitados **antes** de qualquer incremento na contagem de pulsos (`pulseCount_`), impedindo que eventos reordenados ou duplicados avancem a referência musical do PLL.

#### Condição Hardened para Estado LOCKED
Para transicionar de `ACQUIRING` para `LOCKED`, o PLL exige cumulativamente:
1. 8 pulsos consecutivos com intervalo estável.
2. $|\text{wrappedPhaseErrorBeats}| \le 0.015$ batidas (erro de fase fino nulo).
3. $|\text{absolutePhaseErrorBeats}| \le 0.05$ batidas (garantia de alinhamento na mesma batida musical, rejeitando deslocamentos inteiros).

Uma correção proporcional com limite dinâmico (*bounded slew*) é aplicada ao andamento do GTC:
$$\Delta \text{BPM}_{\text{corr}} = \text{clamp}\left(-K_p \times \text{wrappedPhaseErrorBeats}, \; - \text{maxCorrection}, \; + \text{maxCorrection}\right)$$
onde $K_p = 15.0$ e $\text{maxCorrection} = \min(3.0, \text{estimatedBpm} \times 0.03)$ (máximo de 3% de variação ou 3.0 BPM por bloco).
Essa modulação suave garante que o erro de fase seja continuamente drenado para zero **sem provocar artefatos audíveis de wow/flutter nem saltos perceptíveis no pitch da fita**.

---

## 5. Máquina de Estados de Sincronização e Timeout

```
  +--------------+   Pulsos recebidos   +-------------+   8 pulsos estáveis & fase < 0.01   +------------+
  | DISCONNECTED | -------------------> |  ACQUIRING  | ----------------------------------> |   LOCKED   |
  +--------------+                      +-------------+                                     +------------+
         ^                                     |                                                   |
         | > 1.0s sem pulsos                   |                                                   | > 150ms sem pulsos
         |                                     |                                                   v
  +--------------+                             |                                            +------------+
  |     LOST     | <---------------------------+------------------------------------------- |  HOLDOVER  |
  +--------------+                     > 1.0s sem pulsos                                    +------------+
```

1. **DISCONNECTED (`0`)**: Nenhuma mensagem MIDI Clock recebida desde a inicialização.
2. **ACQUIRING (`1`)**: Pulsos detectados, porém o histórico ainda não acumulou estabilidade estatística suficiente (requer 8 pulsos consecutivos com desvio $< 2$ BPM e erro de fase $< 0.01$ batidas).
3. **LOCKED (`2`)**: Sincronização completa de andamento e fase musical.
4. **HOLDOVER (`3`)**: Ausência transitória de pulsos (entre 150 ms e 1,0 s). O GTC mantém o playback contínuo na última velocidade conhecida sem travar nem saltar.
5. **LOST (`4`)**: Ausência prolongada de pulsos (> 1,0 s). O estado sinaliza perda definitiva do clock externo; ao reconectar, o sistema reentra em `ACQUIRING` sem travar o áudio.

---

## 6. Concorrência Real-Time Safe no ESP32-S3 e Recuperação de Overflow

Para respeitar a garantia matemática do padrão Lock-Free **Single-Producer Single-Consumer (SPSC)**, o subsistema de entrada MIDI conta com uma fila dedicada e isolada:

- **`commandQueue_` (Capacidade: 32)**: Produtor exclusivo = Tarefa CLI/Configuração (Core 0). Comandos de transporte, andamento manual e fonte de tempo.
- **`midiQueue_` (Capacidade: 64)**: Produtor exclusivo = Tarefa MIDI / ISR de porta serial / USB (Core 0 ou driver serial). Eventos `0xF8`, `0xFA`, `0xFB`, `0xFC` com timestamp em microsegundos ou amostras.
- **Consumidor Único**: A tarefa de áudio (`audioTask`, Core 1) drena ambas as filas no início de cada bloco de 128 frames (`drainCommandQueue()`).
- **Detecção e Recuperação de Overflow**: Se a fila de 64 eventos transbordar devido a atrasos transitórios do host, o adaptador incrementa atomicamente o contador `midiOverflowCount_` e ativa a flag `midiQueueOverflowed_`. No início do próximo bloco de áudio, a tarefa de áudio purga os pulsos residuais corrompidos e aciona `hydra_dsp_midi_clock_reset(handle_)`, forçando uma reaquisição limpa em vez de sustentar um estado travado com integridade comprometida.
- **Triple Buffer Lock-Free (`StatePublisher<hydra_dsp_midi_clock_state>`)**: Core 1 publica snapshots atômicos do estado do clock sem contenda nem mutexes, lidos de forma segura pela UI ou CLI.

---

## 7. C-API Versão 1.7.0 e Compatibilidade ABI

A versão da C-API foi elevada para **1.7.0** (`0x00010700`):

### Novas Funções Exportadas:
```c
int hydra_dsp_midi_clock_event(hydra_dsp_handle* handle, int midi_status, uint64_t timestamp_frames);
int hydra_dsp_midi_clock_get_state(hydra_dsp_handle* handle, hydra_dsp_midi_clock_state* out_state);
int hydra_dsp_midi_clock_reset(hydra_dsp_handle* handle);
int hydra_dsp_set_tempo_source(hydra_dsp_handle* handle, uint32_t source);
int hydra_dsp_get_tempo_source(hydra_dsp_handle* handle, uint32_t* out_source);
```

### Estrutura de Estado (48 bytes):
```c
typedef struct hydra_dsp_midi_clock_state {
  uint32_t tempo_source;       // 0: Internal, 1: Tap, 2: MidiExternal
  int sync_status;             // 0: Disconnected, 1: Acquiring, 2: Locked, 3: Holdover, 4: Lost
  float estimated_bpm;         // BPM instantâneo/suavizado do fluxo externo
  float applied_bpm;           // BPM efetivo aplicado ao GTC (com correção do PLL)
  float phase_error_beats;     // Erro de fase normalizado [-0.5, +0.5]
  float estimated_jitter_ms;   // Jitter estatístico medido em ms
  uint64_t pulse_count;        // Contagem cumulativa de pulsos 24 PPQN
  uint64_t last_pulse_frame;   // Timestamp em amostras do último pulso recebido
  int is_transport_running;    // Flag indicando transporte ativo via Start/Continue
  uint32_t missed_pulses;      // Contador de pulsos faltantes identificados e compensados
} hydra_dsp_midi_clock_state;
```

**Verificação de Invariantes ABI**:
- `sizeof(hydra_dsp_params_v1_2) == 116` bytes (Preservado)
- `sizeof(hydra_dsp_params_v1_3) == 140` bytes (Preservado)
- `sizeof(hydra_dsp_params_v1_4) == 148` bytes (Preservado)
- `sizeof(hydra_dsp_midi_clock_state) == 48` bytes (Validado por `static_assert`)

---

## 8. Comandos CLI no ESP32-S3

A interface serial do firmware ESP32-S3 disponibiliza os seguintes comandos:

| Comando | Descrição |
| :--- | :--- |
| `clock source internal` | Seleciona andamento manual interno. |
| `clock source tap` | Seleciona estimador Tap Tempo V1. |
| `clock source midi` | Seleciona sincronização MIDI Clock externa (24 PPQN). |
| `clock status` | Exibe fonte ativa, status de lock, BPM externo, BPM aplicado, jitter, erro de fase e pulsos. |
| `clock reset` | Reseta exclusivamente o estado do estimador de MIDI Clock. |
| `clock event <hex>` | Injeta byte MIDI real-time para depuração (ex: `clock event F8`, `clock event FA`). |

---

## 9. Integração Web (Web MIDI API & AudioWorklet)

### 9.1 Preservação de Timestamps e Conversão de Latência (`webMidiTime.js`)
Anteriormente, o host web descartava o timestamp do evento MIDI (`event.timeStamp`) e enviava `timestampFrames = 0`, submetendo a estimativa ao jitter de agendamento do `MessagePort` e do browser main thread.
A Fase 5.1 implementa o módulo formal de tempo (`host/web/webMidiTime.js`):
1. **Preservação Monotônica de Alta Precisão**: O `event.timeStamp` em milissegundos é preservado diretamente do driver da Web MIDI API.
2. **Transformação para Domínio de Amostras de Áudio**:
   $$\text{latencySeconds} = \frac{\text{nowMs} - \text{event.timeStamp}}{1000.0}$$
   $$\text{latencyFrames} = \text{round}(\text{latencySeconds} \times f_s)$$
   $$\text{audioFrame} = \max(0, \text{currentAudioFrame} - \text{latencyFrames})$$
3. **Processamento em Lote Sem Colapso**: Quando múltiplos eventos chegam em lote, `processBatchMidiEvents` preserva as diferenças relativas de tempo entre eles, garantindo que `frames[k] >= frames[k-1] + 1` sem colapsar múltiplos pulsos no mesmo frame de áudio.
4. **Política de Fallback e Sanitização**: Timestamps não-finitos, no futuro excessivo (> 50 ms) ou estéreis (> 3,0 s) sofrem fallback determinístico para `currentAudioFrame` mantendo a monotonicidade.

### 9.2 Controles de UI e Telemetria
1. **Seletor de Porta Web MIDI**: Suporte nativo a `navigator.requestMIDIAccess({ sysex: false })`. Enumera portas de entrada MIDI USB e roteia mensagens `0xF8`, `0xFA`, `0xFB`, `0xFC` diretamente ao AudioWorklet.
2. **Seletor de Modo na Interface**: Botões `INTERNAL`, `TAP` e `MIDI EXT` atualizam o modo e refletem o estado ativo.
3. **Badge de Diagnóstico**:
   - `DISCONNECTED`: Sem porta conectada ou sem pulsos.
   - `ACQUIRING`: Fluxo de pulsos detectado; travamento em andamento.
   - `SYNC: LOCKED`: Sincronização estável de andamento e fase.
   - `HOLDOVER`: Interrupção temporária de pulsos; andamento sustentado.
   - `SIGNAL LOST`: Ausência prolongada de sinal externo.
4. **Telemetria de Fase e Jitter**: Exibe BPM externo medido, desvio de fase e jitter estimado em milissegundos.

---

## 10. Validação no Hydra Listening Lab

O script `scripts/render_midi_clock_review.py` gerou o pacote completo de validação auditiva e temporal em `artifacts/listening-lab/midi-clock-external-v1/`:

| Cenário | Descrição | Duração | Verificação Sonora e Temporal |
| :--- | :--- | :---: | :--- |
| `01_fixed_internal_120_bpm` | Baseline interno a 120 BPM | 4.0s | Referência de áudio contínuo sem modulação externa. |
| `02_midi_clock_lock_120_bpm` | Travamento em 120 BPM via 24 PPQN | 4.0s | Travamento em $< 0.4$s, erro de fase final $< 0.001$ batidas. |
| `03_midi_clock_lock_90_bpm` | Travamento em andamento lento (90 BPM) | 4.5s | Resolução precisa de intervalos longos sem falsos timeouts. |
| `04_midi_clock_lock_140_bpm` | Travamento em andamento rápido (140 BPM) | 4.0s | Alta cadência de pulsos acompanhada com estabilidade. |
| `05_tempo_ramp_120_to_140` | Aceleração gradual (120 $\to$ 140 BPM) | 5.0s | Acompanhamento contínuo de aceleração sem quebra de fase. |
| `06_tempo_slowdown_140_to_100` | Desaceleração gradual (140 $\to$ 100 BPM) | 5.0s | Desaceleração contínua sem estalos ou instabilidade. |
| `07_sudden_tempo_jump_120_to_90` | Degrau instantâneo de tempo | 5.0s | Slew rate limitado (máx 3%), transição suave sem estalos. |
| `08_jitter_resistance_pm5ms` | Clock com $\pm 5$ ms de ruído temporal | 4.5s | Filtro do PLL rejeita o jitter; áudio estável e musical. |
| `09_clock_holdover_and_recovery` | Perda temporária de sinal de clock | 5.0s | Transição para Holdover preserva tempo; reconexão imediata. |
| `10_start_stop_continue_transport` | Sequência Start $\to$ Stop $\to$ Continue | 5.0s | Posicionamento exato, caudas de delay e feedback preservadas. |
| `11_reverse_eighth_sync_midi` | Reverse 1/8 quantizado a 130 BPM externo | 4.5s | Janelas reversas adaptadas precisamente à cadência MIDI. |
| `12_source_transition_internal_to_midi`| Transição `Internal` $\to$ `MidiExternal` | 5.0s | Transição ao vivo sem estalos, sem corte de áudio e sem glitch. |

---

## 11. Cobertura de Testes Automatizados (Fase 5.2 Estabilização Final)

A suíte automatizada cobre integralmente os requisitos de autoridade de fonte na C-API, recuperação determinística de fase, tolerância a falhas e integração ponta a ponta:

1. **Testes Nativos C++ (`tests/core/midi_clock_test.cpp`) — 23/23 Aprovados**:
   - `testMidiRealtimeParser`: Parser e intercalação sem corrupção de `runningStatus`.
   - `testBpmAcquisition`: Aquisição precisa de 60 a 300 BPM em 44.1 kHz e 48 kHz.
   - `testPhaseLockAndAlignment`: Alinhamento de fase após Start com deriva zero.
   - `testStartStopContinue`: Preservação de posição em Stop e retomada transparente em Continue.
   - `testJitterFiltering`: Imunidade a jitter gaussiano de $\pm 0.5$ ms a $\pm 5$ ms.
   - `testTempoRamp`: Rastreamento contínuo de aceleração (120 $\to$ 140 BPM).
   - `testSuddenTempoChange`: Convergência rápida em degraus abruptos (120 $\to$ 90 $\to$ 140 $\to$ 180 BPM).
   - `testMissingPulses`: Detecção e interpolação de pulsos perdidos.
   - `testClockLossAndHoldover`: Transição progressiva Locked $\to$ Holdover $\to$ Lost.
   - `testReconnection`: Reaquisição após restabelecimento do sinal.
   - `testSourceSwitching`: Todas as 6 transições de fonte clickless e com buffers intactos.
   - `testReverseSynchronization`: Janelas rítmicas reversas sincronizadas com clock externo.
   - `testMultiHeadSynchronization`: Cabeças de delay musicais proporcionais ao clock externo.
   - `testSpscMidiConcurrency`: Fila SPSC com 64 slots, descarte não-bloqueante e thread safety.
   - `testAbiCompatibility`: Validação estrita da ABI 1.7.0 e offsets de structs legadas.
   - `testLongRunningStability`: 100.000 pulsos com deriva acumulada nula ($< 0.01$).
   - `testSourceArbitrationExplicit`: Testes formais da Seção 3.4 (Testes A, B, C, D e E).
   - `testHardenedPhaseLockAndRejection`: Rejeição de ambiguidades de batida inteira (1 e 2 beats) e descarte antecipado de pulsos fora de ordem/duplicados.
   - `testStartStopContinueEdgeCases`: Continue sem Start, Starts repetidos e pulsos durante parada.
   - `testSpscQueueOverflowRecovery`: Detecção atômica de transbordo, contador de overflows e reset automático de aquisição.
   - `testCApiSourceAuthority`: Seção 3.3 Bloqueador 1 — Garantia estrita de que `hydra_dsp_transport_set_bpm()` e parâmetros da API C respeitam a autoridade da fonte selecionada sem contornar o GTC.
   - `testDeterministicPhaseRecovery`: Seção 5 Bloqueador 2 — Validação determinística de recuperação para todos os 7 offsets ($1/24, 1/8, 1/4, 1/2, 1, 2, 4$ beats) e Casos A..G (Start, Continue, GAP/Holdover sem perda de áudio, troca de fonte durante playback).
   - `testReverseStabilityUnderJitterAndTempoChanges`: Seção 8 — Validação quantitativa do deadband de 0.1 BPM sob jitter ($\pm 0.5$ ms) e saltos de andamento ($120 \to 140$ e $140 \to 90$ BPM) confirmando zero falhas ou resets.

2. **Testes Web (`host/web/tests/webMidiClock.test.mjs` & Suíte Web) — 10/10 e 35/35 Aprovados**:
   - Conversão de latência preservando timestamps originais a 44.1 kHz e 48 kHz.
   - Rejeição de jitter do `MessagePort` sustentando BPM de 120 sem distorção.
   - Política determinística de fallback para timestamps inválidos, futuros ou estéreis.
   - Processamento de lotes mantendo diferenças relativas e monotonicidade estrita.
   - Rejeição de eventos duplicados ($< 1$ ms) e fora de ordem sem mutação arbitrária de timestamps.
   - Métricas de erro temporal: erro máximo $\le 1$ frame e RMSE $< 0.1$ frame.
   - Teste integrado ponta a ponta: Web MIDI $\to$ `processBatchMidiEvents` $\to$ AudioWorklet (`hydra-processor.js` com `BigInt`) $\to$ WASM DSP atingindo status LOCKED e BPM estável.

3. **Compatibilidade ESP32-S3 Firmware**:
   - Compilação limpa no PlatformIO (RAM: 7.9%, Flash: 3.7%, zero warnings).

---

## 12. Critérios do Release Gate e Exceção Formal Issue #48

1. **Aprovação em 100% dos Testes**: CTest 15/15 testes passando, C++ MIDI Clock 23/23 subtestes passando, Web Host 35/35 testes passando.
2. **Estabilidade de Áudio**: Ausência de estalos (*clicks*), cortes de áudio ou resets em buffers durante travamento, recuperação de clock e transições entre Internal, Tap e MIDI External.
3. **Exceção da Issue #48 (Divergência Histórica Native/WASM)**:
   - A comparação entre `hydra_render_trace_native.exe` e `hydra_render_trace.js` resultou em:
     $$\text{max\_abs} = 5.968000 \times 10^{-6} \quad (\le 3.6681 \times 10^{-5})$$
     $$\text{rmse} = 2.344106 \times 10^{-6} \quad (\le 1.585565 \times 10^{-5})$$
   - Essa discrepância sutil decorre de diferenças intrínsecas de aritmética de ponto flutuante / otimizações do compilador entre LLVM nativo e Clang/WASM, sendo anterior à implementação do MIDI Clock.
   - A Issue #48 permanece aberta como exceção documentada no release gate sem impedir o merge da PR #52.

---

## 13. Hotfix 5.2.1 — Interoperabilidade WASM BigInt (i64) & Recuperação do Web Release

### 13.1 Diagnóstico da Regressão Pós-Merge PR #52
Após a integração da PR #52 na branch `main`, o workflow de CI `Web Demo (Build + Deploy Pages)` falhou no teste integrado de ponta a ponta (`host/web/tests/webMidiClock.test.mjs`) com o seguinte erro em tempo de execução:
```text
TypeError: Cannot convert a BigInt value to a number
    at Module._hydra_dsp_midi_clock_event
    at ccall
    at Object.midiClockEvent
    at HydraProcessor.onMessage (hydra-processor.js)
```

**Causa Raiz**:
Nas versões modernas do Emscripten (incluindo a 3.1.62 utilizada no repositório), a legalização de inteiros de 64 bits (`i64`) para WebAssembly é habilitada por padrão quando `-sWASM_BIGINT=1` não é especificado. Nessa configuração (`WASM_BIGINT=0`), as funções exportadas que recebem `uint64_t` ou `int64_t` na ABI C têm seus argumentos convertidos para pares de inteiros de 32 bits (`(i32, i32)`). A biblioteca JavaScript gerada (`hydra_dsp_web.js`) espera números JavaScript (`Number`) para preencher esses slots. Quando o AudioWorklet (`hydra-processor.js`) chamou a função encapsulada por `cwrap('...', 'number', ['number', 'number', 'bigint'])` passando instâncias de `BigInt`, a conversão interna para número falhou (`TypeError: Cannot convert a BigInt value to a number`).

### 13.2 Política Única de Interoperabilidade i64: `-sWASM_BIGINT=1`
Para assegurar fidelidade de 64 bits sem perda de precisão ou truncamento arbitrário, foi adotada a interoperabilidade nativa de `BigInt` com WebAssembly em todo o projeto:
1. **Configuração de Linker Emscripten (`CMakeLists.txt`)**:
   - Adicionada a flag global:
     ```cmake
     if(EMSCRIPTEN)
         add_link_options("SHELL:-sWASM_BIGINT=1")
     endif()
     ```
   - E reforçada especificamente no target `hydra_dsp_web`:
     ```cmake
     target_link_options(hydra_dsp_web PRIVATE
         "SHELL:-sEXPORTED_FUNCTIONS=['_hydra_dsp_create',...]"
         "SHELL:-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','addFunction','removeFunction']"
         "SHELL:-sALLOW_TABLE_GROWTH=1"
         "SHELL:-sWASM_BIGINT=1"
         "SHELL:-sMODULARIZE=1"
         "SHELL:-sEXPORT_NAME='createHydraDspModule'"
     )
     ```
   Com `-sWASM_BIGINT=1`, a assinatura WebAssembly exportada preserva nativamente o tipo `i64` (`(i32, i32, i64) -> i32`), aceitando objetos `BigInt` do JavaScript de forma direta, atômica e sem legalização.

2. **Auditoria e Padronização de Assinaturas C-API no Host Web**:
   Todas as funções da C-API com argumentos ou retornos de 64 bits foram auditadas e padronizadas no `HydraProcessor`:
   - `hydra_dsp_midi_clock_event(handle, status, timestamp_frames)`: `['number', 'number', 'bigint']`
   - `hydra_dsp_tap_tempo_event(handle, timestamp_frames)`: `['number', 'bigint']`
   - `hydra_dsp_transport_locate(handle, sample_pos)`: `['number', 'bigint']`

3. **Conversão Segura e Validação de Limites (`toUint64BigInt`)**:
   Implementado helper robusto de conversão em `host/web/hydra-processor.js`:
   ```javascript
   function toUint64BigInt(val, fallback = 0n) {
       if (val === undefined || val === null) return fallback;
       try {
           if (typeof val === 'bigint') {
               if (val < 0n) return 0n;
               if (val > 0xFFFFFFFFFFFFFFFFn) return 0xFFFFFFFFFFFFFFFFn;
               return val;
           }
           const num = Number(val);
           if (!Number.isFinite(num) || isNaN(num) || num <= 0) return 0n;
           if (num >= 18446744073709551615) return 0xFFFFFFFFFFFFFFFFn;
           return BigInt(Math.round(num));
       } catch (_) {
           return fallback;
       }
   }
   ```
   Garante conformidade estrita no intervalo $[0, 2^{64}-1]$, evitando falhas na presença de `Number`, `BigInt` ou strings numéricas.

4. **Preservação de Posição de Frames no AudioWorklet**:
   Adicionado o rastreamento acumulativo `this.totalFramesProcessed = (this.totalFramesProcessed || 0) + frames;` em `HydraProcessor.process()`, permitindo cálculo contínuo de referências temporais no processador de áudio.

### 13.3 Matriz de Testes Expandida (Web & C++)
- Suíte `host/web/tests/webMidiClock.test.mjs` expandida para 15 testes integrados cobrindo:
  - Cenário A (120 BPM regular e travamento de PLL).
  - Cenário B (90 BPM) e Cenário C (180 BPM).
  - Cenário D (Transporte Start, Stop e Continue sem perda de fase).
  - Cenário E (Limites temporais de 64 bits: `0ULL`, `1000ULL`, `2^32-1`, `5e9`, `MAX_SAFE_INTEGER`, `UINT64_MAX` e strings numéricas).
  - Cenários F e G (Jitter de entrega e lotes de mensagens preservando diferenças temporais).
  - Cenário H (Regressão de Tap Tempo via WASM com timestamps de 64 bits).
  - Cenário I (Transport Locate com argumento `sample_pos` de 64 bits via WASM).
- 40/40 testes web passando em 100%.
- C++ `testAbiCompatibility` reforçado com validação de limites `0ULL`, `1000ULL`, `4294967295ULL`, `5000000000ULL` e `kTapTempoUseCurrentTime`.
- Algoritmo DSP do PLL e estimador de clock mantidos 100% inalterados.
