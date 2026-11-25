# Estabilidade F2

Matriz final em execução. Cada candidata é testada a44100/48000Hz, feedback público
50/75/85/95/100, Single/Multi-Head/Reverse/Ping-Pong. Dez fontes incluem
impulso, burst40Hz, senoides80/130/200Hz, guitarra, baixo/synth graves e percussão.
Todos os sinais passam por Single em todos os feedbacks; fontes adicionais
distribuídas nos outros modos, com burst grave em todos. Todos os casos FB95/100
usam60s, inclusive fontes musicais. Multi-Head+Ping-Pong tem quatro caudas60s
adicionais por candidata. Suite canônica27 gates também executada separadamente.

Instrumentação offline captura ambos canais, feedback, record após DC antes e
depois do clamp±4, e escrita magnética. Todos os blocos são verificados contra
saída sem instrumentação bit-exact; nada é adicionado à audio task do firmware.
Picos globais e RMS por segundo internos/externos registrados. Exige finitude,
zero samples no teto.9899 **após parar a entrada** e record pré-clamp abaixo4.
A guitarra sintética full-level fast/new/FB100 já sobrecarrega o monitor C0
durante a entrada (24 samples no teto em44100Hz); isso permanece visível como
`full_run_zero_ceiling_gate_pass=false`, não como zero clipping. Só qualificamos
essa sobrecarga de entrada se a mesma cena C0 também a apresenta, a cauda não
clipa e todos os gates de estados internos/decay passam. Clipping novo ou
persistente falha. Nenhum nível de entrada foi abaixado para esconder problema.
As caudas canônicas de burst continuam exigindo zero teto em toda a execução.

Screens12s comparam cauda tardia com janela anterior de vários segundos.
Não aplicar regra de crescimento de dois segundos adjacentes a impulsos curtos:
as janelas de blocos128 podem conter frações diferentes do pico de cada eco.
Uma versão inicial dessa bancada sinalizou +.34dB interno em C0 aos7–8s,
enquanto a saída decaía monotonicamente; diagnóstico preservado. Não houve
mudança de DSP para corrigir essa medição. O gate canônico de caudas60s mantém
delta último segundo≤.05dB e pico últimos10s≤1.02×segundos10–20, mais critério
interno de crescimento≤.05dB no final. Thresholds históricos não alterados.

As calibrações iniciais falharam no impulso0.5/fast-new/bump100/FB100/44100:
C0 decai; B180Hz cresce+.0788dB no último segundo e C240Hz+.1085dB,
com razão de pico tardio/intermediário1.00212 e8.99261 respectivamente.
Sweep só do HPF: B260Hz passou (−.5866dB, razão.01508), C280Hz passou
(−.7904dB, razão.00063). C260Hz ainda falhou (+.1176dB), por isso não usada.
JSONs dessas tentativas preservados como diagnóstico; não são sucesso F2.

Nenhuma comparação automática estabelece sustain agradável ou ausência de
artefatos perceptuais. Hardware PENDING_PHYSICAL; audição PENDING_HUMAN.

Pico de saída global: .99 nos casos de sobrecarga ativa, finito. Quantidade de
samples no teto durante guitarra/fast-new/FB100 (44100 /48000): C0=24/2,
Warm=71/47, Vintage=38/17. Os candidatos produzem mais clipping transitório
nesse estresse; isto é uma diferença dinâmica material para avaliação humana,
não uma melhoria. **Zero samples no teto após cessar a entrada em todos os
462 casos**. Estados antes do clamp e da escrita continuam limitados e decaem.
Sem modificar ganho, limiter ou compressor. Esses eventos não são mascarados
pela normalização MATCHED nem usados como evidência de preferência.
