# Native/WASM e não regressão

Local: MinGW UCRT GCC14.2.0, Emscripten3.1.62, Release/ffp-contract=off.
ClassicC=1/F2=2/F3=0|1|2 verificados nos comandos efetivos de todos os targets.
Paridade dos cinco cenários existentes: **max_abs=0,RMSE=0 nas três variantes**,
com limites originais1e-5/1e-6. Inputs/LUT idênticos.

F3-A:10/10 renders bit-exact com a fonte F2-C@2c15b88 compilada na mesma
toolchain e receita F0.2/F1. Manifesto próprio F3-A inclui hashes de entradas,
saídas, parâmetros/readback, flags e compilador. Não substitui manifestos F2.
Controle A/C0/Warm:10/10 bit-exact cada contra a fonte F2 congelada.
Warp: bit-exact A/B/C. API1.7/38 e consumidor C legado preservados.
LUT/bias, headers públicos, presets e goldens não modificados.

CTest original:16/17 Native e14/15 WASM por candidata; falha somente na
assertion de energia da rota A em `regression_core.cpp:255`.
O teste original e seus limites permanecem intactos. Cópia de observação
Native **e WASM** avalia todas as assertions posteriores, confirma full/split
L/R exato e só registra essa expressão conhecida. Contrato específico de
wrap exige ambas saídas finitas/não silenciosas e identidade por blocos.
Reverse V2, Multi-Head/Ping-Pong, transport, Tap/MIDI, LUT e ABI executados.
Suíte canônica de motor:27/27 por candidata,44.1/48kHz e caudas60s.

Primeira execução Native de A:15/17 por flake de scheduling Tap previamente
documentado na F0.1/#63 (`validSnapshots.load()>0`), junto da falha A conhecida.
Log/XML preservados em `variant0/contracts-native-initial/`. Sem alteração
de teste ou threshold. Repetição integral:16/17, Tap passou. Não apresentar
a primeira execução como verde nem incorporar #63 sem necessidade.

Seleções inválidas F3 fora0–2 ou B/C sem ClassicC/F2Vintage falham compilação.
Default0 preserva as expressões históricas literalmente; nenhuma UI ou ID novo.
Tamanho de objeto e comandos completos arquivados em `compatibility/`.
Tempo desktop não é medição física. Firmware experimental compilado
separadamente; flash/performance ESP32 continuam **PENDING_PHYSICAL**.
