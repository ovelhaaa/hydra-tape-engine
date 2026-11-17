# HYDRA TAPE ENGINE 2.0 — TAP TEMPO V1 SPECIFICATION
## Performance-Ready Musical Tempo Input & GTC Integration

---

## 1. Visão Geral e Princípios Arquiteturais

O **Tap Tempo V1** é o subsistema do Hydra Tape Engine 2.0 responsável por converter toques sucessivos do usuário em uma estimativa estável, responsiva e musical de andamento (BPM), encaminhando o resultado diretamente ao **Global Transport Clock (GTC)**.

### Princípio Cardeal: Fonte Única de Tempo Musical

> [!IMPORTANT]
> **O Tap Tempo é estritamente um componente de entrada e estimação de andamento.**
> Não é criado um segundo relógio musical. A única fonte autoritativa de tempo, fase musical e contadores de amostras para todo o motor DSP continua sendo o **Global Transport Clock (GTC)**.

O Tap Tempo estima e valida o BPM a partir dos toques; o GTC recebe o novo valor através de `setBpm()` e atualiza a taxa de avanço do transporte de forma contínua e sem descontinuidades de fase.

O componente `TapTempo`:
- **NÃO** processa amostras de áudio.
- **NÃO** avança a fase musical nem mantém contadores paralelos de beats.
- **NÃO** reinicializa o transporte (`reset`), não zera a posição musical (`locate(0)`).
- **NÃO** reinicializa buffers de delay nem o módulo Reverse V2.
- **NÃO** realiza alocações dinâmicas de memória (`new`, `malloc`, `std::vector`) nem operações de I/O em tempo de execução.

---

## 2. Arquitetura do Componente (`dsp/core/tap_tempo.hpp`)

O componente foi implementado como uma classe independente em C++17, sem dependências externas de alocação ou frameworks:

```
[Web UI (Mouse / Touch / Key 'T')] ---\
[ESP32-S3 Serial CLI ('tap')] --------+--> hydra_dsp_tap_tempo_event(handle, timestamp)
[Futuro Footswitch GPIO (ISR/Queue)] -/              |
                                                     v
                                          +---------------------+
                                          | TapTempo Estimator  |
                                          | - Debounce (80 ms)  |
                                          | - Nominal [30, 300] |
                                          | - Circular Mediana  |
                                          | - Outlier Reject    |
                                          | - Timeout (3.0 s)   |
                                          +---------------------+
                                                     | (BPM Validado)
                                                     v
                                          +---------------------+
                                          |  Global Transport   |
                                          |     Clock (GTC)     |
                                          | (Preservação Fase)  |
                                          +---------------------+
                                                     |
                                       +-------------+-------------+
                                       |                           |
                                       v                           v
                             [Reverse Tape Echo V2]       [Multi-Head Delay]
```

### Estrutura de Estado

- `sampleRate_`: Taxa de amostragem configurada (e.g., 44.1 kHz no hardware ESP32-S3, 48 kHz no Web/WASM).
- `lastValidTimestampFrames_`: Timestamp absoluto em frames do último toque aceito.
- `tapCount_`: Quantidade de toques válidos na sequência atual.
- `intervals_[5]`: Buffer circular de até 5 intervalos válidos recentes (em segundos).
- `historyCount_`: Número de intervalos registrados no buffer (0 a 5).
- `estimatedBpm_`: BPM estimado pelo estimador central.
- `appliedBpm_`: BPM efetivamente validado e encaminhado ao GTC.
- `confidence_`: Nível de confiança da estimativa ($0.0 \dots 1.0$).
- `consecutiveOutliers_`: Contador de intervalos consecutivos divergentes da mediana.
- `pendingNewTempoInterval_`: Intervalo candidato quando uma nova cadência é iniciada.
- `status_`: Estado da detecção (`Idle`, `Acquiring`, `Locked`, `Rejected`).
- `lastRejection_`: Causa de rejeição do último toque (`None`, `Bounce`, `OutOfRange`, `OutOfOrder`).
- `isTimedOut_`: Flag booleana de expiração de sessão (> 3.0 s sem toques).

---

## 3. Domínio Temporal e Políticas de Conversão

Os eventos de tap podem ser gerados em diferentes contextos de execução. Para assegurar coerência musical e estabilidade, todos os toques são traduzidos para o domínio do contador absoluto de amostras (`absolute_sample_position`) do motor DSP:

### 3.1 Web (AudioWorklet & Interface)
- **Origem dos eventos:** Cliques de mouse, toques na tela sensível ou acionamento da tecla `T` são capturados na thread principal da UI.
- **Opção Adotada:** A thread de UI despacha a mensagem `{ type: 'tapTempoEvent', timestampFrames: 0 }` via `MessagePort` para o `AudioWorkletProcessor`. No processador de áudio, o evento é processado no início do bloco de renderização (128 amostras), adotando a posição absoluta da amostra daquele bloco (`absolute_sample_position`).
- **Precisão e Jitter:** A entrega via `MessagePort` adiciona uma latência típica de 2 a 8 ms (dependente do agendamento do navegador). Como o tempo musical humano possui jitter motor natural de $\approx 15 \dots 35\text{ ms}$, essa abordagem é completamente estável e imune a deriva entre relógios de parede (`performance.now()`) e relógios de áudio (`AudioContext.currentTime`). O estimador por mediana absorve integralmente esse jitter residual.

### 3.2 ESP32-S3 CLI
- Na tarefa serial (Core 0), o comando `tap` captura a timestamp imediatamente ao receber o comando antes de qualquer operação de parsing ou impressão serial.
- O evento é encaminhado diretamente ao `HydraDspEsp32Adapter::tapTempoEvent(0)`, associando o frame corrente do transporte sem bloquear a tarefa de áudio (Core 1).

### 3.3 Preparação para Footswitch Físico
- Um pino GPIO com interrupção por borda de descida (contato momentâneo fechando para GND com pull-up interno ou externo) registrará o instante exato via `esp_timer_get_time()` ou consulta atômica do contador de samples na ISR.
- Uma fila lock-free (ou `xQueueSendFromISR`) encaminhará o timestamp diretamente para consumo na tarefa de áudio, com zero impacto na latência do DSP.

---

## 4. Debounce e Intervalos Válidos

### 4.1 Debounce (Rejeição de Repiques)
- **Janela de Debounce:** **80 ms** ($0.080\text{ s}$).
- Qualquer evento que ocorra com $\Delta t < 80\text{ ms}$ em relação ao último toque válido é rejeitado com status `HYDRA_DSP_TAP_REJECT_BOUNCE`.
- **Regra Fundamental de Preservação:**
  - O toque de bounce **não** altera o BPM.
  - **Não** entra no histórico de intervalos.
  - **Não** atualiza a referência `lastValidTimestampFrames_`.
  - Isso garante que se um usuário acionar um botão com repique (e.g. aos 15 ms), o toque seguinte legítimo (e.g. aos 500 ms) seja medido a partir do primeiro toque e não a partir do repique.

### 4.2 Faixa Nominal de BPM
- **Faixa Válida:** **30.0 BPM a 300.0 BPM**.
  - A 300 BPM: intervalo = $60.0 / 300.0 = 200\text{ ms}$.
  - A 30 BPM: intervalo = $60.0 / 30.0 = 2000\text{ ms}$.
- Toques com intervalos entre 80 ms e 200 ms (> 300 BPM) ou entre 2000 ms e 3000 ms (< 30 BPM) são rejeitados com `HYDRA_DSP_TAP_REJECT_OUT_OF_RANGE`.
- A referência temporal é atualizada para permitir que o toque seguinte estabeleça um intervalo válido.

---

## 5. Estimativa Robusta de BPM e Rejeição de Outliers

O algoritmo foi projetado para equilibrar **resposta imediata ao vivo** e **rejeição de toques acidentais**:

```
Sequência de Toques:
Tap 1 -----> Inicia sessão de detecção (tapCount = 1). Status = ACQUIRING. Confiança = 0.0.
Tap 2 -----> Primeiro intervalo (I0). Estimativa imediata: BPM = 60 / I0. Status = ACQUIRING. Confiança = 0.40.
Tap 3 -----> Segundo intervalo (I1). Compara com I0:
             - Se consistente (|I1 - I0| / I0 <= 20%): BPM = 60 / média(I0, I1). Status = LOCKED. Confiança = 0.75.
             - Se inconsistente: reancora a estimativa no novo intervalo (responsividade).
Tap 4+ ----> Histórico de 3 a 5 intervalos:
             - Calcula mediana dos intervalos históricos.
             - Compara novo intervalo I com a mediana: desvio relativo = |I - Med| / Med.
             - Se desvio <= 20%: Inlier. Adiciona ao buffer circular, atualiza mediana, Confiança >= 0.85.
             - Se desvio > 20%: Outlier detectado!
```

### Política de Outliers e Mudança Intencional de Andamento

1. **Toque Isolado Inconsistente (Stumble/Acidental):**
   - O primeiro intervalo divergente (`consecutiveOutliers == 1`) é armazenado como candidato pendente (`pendingNewTempoInterval`), mas **não altera o BPM aplicado nem corrompe o buffer circular**.
   - Se o toque seguinte retornar à cadência anterior, o outlier isolado é descartado e o andamento original permanece intacto.
2. **Mudança Intencional de Andamento:**
   - Se o usuário deliberadamente mudar de andamento (e.g. de 120 para 90 BPM), o segundo toque consecutivo na nova cadência produz um segundo intervalo consistente com o primeiro intervalo divergente.
   - O algoritmo detecta `consecutiveOutliers >= 2` com intervalos mútuos consistentes, **confirma a mudança intencional**, limpa os intervalos do andamento antigo e trava instantaneamente no novo BPM!
   - Convergência completa em apenas 2 intervalos da nova cadência.
3. **Mudança Gradual (Aceleração/Desaceleração):**
   - Variações graduais ($\Delta < 20\%$ entre beats adjacentes) são tratadas continuamente como inliers, permitindo que a mediana acompanhe a aceleração de forma orgânica.

---

## 6. Timeout e Nova Sequência

- **Limite de Timeout:** **3.0 segundos** sem novos toques válidos.
- Quando o tempo decorrido ultrapassa 3.0 s:
  - A sessão atual é encerrada (`isTimedOut = true`, `status = Idle`).
  - O histórico de intervalos é resetado.
  - **O último BPM confirmado no GTC é rigorosamente preservado.**
  - O transporte não é interrompido nem reiniciado; linhas de atraso e o Reverse permanecem inalterados.
- O próximo toque que chegar inicia uma nova sequência de detecção como `Tap 1`, evitando qualquer cálculo espúrio de intervalo através do intervalo de silêncio.

---

## 7. Convenção Rítmica (Half-Time / Double-Time)

- O Tap Tempo V1 adota a convenção métrica padrão: **1 intervalo entre taps = 1 semínima (quarter-note beat)**.
- Não há inferência heurística oculta nem normalização silenciosa por metade ou dobro de andamento.
- Modos half/double explícitos ficam reservados para evolução futura.

---

## 8. Integração com Global Transport Clock (GTC)

Quando uma estimativa de BPM é aceita pelo algoritmo:
1. O valor é validado no intervalo nominal $[30.0, 300.0]$.
2. O BPM é encaminhado para `GlobalTransportClock::setBpm(newBpm)`.
3. O GTC preserva a fase musical e posição do transporte de forma contínua:
   $$m(n) = m_{\text{origin}} + (n - n_{\text{origin}}) \cdot \frac{\text{BPM}}{60 \cdot f_s}$$
4. O novo BPM é automaticamente herdado por:
   - **Reverse Tape Echo V2:** Em modo Rhythmic, a janela de reprodução adapta sua duração em milissegundos conforme a subdivisão selecionada (e.g., 500 ms em 1/4 @ 120 BPM passa para 666.7 ms @ 90 BPM), mantendo a grade musical sincronizada.
   - **Multi-Head Delay:** Cabeças em modo musical recalculam seus tempos de atraso preservando as razões rítmicas.

---

## 9. Interface da C-API (Versão 1.6.0)

A C-API foi expandida para a versão **1.6.0** preservando estritamente os layouts das structs existentes (`v1_2` com 116 bytes, `v1_3` com 140 bytes, `v1_4` com 148 bytes, e `hydra_dsp_transport_state` com 72 bytes).

### Novas Definições e Estruturas

```c
typedef enum hydra_dsp_tap_status {
  HYDRA_DSP_TAP_STATUS_IDLE = 0,
  HYDRA_DSP_TAP_STATUS_ACQUIRING = 1,
  HYDRA_DSP_TAP_STATUS_LOCKED = 2,
  HYDRA_DSP_TAP_STATUS_REJECTED = 3
} hydra_dsp_tap_status;

typedef enum hydra_dsp_tap_rejection_reason {
  HYDRA_DSP_TAP_REJECT_NONE = 0,
  HYDRA_DSP_TAP_REJECT_BOUNCE = 1,        /* < 80 ms */
  HYDRA_DSP_TAP_REJECT_OUT_OF_RANGE = 2,  /* < 30 BPM ou > 300 BPM */
  HYDRA_DSP_TAP_REJECT_OUT_OF_ORDER = 3   /* timestamp <= last_valid */
} hydra_dsp_tap_rejection_reason;

typedef struct hydra_dsp_tap_tempo_state {
  float estimated_bpm;              /* Offset 0,  4 bytes */
  float applied_bpm;                /* Offset 4,  4 bytes */
  uint32_t tap_count;               /* Offset 8,  4 bytes */
  float confidence;                 /* Offset 12, 4 bytes */
  float time_since_last_tap_s;      /* Offset 16, 4 bytes */
  int is_timed_out;                 /* Offset 20, 4 bytes */
  int status;                       /* Offset 24, 4 bytes */
  int last_rejection;               /* Offset 28, 4 bytes */
} hydra_dsp_tap_tempo_state;        /* Total: 32 bytes */
```

### Funções Adicionadas

```c
/* Registra um evento de tap com timestamp absoluto em amostras (0 usa contador corrente) */
int hydra_dsp_tap_tempo_event(hydra_dsp_handle* handle, uint64_t timestamp_frames);

/* Consulta o estado detalhado do estimador de tap tempo */
int hydra_dsp_tap_tempo_get_state(const hydra_dsp_handle* handle, hydra_dsp_tap_tempo_state* out_state);

/* Limpa o histórico e reinicia a sequência do estimador (preserva BPM do GTC) */
int hydra_dsp_tap_tempo_reset(hydra_dsp_handle* handle);
```

---

## 10. Interface com o Usuário e CLI

### 10.1 Web UI
- **Controle Principal:** Botão `TAP` no painel do Global Transport Clock, com acionamento por clique, toque ou atalho de teclado global **`T`** (quando nenhum campo de entrada de texto estiver focado).
- **Feedback Visual:**
  - Badge de status dinâmico: `TAP TO SET TEMPO`, `ACQUIRING`, `TEMPO LOCKED`, `TAP REJECTED`.
  - Telemetria em tempo real: Exibição de `Est: %.1f BPM | Taps: %d | Conf: %d%`.
  - Indicador de pulsos de beat continua derivando exclusivamente do GTC real.

### 10.2 ESP32-S3 CLI
Comandos adicionados ao console serial (115200 baud):
- `tap`: Registra um evento de toque utilizando a timestamp atual do motor. Exibe resposta imediata:
  ```
  [TAP #2] Est: 120.0 BPM | Applied: 120.0 BPM | Conf: 0.40 (ACQUIRING)
  ```
- `tap status`: Exibe o relatório detalhado de estado:
  ```
  === TAP TEMPO STATUS ===
    Current BPM         : 120.0
    Estimated BPM       : 120.0
    Tap Count           : 4
    Confidence          : 0.85 (HIGH)
    Sequence State      : LOCKED
    Time Since Last Tap : 0.52 s
  ```
- `tap reset`: Reinicia o estimador de taps sem alterar o transporte, atrasos ou I2S.

---

## 11. Validação Acústica — Hydra Listening Lab

Pacote gerado em `artifacts/listening-lab/tap-tempo-v1/`:
1. `01_fixed_vs_tapped_120`: Comparação de 120 BPM fixo versus 120 BPM adquirido por taps.
2. `02_reverse_quarter_after_tap`: Reverse em 1/4 travado na cadência pós-aquisição.
3. `03_reverse_eighth_after_tap`: Reverse em 1/8 travado na cadência pós-aquisição.
4. `04_tempo_change_120_to_90`: Transição sem descontinuidades de 120 para 90 BPM.
5. `05_tempo_change_90_to_140`: Aceleração de 90 para 140 BPM com acompanhamento rápido.
6. `06_accidental_tap_rejected`: Rejeição de repique de 30 ms e outlier isolado de 350 ms.
7. `07_timeout_and_resumption`: Pausa de 3.5 s (timeout) e início limpo de nova sessão a 140 BPM.
8. `08_tap_high_feedback_95`: Atualização de andamento estável com saturação magnética a 95% de realimentação.
9. `09_tap_multihead_musical`: Cabeças multi-head alinhadas musicalmente ao andamento adquirido.
10. `10_tap_reverse_percussive`: Material rítmico percussivo com Reverse mantendo alinhamento estrito.

---

## 12. Desempenho Físico no ESP32-S3

Medições realizadas diretamente no hardware físico YD-ESP32-S3 (N16R8 @ 240 MHz, 44,1 kHz / 128 frames, orçamento de 2902,49 µs/bloco):

| Cenário de Teste | Média (µs) | Carga Média (%) | P99 (µs) | Carga P99 (%) | Overruns |
| :--- | :---: | :---: | :---: | :---: | :---: |
| 1. Standard Delay, GTC Active | 1448.76 | 49.91% | 1536.00 | 52.92% | **0** |
| 2. Reverse V2 Rhythmic Sync | 1476.02 | 50.85% | 1543.00 | 53.16% | **0** |
| 3. Reverse V2 Free Mode | 1488.06 | 51.27% | 1555.00 | 53.57% | **0** |
| 4. Reverse V2 Rhythmic (95% FB) | 1496.44 | 51.56% | 1544.00 | 53.20% | **0** |
| 5. Reverse com Mudanças de BPM | 1484.41 | 51.14% | 1594.00 | 54.92% | **0** |
| 6. Cadeia Completa (Delay + Warp + Spring) | 1607.67 | 55.39% | 1693.00 | 58.33% | **0** |
| 7. Carga Máxima (Reverse V2 + Warp + Spring + GTC) | 1583.14 | 54.54% | 1644.00 | 56.64% | **0** |

- **P99 Máximo:** 1693 µs (58.33% do orçamento — meta < 70% plenamente atingida).
- **Variação de Heap:** 0 bytes (zero vazamento de memória).
- **Falhas de I2S TX:** 0.
