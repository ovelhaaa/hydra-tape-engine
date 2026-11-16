# Auditoria do Reverse V1

A PR #47 foi mesclada em `20f6569`. A issue [#48](https://github.com/ovelhaaa/hydra-tape-engine/issues/48) acompanha a divergência numérica Native/WASM observada antes do Reverse V2.

| Elemento | Comportamento na main |
| --- | --- |
| Memória e escrita | Um anel float por canal, com guardas espelhadas para Hermite. Um cursor estéreo grava depois das leituras e avança mesmo em Freeze. |
| Leitura e direção | A posição é writeHead − delay + contador. Ela avança no histórico em regime estável e salta para trás quando o contador reinicia. O contador é incrementado separadamente para L e R. |
| Interpolação e bordas | Polinômio cúbico próprio de quatro pontos. O índice vem de truncamento e a fração usa floor, divergindo em posições negativas não inteiras. |
| Janela e envelope | Reinício com comprimento igual ao delay corrente em amostras. Variação superior a 1.000 amostras frente ao tamanho memorizado zera o contador. Sem envelope entre janelas. |
| Relógio e controles | Tempo livre suavizado ou semínima quando Heads Musical está ativo. Wow/flutter desloca o tap e afeta a fronteira. A UI e os presets registram Reverse, Smear, tempo e BPM. |
| Feedback | Saída combinada de textura e delay, após dropout/azimute e com hiss. Ping-Pong cruza os canais antes de HPF, head bump, gap loss, allpass, saturação e compressor. A gravação passa por bloqueio DC e magnetismo. |
| Saída | EQ de reprodução e limitador. Reverse Smear aplica quatro allpasses estéreo após a mistura, fora do loop. Warp tem precedência e Reverse substitui os taps de Multi-Head. |

A colagem abrupta e a dispersão de Smear são fontes plausíveis da textura reconhecível. O salto sem envelope e o erro de fração na volta do anel podem produzir descontinuidades. O contador compartilhado desloca as fronteiras entre canais e encurta a cadência. A reversão real troca a ordem de ataque e cauda dentro do fragmento, com latência dependente da posição da nota. Modulação e feedback alteram a forma de cada repetição. A contribuição perceptual de cada mecanismo precisa de escuta.
