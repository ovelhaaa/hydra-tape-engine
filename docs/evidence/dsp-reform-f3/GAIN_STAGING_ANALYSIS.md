# Gain staging e métodos

Os três ganhos experimentais estão em `saturation_calibration_experiment.hpp`;
as expressões originais são preservadas literalmente no ramo F3=0.
Em pequeno sinal, produto A=.97416, B=.965952 (−.07349dB relativo a A),
C=.968136 (−.05388dB). Nenhuma candidata aumenta esse ganho nominal.
O ganho **máximo público de feedback não mudou**: safeFeedback≤.88,
Reverse=.83×min(FB/100,.95). A suavização de Reverse permanece intacta.
Assim, FB95/100 podem produzir o mesmo alvo normal sem serem testes redundantes:
os estresses F2 também usam diferentes speed/age/bump/fontes.

`feedbackDrive` mantém contribuição do feedback (coeficiente.30), envelope de
entrada (.18 com teto.22) e tapeAge(.10). Pré/de-emphasis continuam o par
escalar recíproco existente. B/C redistribuem a excursão entre os saturadores,
sem alterar input drive, filtros, compressor, magnetização ou limiter.
Menor compressão em B pode elevar picos apesar do ganho linear menor; por isso
o gate de clipping compara as mesmas entradas e não se baseia no produto linear.

`dynamics.py`: 2 taxas×8 feedbacks×4 níveis×3 sinais=192 cenas/variante.
Seno1kHz e dois tons900/1100Hz, pico nominal−24/−12/−6/0dBFS; dois tons
somados com fator.5. Um segundo de entrada e3s de cauda. Degrau de burst1kHz
com fatores.25/1/.25 em0/.25/.5s; RMS de10ms a partir da chegada500ms.
O joelho é memoryless, portanto estes são tempos de resposta do sistema,
não ataque/release de um compressor com detector.

Instrumentação offline F2 captura feedback L/R, record antes do clamp±4,
saída magnética e monitor L antes do limiter; saída final L/R comparada
bit-exact com o motor não instrumentado em cada cena. RMS/pico/crest por estágio
separam compressão do loop, saturação magnética e clipping de monitor/saída.
Não inclui prova de headroom analógico ou de periféricos I2S.

Harmônicos1–10 por projeção na janela650–850ms, THD2–10 relativo à fundamental.
IMD3: RSS700/1300Hz dividido por RSS900/1100Hz; também registra200/2000Hz.
São **estimativas do motor completo**, não medição isolada de THD do saturador:
filtros, memória magnética, leitura/modulação e repetição participam.
RMS/pico/crest por janelas de repetição, ganhos incrementais, burst recovery e
true peak estimado por `scipy.signal.resample_poly`4x são descritivos.
Janelas podem conter caudas sobrepostas; não equivalem a passagens isoladas.
Oversampling existe somente na bancada e não altera o firmware.

Estabilidade60s usa as regras históricas sem relaxamento: finitude, crescimento
final≤.05dB e razão pico tardio/intermediário≤1.02; estados antes do clamp<4;
zero teto após parar entrada. Clipping ativo é comparado com F3-A por cena/taxa.
Convenção prospectiva para sinalizar investigação automática: aumento acima
de max(3 amostras,10% da contagem A). A comparação numérica completa permanece
visível mesmo abaixo disso; não é aprovação humana de clipping adicional.
Nenhum ganho/limiter de saída é ajustado para passar gates.
