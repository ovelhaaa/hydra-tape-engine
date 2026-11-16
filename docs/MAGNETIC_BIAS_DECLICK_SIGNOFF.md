# Magnetic Bias De-Click — V1.3

**Escuta humana · `APPROVED` · 16/11/2026**

Abra a pasta extraída do artefato `magnetic-bias-declick-v1.3` na [Hydra Listening Lab](../tools/listening-lab/README.md). O manifesto registra os commits, parâmetros iniciais, eventos, flags, taxa de 48 kHz e SHA-256 por render. Os WAVs são estéreo PCM24 sem normalização. Selecione **MATCHED** para a avaliação cega. O ponteiro de reprodução é comum ao par A/B. O resultado da [escuta anterior](ACCIDENTAL_WARP_LISTENING_SIGNOFF.md) permanece no registro original.

## Roteiro de escuta

| Prioridade | Cenário e par | Marcador ou trecho | O que verificar |
|---|---|---|---|
| 1 | `tape_clean` · Original Fast × Corrected Fast | 6,255979 s, loop 5–7 s | O clique registrado no original. Comparar brilho e ataque. |
| 2 | `multi_head_transients` · Original Strict × Corrected Strict | 0,125167 s, loop 0,10–0,15 s | Clique no canal direito e definição dos ataques. |
| 3 | Tape Clean e Multi-Head · Corrected Strict × Corrected Fast | Mesmas janelas | Textura, coloração e qualquer clique residual. |
| 4 | Tape Clean e Multi-Head · Baseline × Corrected Fast | Mesmas janelas | Timbre anterior ao Performance Rescue e diferença de ataque. |
| 5 | `tape_high_drive`, `high_feedback_harmonic`, `wow_flutter_excerpt`, `ping_pong` · Original Fast × Corrected Fast | Ataques, repetições e cauda | Drive, brilho, saturação, movimento e imagem estéreo. |
| 6 | `warp_musical`, `warp_raw_integer`, `warp_feedback_cascade`, `warp_feedback_95` · Original Fast × Corrected Fast | Ataque, primeiros grãos e cauda | Transposição, espacialidade, repetições e término da cauda. |
| 7 | `silence_decay` · Original Fast × Corrected Fast | Comparar o primeiro segundo e a janela de 20–30 s | Nível e textura da cauda de Tape tradicional com feedback a 95%. |

Os marcadores de 6,256 s e 0,125 s são os eventos medidos nos WAVs históricos. O manifesto também marca o maior delta de cada par no pacote aberto. A reconstrução Linux do Actions usa os mesmos roteiros, com builds e hashes próprios. As medições abaixo usaram os PCM e scripts de automação originais no Windows.

## Correção e medições

O amplificador de gravação condiciona e aplica Drive à entrada nova, soma a realimentação, remove DC e limita o sinal para a gravação. No Warp, só a entrada nova pode autorizar bias. Em cada canal, a fase triangular avança mesmo com ganho zero. O bias é somado ao sinal de gravação antes da coerção, da LUT de saturação e da atualização de remanência. O ganho agora percorre uma rampa linear de aproximadamente **0,5 ms** (22 frames nominais a 44,1 kHz e 24 a 48 kHz) em direção ao estado solicitado pelo gate original. O passo é calculado na configuração. O clamp atinge zero e um exatos, sem cauda assintótica. Reset zera a fase, o ganho e o estado magnético. A limpeza de denormais do estado magnético continua no fim do bloco.

| Rampa | Tape Clean, pico Strict × Fast, 5–7 s | Multi-Head, pico Strict × Fast, 0,10–0,15 s | Warp 95%, RMS de 0,5–1,5 s |
|---|---:|---:|---:|
| Original | 0,0119662 | 0,0215551 | 2,22e-8 |
| 0,5 ms | 0,00050110 | 0,00089590 | 1,70e-9 |
| 1,0 ms | 0,00025634 | 0,00000816 | 1,80e-9 |
| 2,0 ms | 0,00013159 | 0,00022280 | 1,98e-9 |

A rampa mais curta reduziu os dois picos em aproximadamente **28 dB** e produziu o menor resíduo de cauda Warp das três. No Tape tradicional Silence/Decay, também apresentou o menor delta RMS em relação ao Fast original na janela de 20–30 s (0,00597 contra 0,01071 e 0,01604). O impulso Warp único de dez segundos a feedback 95%, Speed 0,5, Grain 80 ms ficou finito e chegou a zero numérico na janela de 1,5–10 s. O RMS original de 2,22e-8 no runner Fast é compatível com a referência de aproximadamente 2,77e-8 do ensaio anterior.

| Janela | RMSE Strict × Fast original → corrigido | Maior passo no resíduo corrigido | Correlação corrigida |
|---|---:|---:|---:|
| Tape Clean · 5–7 s | 7,25e-5 → 8,05e-6 | 0,000353 | 0,99999996 |
| Multi-Head · 0,10–0,15 s | 4,02e-4 → 1,69e-5 | 0,000634 | 0,99999768 |

O maior passo do sinal Fast passou de 0,02632121 para 0,02632106 em Tape Clean e de 0,00916112 para 0,00916109 em Multi-Head. Em Tape Clean o RMS Fast ficou em 0,02677 e o pico em 0,12824. As linhas de 6 e 18 kHz permaneceram em −38,29 e −48,85 dBFS por bin. A integral relativa de energia acima de 4 kHz permaneceu estável na janela. A correlação entre Fast original e corrigido foi maior que 0,99995 em High Drive, High Feedback, Wow/Flutter, Ping-Pong e nos modos Warp Musical e Raw Integer. A cascata Warp manteve silêncio no último segundo e apresentou pico inicial de 0,224 → 0,231 FS. Silence/Decay tradicional manteve a cauda sustentada já presente no baseline (RMS final de 0,34981 → 0,34986). O manifesto detalha nível, RMS, pico, derivada, energia em alta frequência, espectro e estéreo por par.

Os testes de de-click exercitam cruzamentos do limiar, reset, isolamento de canais, silêncio, feedback alto em Tape e Warp e processamento escalar versus blocos de 128 em 44,1 e 48 kHz. A medição de feedback Warp em 50% e 80% agora verifica a diferença em relação a 0% na janela das primeiras recirculações. A medida anterior de energia tardia caía abaixo do mínimo após a redução da energia do bias. A nova janela separa o sinal de feedback do resíduo tardio. Os limites de equivalência e os goldens foram preservados.

## ESP32-S3

O build de produção ficou em 25.904 bytes de RAM estática e 589.275 bytes de flash. O mapa mantém `TapeCore::processStereoBlock` em IRAM. A placa executou o binário com SHA-256 `7534a97a67f0601acb59c7a0e880820b2ac7da3c1d46ef826a91b0cbdf83f85b`, verificado na flash, em 240 MHz, 44,1 kHz e blocos de 128 frames.

| Cenário · 1.024 blocos | Média (µs) | P99 (µs) | Pico (µs) |
|---|---:|---:|---:|
| Tape Default | 1.715,38 | 1.821 | 1.956 |
| Warp Feedback 50% | 1.641,42 | 1.698 | 1.905 |
| Warp Feedback 95% | 1.615,70 | 1.697 | 1.888 |
| Cadeia completa | 1.783,25 | 1.838 | 1.979 |

As quatro janelas tiveram zero overruns e zero falhas de escrita I2S, com heap interno e PSRAM estáveis. O orçamento é 2.902,49 µs por bloco. O ensaio usa o gerador interno e a saída digital. A [descrição da PR](https://github.com/ovelhaaa/hydra-tape-engine/pull/47) registra a execução sustentada.

## Decisão do revisor

| Campo | Registro |
|---|---|
| SHA-256 do pacote aberto | `f91f2f74a708cb1b715e065352286461a2cc7ede88d2262fa19c317ec34abd89` |
| Revisão corrigida avaliada | `04c89cc85b6e2175216d9c976a85ce360f0dc7e9` |
| Tape Clean e Multi-Head | Preferência cega pela correção em Tape Clean Fast, Tape Clean Strict e Multi-Head Strict, com confiança 5/5. Os pares Strict × Fast corrigidos foram julgados equivalentes nos dois cenários. |
| Drive, estéreo, Warp e Silence/Decay | Todos os pares receberam `APPROVE`. |
| Decisão | `APPROVED`, 17 de 17 pares em 11 cenários. |

O [relatório exportado da Hydra Listening Lab](MAGNETIC_BIAS_DECLICK_LISTENING_REPORT.md) contém as decisões individuais e o inventário verificado. O [gate de release](ACCIDENTAL_WARP_RELEASE_GATE.md#exceção-nativewasm) registra a decisão sobre Native/WASM.
