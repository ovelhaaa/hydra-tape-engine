# Implementação e preflight F2

Antes de editar: G0 e roteiro F0–F11, auditoria musical, reconciliação,
F0.2/F1 README/protocolos/manifestos/espectro/desenho F2 e evidência física C.
HEAD inicial `f3281d0`; nenhuma fase posterior incorporada. Autorização desta
F2 vem da solicitação explícita, não do plano. PRs anteriores não modificadas.

Fonte ativa: `src/main.cpp:880–954` → adaptador ESP32 `:138–152` → API C →
`dsp/core/tape_core.cpp`; o TapeModel legado não é utilizado/substituído.
Memória de Tape/Reverse/Warp compartilhada. Playbacks do monitor e privado C
mantêm estados próprios; nenhuma duplicação de filtro por sample.

| Build | HPF/Q | Bump eletrônico em amount50 | Gap eletrônico |
|---|---|---|---|
|0 Classic C0|300Hz/.5|+1dB|original|
|1 Warm Tape|260Hz/.5|+.6dB|original|
|2 Vintage Progressive|280Hz/.5|+.8dB|cutoff original ×.75, mínimo1600Hz|

Mudanças em `tape_core.cpp:499–537` e
`feedback_calibration_experiment.hpp`. Centro60+140*speed e Q.8 do bump
preservados, sem alterar `headBumpAmount` público. Playback privado C,
gap biquad, shelf, magnetização/LUT/bias, saturação, ganho/clamp e compressor
intactos. A multiplicação adicional de C está no recálculo de coeficientes;
cache e frequência de atualização preservados. Seleção inválida ou B/C sem
Classic C gera erro de compilação. Default0 mantém coeficientes originais,
inclusive na rota A.

Hipóteses: B recupera corpo80–300Hz; C permite corpo moderado com perda HF
progressiva. Nenhuma diferença teórica estabelece preferência musical.
Riscos: ringing/acúmulo grave em B e escurecimento redundante slow/worn em C.
Validação: filtros reais configurados por updateParams, loops não lineares,
finitude/estados internos/caudas60s, Native/WASM estrito, C0 congelada e áudio
cego RAW/MATCHED. Sem alterações de goldens ou tolerâncias.

Custo estrutural incremental: **0 bytes** de estados/buffers; **0 filtros ou
operações por sample**, sem alocações/bloqueios/transcendentes adicionais no
hot path. Os coeficientes podem afetar caminhos condicionais via sinal;
benchmark Native e tamanho de objeto são só indicadores preliminares.
ESP32-S3 240MHz/44100/128, deadline2902.49µs: **PENDING_PHYSICAL** nesta build.
Não reutilizar histórico P99~2333µs/full-work~2604µs como medição F2.
Antes de promoção medir C0/B/C equivalentes: DSP P50/P95/P99/peak, full-work,
rajadas de parâmetros, stack, heap/PSRAM, I2S e zero overruns, com flags/firmware
e logs arquivados. Nenhuma candidata promovida automaticamente.

Infra de teste isolada: novo contrato em `tests/core/f2_wrap_contract.cpp`,
registrado em CMake. O teste A original fica intacto. Probe offline acessa os
filtros reais sem mudar o firmware; instrumentação fica apenas em artifacts.

Indicadores Native (MinGW14.2/O3): se��o text84096/84128/84160 bytes
(C0/B/C), delta+32/+64 bytes, data/bss0. S�mbolo do hot path interno mant�m
32304 bytes e wrapper416 bytes nas tr�s builds. Objetos103943/103985/104027
bytes. Indicador desktop Multi-HeadFB85, blocos128/44100: medianas P50
90.8/92.7/90.9�s; P99 205.49/236.53/250.31�s, com picos de scheduling de at�
13.4ms. N�o � ensaio controlado de DSP isolado e n�o resolve o gate f�sico.
Nenhuma estimativa de P99 ESP32-S3 � inferida desses tempos. Arquivo bruto
`artifacts/dsp-reform-f2/native-benchmark.json`.
