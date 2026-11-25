# Auditoria da interface física Live v0.1

Base: `f3bd0cdae7ef662f25fa68876cdf06442d654724`, PR #71, branch
`integration/hydra-vintage-live-v0.1`. Trabalho isolado em
`integration/hydra-live-v01-audio-hardware`, sem alterar a worktree com UI local.

## Preflight e mapa da base

Lidos: auditoria musical de 2026-11-23, roteiro/G0, reconciliação F0.1,
consolidação F0, README/manifesto/protocolo F0.2, integração Live e roteiro físico.
F0.2 é baseline A independente, não autorização para promover C. PR #71 confirma
F4/F5 integradas na Live, com gates offline aprovados; físico ainda pendente.
Nenhuma fase DSP nova é implementada. API 1.7/38; #61/#62 não incorporadas.

Referências abaixo são da **base**, não números após inserções:

| Código | Achado |
|---|---|
| `src/main.cpp:40–57` | GPIO15/16/17/18, I²S0, 44100Hz, 128 frames |
| `src/main.cpp:834–850` | RX preenchido com zero, retorno/bytes ignorados |
| `src/main.cpp:868–875` | words signed32 divididos por 2^31; últimos 8 bits não descartados |
| `src/main.cpp:884–972` | TapeCore canônico, tanh fallback, gate e Freeverb |
| `src/main.cpp:546–559` | TX estéreo L/R saturante, masterVolume×0,90 |
| `src/main.cpp:1477–1518` | identity e audiohealth existentes |
| `src/main.cpp:2301–2345` | master TX/RX Philips32, MCLK omitido; erro de instalação podia ser sobrescrito |
| `scripts/build_esp32_dsp.py:9–29` | digest de fontes rastreadas, commit/dirty, ambiente/flags |
| `host/native/esp32/hydra_dsp_esp32_adapter.cpp` | adaptador canônico preservado |
| `lib/DisplayUI/include/hardware_pins.h:13–30` | OLED1/2, encoder4/5/6, botão7 |

Hipótese: ADC sem SCKI pode não converter; sucesso de read não prova sinal.
Bits pós-payload não devem contribuir para sinais baixos. Corrigir fronteira
PCM/clock sem compensar com ganho ou alterar algoritmos musicais.

## Implementação e custo

Somente `HYDRA_AUDIO_HWTEST=1` ativa MCLK14, checagens de init/RX e diagnósticos.
Ambientes anteriores mantêm opções/roteamento. Core/ABI/presets/goldens intactos.
Driver legacy auditado nos headers efetivos Arduino3.3.6/IDF5.5.2: mck_io_num,
PLL_160M, múltiplo256, Philips32 estéreo e RX slave interno do TX. Fração nominal
14+76/441 exata no S3. Detalhes elétricos/fontes no documento de montagem.

`src`, `identity`, `perf`, `bench`, `audiohealth` preservados. CLI adiciona
`hw normal|silence|tone|left|right|monitor` e `adc`. Frame de diagnóstico é
compartilhado com teste Native/WASM: silêncio, senoide440Hz moderada L/R,
monitor linear ganho0,25. Normal mantém DSP/ganho anteriores.

Mailbox lock-free verificada por static_assert. Estado fixo <128B; nenhum buffer
longo/motor/tarefa extra. Normal: conversão24bit, decisões por bloco, inspeção
de retorno e bytes. Captura solicitada: 256 samples/bloco com soma/quadrado/pico
e near-FS por canal durante128 blocos. Sqrt/prints só na CLI. Tom só em diagnóstico,
polinômio limitado sem libm por amostra. Read conserva timeout existente10ms;
não adiciona bloqueio novo. Custo em us não foi medido; comparar fullWorkUs e
deadline em hardware antes de promover. Captura ADC fica fora do cronômetro DSP
existente, portanto perf DSP sozinho não mede seu custo.

## Validação e distribuição

Testes novos: zero, sinais+/−, extremos24bit, LSBs/ruído de padding, saturação32,
NaN, independência L/R, RMS/DC/near-FS, oscillator e todas as rotas diagnósticas,
conflitos GPIO Live. Compilados nos mesmos targets Native e WASM.
Não provam alinhamento nem canal físico: roteiro de estímulo e captura I²S obrigatório.

Workflow `hydra-live-audio-hardware.yml` conserva todos os gates da Live:
CTest Native/WASM, paridade estrita00/10/01/11, matriz estática/F3-A congelada,
full-engine e feedback60s com estágios produção/Live. Só publica após esses gates
e build ESP32. Logs/JSON são incluídos no artifact
`hydra-live-v01-pcm1808-pcm5102-hwtest`. Manifesto contém commit, identidade,
flags, pinout, hashes BIN/ELF e situação física. O pacote exige checkout limpo.
Não há flash, merge, alteração de PR F4/F5 ou atualização de goldens.

Primeira rodada local: Native30/30, build ESP32 SUCCESS. Logs de execução e
demais resultados finais ficam em `gates/` no artifact, associados ao commit
do manifesto; não confundir esta observação com certificado físico.

## Gates ainda físicos

`READY_FOR_HARDWARE_WIRING — PENDING_PHYSICAL_CLOCK_VERIFICATION` é condicionado
à inspeção dos exemplares antes de soldar/alimentar: header→chip/IO14,
rails/reguladores/straps, coerência/níveis/clock, formato real/LR/sinais baixos,
escuta A/B e benchmark/deadline. Requisitos e pendências explícitos em
[montagem](HYDRA_PCM1808_PCM5102_WIRING.md). Não declarar ADC/DAC funcional na
placa, nem sonoridade física aprovada, com base apenas no firmware compilado.
