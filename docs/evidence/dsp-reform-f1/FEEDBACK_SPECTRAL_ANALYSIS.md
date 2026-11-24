# F1 — resposta espectral medida do feedback C0

Medições offline nesta entrega, não parecer de audição. Dados completos:
[spectral-measurements.json](spectral-measurements.json). Não houve mudança de
coeficientes ou algoritmo. Probe utiliza as classes reais do core congelado.

## Ordem efetiva e condicionais

Leitura forward/Multi-Head com modulação e blend Reverse já aplicado → delaySig
sem ganhos do monitor → **se C e fora Warp**: dropout privado (LP rápido e
ampGain somente se hfLoss/ampGain < .9995), azimute condicional, shelf −12 dB,
bump de playback, gap biquad, soma 0.20*hiss → troca L/R se Ping-Pong →
**se delayActive** HPF300/Q.5 → feedbackDrive dependente de feedback,
reverseBlend, inputEnv e tapeAge → bump eletrônico → gap one-pole → allpass →
ganho escalar preEmph → rationalSat(x*1.10) → trim .82 → rationalSat(x*1.08)
→ ganho escalar deEmph → feedbackGain (safeFeedback limitado a .88) →
compressor → delayEnableRamp → soma freshInput condicionado*driveGain →
DC blocker → clamp ±4 → magnetização/LUT/bias/remanência → gravação no buffer
compartilhado se !freeze. Fontes: `tape_core.cpp:1106–1269`, `:417–432`.

Warp contorna coloração C e eletrônica normal: warpFb*warpFbGain → compressor
→ ramp; mantém sua gravação/bias condicional. Delay inativo retorna feedback0.
Freeze suspende escrita, não é novo filtro. Monitor tem textura/ganhos e seus
próprios filtros; não é a fonte C. Pre/de-emphasis são **escalares**, não EQ.
A condição allowBias depende de Warp/Reverse/freshInput. Limiter/auxiliares de
saída não são filtros de feedback. Não compartilhar os estados privados C
com filtros de monitoração.

## Filtros isolados: resposta efetiva

HPF real Q=.5: em 300 Hz é **−6.02 dB**, não −3 dB. Resposta medida por FFT de
impulso float32 processado pelo próprio filtro (1s, resolução1Hz):

| Hz | HPF dB | Todos os filtros C, speed50/age50/bump50/tone50 dB |
|---:|---:|---:|
| 40 | −35.15 | −34.65 |
| 80 | −23.56 | −21.32 |
| 100 | −20.00 | −16.72 |
| 130 | −16.02 | −12.03 |
| 150 | −13.98 | −10.22 |
| 200 | −10.24 | −7.75 |
| 300 | −6.02 | −4.94 |
| 1000 | −0.75 | −0.76 |
| 4000 | −0.05 | −2.23 |
| 8000 | −0.01 | −12.01 |
| 12000 | ~0 | −25.75 |

Bump: centro60+140*speedNorm Hz (60/130/200 nos três pontos), feedback Q=.8,
ganho clamp(headBumpAmount*.02,0,2) dB; playback/C privado Q=.9,
ganho clamp(amount*.06,0,6) dB. Em amount50 são +1 e +3 dB. Probe mede pico
aproximadamente130Hz e ganhos +1/+3dB no centro. O bump **acrescenta** corpo
relativo ao sinal sem bump, mas não desfaz o HPF: combinação em130Hz ainda
~−12dB. Não se pode concluir audibilidade/qualidade apenas pela curva.

Gap eletrônico one-pole: cutoff=(9000+7000*speed)*(1−.65*age), mínimo1600;
faixa efetiva dessas entradas3150–16000Hz, 8437.5Hz no ponto médio. One-pole
usa exp(-2πfc/fs): o número configurado não é necessariamente o −3dB digital.
Playback/C: gap biquad Q=.707 e shelf −12dB, cut=(6000+10000*speed)*ageFactor,
com tone interagindo em ageFactor e mínimo400Hz. No probe tone50:
cut600/6050/16000Hz em slow+worn/médio/fast+new. Shelf em2*cut é limitado pela
função canônica boundedBiquadFrequency (especialmente nos ajustes rápidos).
Não usar uma fórmula analógica como substituto dessa resposta digital.

Perda HF varia muito: em8kHz, toda cadeia medida slow/new ~−11.95dB,
fast/new ~−0.77dB, slow/worn ~−67.06dB, médio ~−12.01dB por passagem.
Consequentemente não há um único decay espectral “da C0”. A idade extrema
pode apagar também médios: slow/worn ~−15.51dB em1kHz por passagem.
O JSON inclui9 configurações ×7 estágios; cauda dos filtros isolados em
últimos100ms próxima a zero, sem ringing persistente observado nesse probe.

`cumulative_filter_only_db` é **projeção LTI** n×dB da resposta medida, para
1/2/4/8 passagens de filtros fixos. Não é medição de loop não linear. P.ex.
130Hz −12dB/passagem prevê −48dB em4 passagens só dos filtros; saturação,
compressão, magnetização, modulação e ruído alteram o loop real.

## Loop completo: repetições medidas

Receita adicional separada da baseline:12s/44100/128, delay500ms, um cabeçote
ativo4, hiss/dropout-depth/azimute/wow/flutter0, speed50/age50/bump50, tone50,
input impulso estéreo0.1 ou burst200Hz/100ms. Mantém comportamento residual
canônico, textura/monitor/magnetização; não é simulação LTI sem DSP.
Janelas: n*.5−.1 até n*.5+.4s, n1–20, sem normalizar. Picos principais
confirmados ~.50005/1.00009/1.50016s; textura inicial ~4.58ms fica fora.
São **janelas por repetição**, não separação matemática de ecos sobrepostos.

No impulso feedback85, mudança de energia da janela1 para2:

| Banda | Delta dB |
|---|---:|
| 40–80Hz | −25.44 |
| 80–150Hz | −13.77 |
| 150–300Hz | −6.99 |
| 300Hz–1kHz | −2.70 |
| 1–4kHz | −2.04 |
| >4kHz | −5.79 |

RMS das janelas1/2/8:0.0004139/0.0002886/0.0000682 (fb85).
Fb100:0.0004195/0.0003055/0.0000928; decay mais lento, não crescimento
monótono nem acúmulo grave nesta receita. Limite de feedback aplicado pelo core
continua .88;100 é controle público, não ganho unitário. Ausência de crescimento
nesse impulso não prova comportamento de toda entrada ou preset.

Todas as8 cenas finitas; picos globais impulso~.0381, burst~.1600. A estimativa
de harmônicos2–10/fundamental do burst200Hz fb85 nas primeiras4 janelas é
~0.13/0.16/0.29/0.54%. É razão medida por projeção coerente em50ms centrais,
contém ruído residual/modulação e não é THD laboratorial de estado estacionário
nem atribuição exclusiva aos saturadores. JSON preserva a razão por repetição.
Bandas usam FFT retangular, energia one-sided somada L/R/Parseval, com limites
inferiores inclusivos/superiores exclusivos. Espectros pontuais em dB são
magnitudes FFT não normalizadas, comparáveis entre janelas iguais; para
transferência entre filtros usar a seção isolada, não esses níveis absolutos.

Seis tails60s adicionais a44.1/48kHz de feedback95, Multi-Head75 e Reverse75
passam regras existentes: finitude, delta último segundo≤.05dB, peak tardio≤1.02
vez intermediário, nenhum sample no teto. `feedback-stability.json` registra
classificação decayed to silence e slope~0 em cauda já no piso, sem confundir
isso com sustain audível. Não houve nova instrumentação de estados internos.

## Oportunidades musicais

1. Há espaço mensurável para corpo80–300Hz: HPF domina sobre o boost do bump.
   Afeta fundamentais de guitarra grave/baixo/synth, mas pode ser a clareza
   musical desejada. Só audição decidirá se preservar mais é melhor.
2. C possui **duas** perdas de gap e dois bumps no loop; calibrar só o one-pole
   pode não controlar a cauda HF como esperado. Preservar primeira repetição e
   alterar a perda recirculada é hipótese útil para F2-C.
3. Ganho de feedback alto sustenta médios mais que graves nas receitas medidas.
   Não mudar ganho/saturação nesta F2 para compensar indevidamente esse efeito;
   registrar RAW e MATCHED para separar volume e timbre.
