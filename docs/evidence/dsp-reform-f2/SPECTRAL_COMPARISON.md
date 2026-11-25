# Comparação espectral F2

Calibrações finais ensaiadas: C0=300Hz, Warm=260Hz, Vintage=280Hz.
As propostas iniciais180/240Hz falharam no gate60s fast/new/FB100 e não são
as candidatas do pacote final. Nenhum parâmetro além do HPF foi corrigido.

49 configurações ×9 estágios reais por candidata. Em speed50/age50/bump50/tone50:

|Hz|C0 dB|Warm dB|Vintage dB|
|---:|---:|---:|---:|
|80|−21.322|−19.264|−20.317|
|100|−16.724|−14.858|−15.816|
|130|−12.026|−10.383|−11.227|
|200|−7.750|−6.374|−7.073|
|1000|−.760|−.591|−.719|
|4000|−2.234|−2.223|−2.804|
|8000|−12.012|−12.010|−13.358|

Loop completo: delta de energia repetição1→2, impulso0.1, feedback85, demais
condições iguais ao ponto médio da bancada F1:

|Banda|C0 dB|Warm dB|Vintage dB|
|---|---:|---:|---:|
|40–80|−25.437|−23.210|−24.379|
|80–150|−13.771|−12.179|−13.004|
|150–300|−6.987|−5.733|−6.367|
|300–1000|−2.704|−2.322|−2.535|
|1–4k|−2.042|−1.991|−2.249|
|>4k|−5.787|−5.779|−6.576|

B recupera moderadamente o corpo; C muda menos o grave e acrescenta perda HF.
São diferenças medidas, não uma promessa de benefício audível. A variante C
pode ser redundante em slow/worn; a matriz completa conserva esses casos em
vez de intensificar a candidata. Não há vencedora nem aprovação de escuta.
Probe usa os estados reais configurados por TapeCore::updateParams, incluindo
HPF, dois bumps, gap eletrônico, gap privado C e shelf. Nenhuma equação de
filtro substitui o processamento canônico. Matriz speed/age0/50/100 completa;
bump/tone0/50/100 nos subconjuntos speed50 ou age50. Impulsos1s/44100Hz,
FFT1Hz: magnitude, fase desembrulhada, pico e residual100ms.

Loop completo separado: impulsos e bursts80/130/200Hz, feedback50/75/85/95/100,
cinco combinações speed/age,12s. Janelas500ms centradas nas repetições1–20,
espectros1/2/4/8/16. Seis bandas40–80/80–150/150–300/300–1000/1–4k/>4k,
energia absoluta/relativa, RMS/pico e harmônicos2–10 por projeção50ms.
Não são ecos matematicamente isolados: modulação, magnetização, ruído residual
e não linearidade participam. Harmônicos estimados não são THD laboratorial.
Tempo observado para−60dB é descritivo; null indica limite não atingido em12s.
Maior energia não é automaticamente melhor. Audição continua pendente.
