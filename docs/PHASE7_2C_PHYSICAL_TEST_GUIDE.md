# Hydra 7.2C — protocolo físico de teste no ESP32-S3 (N16R8)

**Candidato selecionado em audição:** C / Classic Tape Feedback.
**Estado:** audição humana aprovada, validação física pendente; sem merge.
**Risco principal:** overhead da nova cadeia de reprodução em cada amostra do
feedback, quando o orçamento de 128 frames a 44.1 kHz é apenas **2902.49 us**.

## Identidade da placa e fontes da medição

Firmware: `src/main.cpp`, PlatformIO Arduino, ESP32-S3 240 MHz, 44.1 kHz,
128 frames, I2S 32-bit stereo, DMA 4x512, audio task core1, priority5.
Core0 roda a interface/CLI. Não avaliar outra placa/clock/configuração como
substituto sem documentar. No firmware desta revisão:
GPIO15=BCLK, GPIO16=LRCK, GPIO17=I2S DOUT, GPIO18=I2S DIN.
O áudio sai em nível de linha pelo **DAC I2S PCM5102**, se conectado.
Um DAC não é um amplificador de alto-falantes.

O ambiente normal `esp32-s3-devkitc-1` **não habilita C**.
O ambiente `esp32-s3-classic-c` habilita o símbolo exclusivo
`HYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING=1`.
O `esp32-s3-classic-c-stageprofile` habilita C e o profiler, e **não**
é comparável em tempo diretamente com o build sem profiler.

## Preparação de software (Windows / VS Code / PlatformIO)

Para não afetar o projeto local, faça um checkout próprio da branch:

```powershell
git clone https://github.com/ovelhaaa/hydra-tape-engine.git hydra-physical-72
cd hydra-physical-72
git switch --track origin/experiment/phase7-2-feedback-routing
git rev-parse HEAD
pio run -e esp32-s3-classic-c
```

Antes de gravar, confirme **PASS** no workflow da PR #56 que compila o
firmware C real no PlatformIO. O script `scripts/build_esp32_dsp.py` compila
`dsp/core` e o adaptador ESP32 na placa, não um segundo DSP simplificado.

Escolha o COM correto usando `pio device list` (o COM15 do registro
histórico não é garantido em toda sessão). Salve uma cópia do firmware atual
ou tenha uma imagem/checkout conhecida para retorno à versão A. Nunca apague
flash, NVS ou LittleFS como requisito para este teste.

```powershell
pio run -e esp32-s3-classic-c -t upload --upload-port COM15
pio device monitor --port COM15 --baud 115200
```

Troque COM15 pela porta efetivamente identificada; feche o monitor antes
do upload. Se a USB Serial/JTAG estiver enumerada mas não responder,
verifique o reset e a seção de recuperação em
`docs/PHASE65_PHYSICAL_HARDWARE_ACCEPTANCE.md`. A ausência de texto
não equivale a firmware validado.

No monitor, selecione **Newline** e digite:

```text
?
identity
perf
audiohealth
```

Exigir `FeedbackRouting=PHASE72_CLASSIC_C` junto do commit esperado,
CPU=240 MHz, PSRAM=8 MB, SampleRate=44100, BlockFrames=128 e I2SInitialized=1.
Se o marcador C não aparecer, **não declare que o teste rodou com C**.
`audiohealth` verifica o caminho digital PCM e o resultado da escrita I2S;
não prova som analógico saindo do DAC.

## Configuração-base antes de cada cenário

Usar CLI real `?` (não `help`). Usar synth interno sem necessidade de
arquivo MP3/SD/LittleFS, de modo a medir no mesmo caminho da fase física 6.5.
Fixe volume baixo: qualquer feedback muito alto pode produzir sons altos.

```text
src 1
gen 1
vol 10
byp 0
mod 1
mix 70
wrp 0
rev 0
spr 0
frv 0
ppg 0
dly 500
hds 4
fbk 55
drv 30
tps 50
tpa 45
ton 50
hbp 30
nlv 12
dps 10
ftd 10
wwd 10
tempo 120
list
```

O firmware permite entrada ADC via I2S (`src 2`), mas isso exige ADC
físico compatível. Não afirmar validação da entrada analógica usando
apenas `src 1`.

## Os testes

Primeiro execute **baseline A** na placa com a versão compilada da `main`
no env normal `esp32-s3-devkitc-1`, documentando o commit e o `identity`.
Depois instale o candidato C e repita EXATAMENTE as mesmas configurações
de cada caso. Em cada caso, execute `perf reset`, depois
`bench 10000 256`, `perf` e `audiohealth`. Salve o texto cru, sem
editar. Os tempos do bench são histogramas físicos, não estimativas
via Python; o teste de áudio sintético é o mesmo em A e C se os parâmetros
não forem alterados.

| Caso | Alterações à base, nesta ordem |
| --- | --- |
| T1 Delay single head | `hds 4`, `fbk 55`, `rev 0`, `ppg 0` |
| T2 Três heads / estéreo | `hds 7`, `ppg 1`, `fbk 65` |
| T3 Reverse quarter | `hds 4`, `ppg 0`, `rev 1`, `revmode 0`, `revdiv 0`, `fbk 70` |
| T4 Reverse feedback 95 | igual T3, `fbk 95` e **volume baixo** |
| T5 Fita envelhecida | `rev 0`, `tpa 90`, `ton 20`, `dps 35`, `nlv 40`, `fbk 85` |
| T6 Parâmetros extremos | `rev 0`, `tpa 0`, `tps 100`, `ton 90`, `hbp 90`, `fbk 75` |
| T7 Warp controle negativo | `rev 0`, `wrp 1`, `wfb 75`, `wgr 80`, `hds 4` |

Reaplique a base a cada caso, porque os parâmetros são persistentes
durante a sessão e um teste não deve herdar controles do anterior.
Um cenário combinado `wrp 1` + `rev 1` não significa Warp e
Reverse processando simultaneamente: Warp tem prioridade na rota real.

Para análise complementar, abra a compilação
`esp32-s3-classic-c-stageprofile` e repita os casos com
`stageprof 1000 256`. A instrumentação adiciona sobrecarga. Use
`prof2 1000 256` apenas se os contadores estiverem habilitados pelo
build; não use tempos do profiler como benchmark de produção.

## Teste sustentado de pelo menos cinco minutos

Com C de produção (sem profiler), gerador habilitado, Reverse quarter e
feedback alto (comece com 85), mantenha os parâmetros fixos e consulte
`perf` de modo periódico. Conte os blocos no começo e no fim e
registe se ocorreram reinicializações, resets, truncamentos de resposta,
picos inesperados ou cortes audíveis. Faça `audiohealth` fora dos
benchmarks para observar se houve saída PCM.

## Critérios de aceitação — propostos, não provenientes da CI

- **Identidade**: C ativada e exata fonte de firmware documentada.
- **Sem travamentos**: nenhum reset não intencional/watchdog.
- **Contadores**: `DSP Budget Overruns=0`,
  `Full Work Overruns=0`, `I2S TX Write Failures=0` em cada
  sessão verificada; o `bench` também deve registrar `Overruns=0`.
- **Pior caso**: `PeakUs < 2902.49` para os 128 frames a 44.1 kHz.
  `P99Us` deve ter folga significativa, idealmente até ~80% do orçamento
  (objetivo de engenharia, não um gate previamente aprovado).
- **Memória**: sem declínio persistente de heap/PSRAM e sem stack próximo
  de zero. O campo legado `StackMinWords` da CLI é **bytes** no IDF.
- **Áudio**: ausência de clicks, dropouts perceptíveis, saturação
  involuntária e comportamento de feedback perigoso; `audiohealth`
  sinaliza amostras PCM não nulas e TX OK em teste com fonte ativa.
- **Compatibilidade**: condições Warp e Reverse continuam corretas, e
  as medições com C não são atribuídas à versão A.

A base aprovada da fase 6.5 registrou P99 máximo de 2215us no caso
multi-head e peak full-work 2820us em ensaio prolongado — margem mínima
histórica de só 82.49us. A versão C acrescenta processamento: não
assumir aprovação física sem novos números.

## O que enviar para análise

Guarde a saída textual integral de `identity`, `bench`,
`perf`, `audiohealth` em **A e C**; associe cada arquivo à
configuração, data, porta e commit. É suficiente colar os logs no
chat ou anexar um TXT/ZIP com os resultados.

**Não fazer merge da PR #56 a partir do PASS nativo ou apenas por audição:
a decisão final exige avaliação dos números físicos de C.**
