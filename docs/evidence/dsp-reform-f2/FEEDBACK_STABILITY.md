# Estabilidade F2

Gates em execução. Cada candidata é testada a44100/48000Hz, feedback público
50/75/85/95/100, Single/Multi-Head/Reverse/Ping-Pong. Dez fontes incluem
impulso, burst40Hz, senoides80/130/200Hz, guitarra, baixo/synth graves e percussão.
Todos os sinais passam por Single em todos os feedbacks; fontes adicionais
distribuídas nos outros modos, com burst grave em todos. FB95/100 usam caudas
60s nos quatro modos e duas taxas (16 caudas/candidata).

Instrumentação offline captura ambos canais, feedback, record após DC antes e
depois do clamp±4, e escrita magnética. Todos os blocos são verificados contra
saída sem instrumentação bit-exact; nada é adicionado à audio task do firmware.
Picos globais e RMS por segundo internos/externos registrados. Exige finitude,
zero samples no teto.9899 e record pré-clamp abaixo4 nesta matriz.

Screens12s comparam cauda tardia com janela anterior de vários segundos.
Não aplicar regra de crescimento de dois segundos adjacentes a impulsos curtos:
as janelas de blocos128 podem conter frações diferentes do pico de cada eco.
Uma versão inicial dessa bancada sinalizou +.34dB interno em C0 aos7–8s,
enquanto a saída decaía monotonicamente; diagnóstico preservado. Não houve
mudança de DSP para corrigir essa medição. O gate canônico de caudas60s mantém
delta último segundo≤.05dB e pico últimos10s≤1.02×segundos10–20, mais critério
interno de crescimento≤.05dB no final. Thresholds históricos não alterados.

Nenhuma comparação automática estabelece sustain agradável ou ausência de
artefatos perceptuais. Hardware PENDING_PHYSICAL; audição PENDING_HUMAN.
