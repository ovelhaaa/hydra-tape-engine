# Freeze behavior — F4

## O que o Freeze faz hoje

O caminho de escrita é:

```cpp
if (!freeze)
    buffer[writeHead] = mag.process(recSig, allowBias);   // tape_core.cpp:1379
```

`writeHead` continua avançando, o buffer compartilhado não é limpo e as
leituras continuam. Portanto o Freeze é um **gate de gravação**, não um loop
estático de uma janela: as cabeças de leitura percorrem o histórico congelado
e o material antigo reaparece. Essa é a semântica existente e a F4 **não a
altera**.

`freezeFade` existia como estado atualizado a cada sample
(`tape_core.cpp:1457–1463`, no build F4=0) e **não era consultado por nenhum
caminho de áudio**. A investigação pedida pela fase confirmou o fato: não é um
fade inacabado, é estado morto.

## Três opções avaliadas

1. **Suavizar somente a contribuição audível.** Rejeitada: o monitor não muda
   no instante do Freeze; a saída já é contínua. Um fade de saída tornaria o
   Freeze inaudível ou abriria um buraco, sem corrigir a borda real.
2. **Suavizar a entrada/saída do feedback associado.** Não se aplica ao
   Classic C: o feedback é escrito no buffer, e a borda aparece no arrivo do
   eco, não no monitor. Suavizar o ganho de feedback mudaria a cauda e a
   semântica.
3. **Modificar a política de escrita somente no release.** Escolhida e
   limitada: durante o Freeze continua **zero escrita** (nenhum sample do
   buffer congelado é sobrescrito com silêncio ou rampa); ao liberar, a
   amplitude de `recSig` entra por uma rampa de 12 ms. É a menor intervenção
   que atua exatamente onde a descontinuidade nasce.

A rampa é aplicada em `writeCh` (`:1379–1384`), depois do DC blocker e do
clamp, sobre o sinal já somado (entrada + feedback). A saturação magnética, a
LUT, o bias, o compressor e a política de não gravar durante o Freeze não
mudam.

## Por que 12 ms

O release escreve o primeiro sample não congelado no buffer; esse sample chega
ao monitor um `delayTime` depois. Uma rampa de 12 ms cobre a borda espectral
do conteúdo novo sem alongar o gesto. Medições com 10 ms e 12 ms foram
equivalentes; 12 ms foi mantido por dar uma borda um pouco mais suave em
material percussivo. A rampa é linear e por sample, independente do tamanho do
bloco.

## Medições

Entrada (Freeze ON) em `clean_guitar`, `poly_synth` e `driven_guitar`:

- passo de borda e HF **idênticos** entre F4=0 e F4=1;
- diferença vs controle negativo ~0 dB. **Não há clique de entrada
  demonstrável**; o corte de escrita não altera os samples lidos.

Saída (Freeze OFF), no arrivo do eco (delay 300 ms):

| Caso | Passo 1 ms F4=0 → F4=1 | HF vs controle F4=0 → F4=1 |
|---|---:|---:|
| fb 70, clean guitar | 0,02191 → 0,02117 | **+5,37 → −18,89 dB** |
| fb 95, clean guitar | 0,02116 → 0,02068 | **+5,98 → −18,45 dB** |
| fb 70, release rápido 2 | 0,00677 → 0,00234 | −23,25 → −60,26 dB |

O passo de 1 ms cai pouco porque o material musical já tem inclinação própria;
o indicador decisivo é o HF acrescentado acima do controle: a rampa elimina o
“splash” de alta frequência que o release instantâneo produzia. O pico local
permanece abaixo do máximo natural dos 50 ms anteriores em todos os casos.

Freeze com Reverse e Multi-Head: F4=0 = F4=1 no instante do ON; no release o
arrivo do eco ocorre dentro da janela reversa e não foi possível isolar um
ganho mensurável (passo idêntico). A rampa é aplicada ao caminho de escrita
comum, portanto não quebra a janela compartilhada, mas o benefício auditivo
nesse modo fica **não demonstrado** e deve ser julgado na escuta.

## Limitações

- A semântica de “gate de gravação com leitura avançando” não foi redesenhada.
  Transformar o Freeze em loop estático exigiria uma política de leitura nova,
  buffer/estado adicional e decisão musical; isso está fora da F4 e fica
  registrado como trabalho estrutural futuro.
- Não há fade de entrada porque não há artefato de entrada; adicionar um
  produziria buraco na memória circular, exatamente o que a fase proíbe.
- A rampa de release altera o conteúdo dos primeiros 12 ms gravados. Isso é
  intencional e mensurável; não há perda do conteúdo congelado, pois ele não é
  escrito nesse intervalo.
