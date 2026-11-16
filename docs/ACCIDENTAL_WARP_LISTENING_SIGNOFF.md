# Escuta A/B — Accidental Warp V1.3

- **Status** `APPROVED WITH RESERVATIONS`
- **Data da preparação** 2026-11-15
- **Commit de DSP** `d942a7b26523e5a50193e0fb9206093f070cc130`
- **Correção de saída no firmware** `b9fbbd342e3ead2054ca02c49705e385ba4fd985`
- **Comparador do Performance Rescue** `f8d2851c663d62ae4850087dc5bb255bd8b818be`

## Arquivos comparados

Os renders locais ficam em `artifacts/release-gate/listening/`, com `manifest.json`, uma pasta por cenário e WAVs PCM24 estéreo a 48 kHz. Use `before.wav` e `head.wav` para comparar os commits e `strict.wav` e `fast.wav` para comparar as flags no mesmo TapeCore Native. As cópias `*_matched.wav` foram atenuadas para o menor RMS do cenário. Os originais preservam o nível de render. A entrada float32 e os eventos de cada cenário são comuns a todos os builds.

O pacote cobre Tape Clean, High Drive, High Feedback, Ping-Pong, Multi-Head, Warp Musical, Raw Integer, Stereo, Feedback Cascade, Automation, Reverse atual e Silence/Decay. Inclui também Drive Ramp, Wow/Flutter e um teste de 60 s com ruído, dropout, modulação e feedback alto. Os [artefatos da CI](https://github.com/ovelhaaa/hydra-tape-engine/actions/runs/43776656015#artifacts) incluem os pacotes complementares `accidental-warp-ab` e `listening-review`.

## Diferenças objetivas e pontos para escuta

| Trecho | Evidência | Escutar |
|---|---|---|
| Clean, High Drive e Drive Ramp | Maior pico da diferença antes/depois da rampa: `0,02914`, frame 255896 (~5,33 s), RMSE `7,68e-5`. | Transiente próximo a 5,33 s, textura e brilho conforme sobe o Drive. |
| High Feedback, Ping-Pong e Multi-Head | Ping-Pong antes/depois: RMSE `1,029e-4`, correlação `0,99999983`. | Alternância L/R, repetição tardia e imagem dos cabeçotes. |
| Warp Musical, Raw Integer, Stereo, Cascade e Automation | Maior RMSE antes/depois neste pacote Warp: `1,62e-6` na automação. | Ritmo granular, imagem L/R, Grain/Speed e decaimento da cascata. |
| Reverse e Silence/Decay | Reverse é um comparativo do modo atual. Silence/Decay a 95% exercita a cauda do Tape tradicional. | Posição e nível dos marcadores, persistência da cauda e ruído residual. |
| Simulação do estágio pós-TapeCore | `head_post_tanh` reaplica a curva anterior ao mesmo sinal. A queda RMS em High Drive foi `1,216 dB` antes da compensação. | Ataque, compressão e harmônicos em High Drive e High Feedback. |
| Drive alto + Freeverb, volume máximo | `freeverb_drive_pcm_55hz/` contém o PCM de saída e o controle com `tanhf` a 44,1 kHz, ambos com a conversão corrigida. As cópias `*_matched.wav` da senoide extrema ficam em RMS 0,16. | Use as cópias compensadas para avaliar a diferença de harmônicos quando o Freeverb leva o sinal até o limite PCM. |
| Teste longo Native com flags | RMSE `0,1233` e correlação `0,9448` entre `strict` e `fast`. | Comparar a evolução da cauda e do dropout com os níveis ajustados. |

## Avaliação do revisor

- **Data da escuta** 2026-11-16.
- **Pacote avaliado** Release gate · TapeCore · `8f1c674365bf7d2485d3bb0fcb92c7021b3720fe54a4d9748659c9a5c5c3db7e`. O identificador coincide com o pacote local.
- **Commit no relatório exportado** Indisponível. Os commits de preparação permanecem registrados acima.
- **Cobertura** 16 cenários e 35 comparações do pacote. 34 decisões `APPROVE` e uma `APPROVE WITH NOTES`. Nenhuma pendência de escuta dentro desse pacote.

A ressalva foi **Tape Clean · Strict × Fast**. O revisor preferiu Strict no teste cego e ouviu um clique repetido, bem baixo, por volta de 6 s em Fast. Em **Warp Feedback Cascade · Baseline × Optimized**, a preferência X permanece no registro original. A observação do revisor esclarece que a seleção foi acidental e que ambos soaram equivalentes.

O parecer cobre os WAVs identificados acima. A decisão de merge sobre a exceção de equivalência Native/WASM segue pendente.

## Investigação técnica de encerramento — 2026-11-16

O Hydra Listening Lab recalculou o identificador `8f1c674365bf7d2485d3bb0fcb92c7021b3720fe54a4d9748659c9a5c5c3db7e` com as 35 associações A/B em 16 cenários. O [manifesto complementar](../tools/listening-lab/provenance/release-gate-tapecore.json) registra os SHA-256 dos 66 renders distintos e os vincula à mesma identidade. Os quatro runners Native coincidiram com `listening/build-sha256.txt`. A referência de firmware do controle PCM é `b9fbbd3`. A revisão exata do motor que produziu esse WAV e a versão e o horário do gerador histórico ficaram indisponíveis.

### Tape Clean · clique entre 5 e 7 s

Foram lidos `tape_clean/strict.wav`, `tape_clean/fast.wav` e `tape_clean/input.f32` no nível original, a 48 kHz. Os WAVs foram decodificados em float64 e comparados por canal, frame a frame. Strict e Fast tiveram RMS de 0,0267744 e 0,0267721 e correlação de 0,99999633. O maior passo em ambos foi 0,0263213, a 5,022333 s. O resíduo Fast − Strict teve RMS 7,253e-5 e pico de **0,0119662 FS (−38,4 dBFS)** à esquerda no frame 300287, **6,255979 s**. O maior passo do resíduo foi 0,0084475 a 6,752104 s.

Os maiores transientes do resíduo na janela ocorreram a 5,860188, 6,256021, 6,356271, 6,656042 e 6,752104 s (instantes da derivada). Os intervalos variam e os frames têm restos 73, 1, 77, 2 e 5 módulo 128. O estímulo é comum e a automação está vazia. A presença dos transientes nas amostras exclui a alternância do player como causa necessária.

### Ruído agudo e Multi-Head

Welch com janela Hann de 8192 frames e média dos canais encontrou linhas estáveis de **6 kHz e 18 kHz** em Strict e Fast, com −38,3 e −48,9 dBFS por bin em Tape Clean (5–7 s). A banda a partir de 4 kHz reuniu 36% da energia em ambos, contra menos de 0,00001% na entrada. A linha de 6 kHz dominou essa banda em todas as janelas de 1 s. O bias triangular de 18 kHz tem período de oito amostras a 48 kHz. Seu terceiro harmônico dobra para 6 kHz nessa taxa. Em uma sonda separada com `biasGate = 0`, as linhas caíram para −131,2 e −148,8 dBFS por bin, respectivamente. O ruído agudo é coloração do bias, presente nos dois renders. Wow, flutter, hiss e severidade de dropout têm valor zero no roteiro.

Em `multi_head_transients/strict.wav` e `fast.wav`, também com automação vazia, os RMS foram 0,0357142 e 0,0357089, a correlação 0,99999907 e a energia acima de 4 kHz cerca de 25% em ambos. As mesmas linhas aparecem a −37,1 e −48,0 dBFS por bin. O maior transiente diferencial foi **0,0215551 FS (−33,3 dBFS)** à direita em **0,125167 s**, com um passo de 0,0152425 no resíduo duas amostras depois. O controle isolou o maior pulso em Strict, consistente com a nota da escuta cega que mapeou Y para Strict. Os ataques de maior amplitude em comum, como o de 3,006750 s, aparecem nos dois renders, com passo de aproximadamente 0,03744.

### Causa e impacto

Os runners históricos Strict e Fast, com os `input.f32` e `automation.txt` de cada cenário, reproduziram os quatro WAVs PCM24 byte a byte em arquivos temporários. A sonda compilou `tests/core/listen.cpp`, `dsp/core/hydra_dsp_c_api.cpp` e uma cópia de `dsp/core/tape_core.cpp` a `-O2` com as flags Strict/Fast do manifesto complementar. Na cópia, a única alteração foi substituir `(allowBias && std::fabs(x) > 1e-7f)` por `allowBias` dentro de `TapeMagnetics::process`. A diferença máxima Strict/Fast caiu de 0,0119662 para 0,00005236 FS em Tape Clean (5–7 s), e de 0,0215551 para 0,00012571 FS em Multi-Head. A diferença Fast original × Fast com bias contínuo em Tape Clean e a diferença Strict original × Strict com bias contínuo em Multi-Head localizaram os mesmos picos.

O gate binário comuta a amplitude inteira do bias quando o sinal que entra no estágio magnético cruza o limiar. As flags mudam quais cruzamentos atingem esse limiar, e a comutação abrupta produz os pulsos. O gate já existe no comparador `f8d2851`. Os picos nas janelas investigadas variam entre builds. Em Multi-Head o pico de 0,125167 s também aparece no resíduo Head − Baseline. A evidência localiza a causa no DSP Native e deixa a ocorrência e a amplitude no ESP32-S3 em aberto.

O parecer humano continua `APPROVED WITH RESERVATIONS`. O parecer técnico é **`BLOCKED — REPRODUCIBLE AUDIO DEFECT`** pela descontinuidade audível de Tape Clean confirmada nos renders. A menor correção proposta é substituir a comutação binária por uma transição curta de ganho do bias, preservando o desligamento em silêncio e o controle `allowBias`. Forçar bias contínuo foi apenas um controle diagnóstico. Uma correção de produção requer conferir o trecho e as caudas, os regressivos Native/WASM e o orçamento do caminho crítico no ESP32-S3.

## Avaliações individuais

Registro exportado do [Hydra Listening Lab](../tools/listening-lab/README.md), com as decisões e observações do revisor.

### tape clean · Baseline × Optimized

- **Arquivos A/B** tape\_clean/before.wav / tape\_clean/head.wav
- **Decisão** APPROVE · 2026-11-16T17:31:59.042Z
- **Fidelidade / Musicalidade** 5 / 5
- **Artefatos** Leve
- **Diferenças** —
- **Monitoração** MATCHED · canal Stereo · ganhos A/B 1.0000 / 1.0000 · proteção 1.0000
- **Escuta cega** Equivalentes · confiança 5 · semente ce94768e-75d4-4e92-b19f-730ad8502778 · X = A, Y = B

> Ambos parecem ter um ruído high pitch de fundo

### tape clean · Strict × Fast

- **Arquivos A/B** tape\_clean/strict.wav / tape\_clean/fast.wav
- **Decisão** APPROVE WITH NOTES · 2026-11-16T17:34:13.532Z
- **Fidelidade / Musicalidade** 5 / 5
- **Artefatos** Leve
- **Diferenças** —
- **Monitoração** MATCHED · canal Stereo · ganhos A/B 0.9999 / 1.0000 · proteção 1.0000
- **Escuta cega** X · confiança 5 · semente b957d160-b753-46bd-95d0-3d1f82b47a66 · X = A, Y = B

> Há um pequeno artefato no Y aos 6 segundos. Algo como um clique repetido bem baixinho

### tape clean · Head × controle pós-tanh

- **Arquivos A/B** tape\_clean/head.wav / tape\_clean/head\_post\_tanh.wav
- **Decisão** APPROVE · 2026-11-16T17:35:07.811Z
- **Fidelidade / Musicalidade** 5 / 5
- **Artefatos** Nenhum
- **Diferenças** —
- **Monitoração** MATCHED · canal Stereo · ganhos A/B 0.9992 / 1.0000 · proteção 1.0000
- **Escuta cega** Equivalentes · confiança 5 · semente bc843c3f-3aea-4168-93fa-0e78800fa745 · X = B, Y = A

Sem observações.

### tape high drive · Baseline × Optimized

- **Arquivos A/B** tape\_high\_drive/before.wav / tape\_high\_drive/head.wav
- **Decisão** APPROVE · 2026-11-16T17:36:24.957Z
- **Fidelidade / Musicalidade** 5 / 5
- **Artefatos** Nenhum
- **Diferenças** —
- **Monitoração** MATCHED · canal Stereo · ganhos A/B 1.0000 / 1.0000 · proteção 1.0000
- **Escuta cega** Equivalentes · confiança 5 · semente 5df2bcd3-b473-4bd5-9c59-9b711d23df28 · X = B, Y = A

Sem observações.

### tape high drive · Strict × Fast

- **Arquivos A/B** tape\_high\_drive/strict.wav / tape\_high\_drive/fast.wav
- **Decisão** APPROVE · 2026-11-16T17:37:12.203Z
- **Fidelidade / Musicalidade** 5 / 5
- **Artefatos** Nenhum
- **Diferenças** —
- **Monitoração** MATCHED · canal Stereo · ganhos A/B 0.9999 / 1.0000 · proteção 1.0000
- **Escuta cega** Equivalentes · confiança 5 · semente 937ce3de-ad2b-43ea-bff1-2ea0777c34be · X = A, Y = B

Sem observações.

### tape high drive · Head × controle pós-tanh

- **Arquivos A/B** tape\_high\_drive/head.wav / tape\_high\_drive/head\_post\_tanh.wav
- **Decisão** APPROVE · 2026-11-16T17:38:06.573Z
- **Fidelidade / Musicalidade** 5 / 5
- **Artefatos** Nenhum
- **Diferenças** —
- **Monitoração** MATCHED · canal Stereo · ganhos A/B 0.8693 / 1.0000 · proteção 1.0000
- **Escuta cega** Equivalentes · confiança 5 · semente b4647b0d-d169-4b2a-ab63-cce82b71743f · X = B, Y = A

Sem observações.

### multi head transients · Baseline × Optimized

- **Arquivos A/B** multi\_head\_transients/before.wav / multi\_head\_transients/head.wav
- **Decisão** APPROVE · 2026-11-16T17:53:40.366Z
- **Fidelidade / Musicalidade** 5 / 5
- **Artefatos** Nenhum
- **Diferenças** —
- **Monitoração** MATCHED · canal Stereo · ganhos A/B 1.0000 / 1.0000 · proteção 1.0000
- **Escuta cega** Equivalentes · confiança 5 · semente 41b9a6c7-3f70-4fff-bf9a-98a1e6eb02c4 · X = A, Y = B

Sem observações.

### multi head transients · Strict × Fast

- **Arquivos A/B** multi\_head\_transients/strict.wav / multi\_head\_transients/fast.wav
- **Decisão** APPROVE · 2026-11-16T17:54:54.000Z
- **Fidelidade / Musicalidade** 5 / 5
- **Artefatos** Nenhum
- **Diferenças** Outros
- **Monitoração** MATCHED · canal Stereo · ganhos A/B 0.9999 / 1.0000 · proteção 1.0000
- **Escuta cega** X · confiança 5 · semente 28c5b224-3315-4e5a-a854-c00712a687d7 · X = B, Y = A

> Há alguns leves cliques no outro

### high feedback harmonic · Baseline × Optimized

- **Arquivos A/B** high\_feedback\_harmonic/before.wav / high\_feedback\_harmonic/head.wav
- **Decisão** APPROVE · 2026-11-16T17:41:44.129Z
- **Fidelidade / Musicalidade** 5 / 5
- **Artefatos** Nenhum
- **Diferenças** —
- **Monitoração** MATCHED · canal Stereo · ganhos A/B 1.0000 / 1.0000 · proteção 1.0000
- **Escuta cega** Equivalentes · confiança 5 · semente 266369a4-82de-4ac8-8291-175471ad817c · X = B, Y = A

Sem observações.

### high feedback harmonic · Strict × Fast

- **Arquivos A/B** high\_feedback\_harmonic/strict.wav / high\_feedback\_harmonic/fast.wav
- **Decisão** APPROVE · 2026-11-16T17:42:51.216Z
- **Fidelidade / Musicalidade** 5 / 5
- **Artefatos** Nenhum
- **Diferenças** —
- **Monitoração** MATCHED · canal Stereo · ganhos A/B 0.9999 / 1.0000 · proteção 1.0000
- **Escuta cega** Equivalentes · confiança 5 · semente b33cb29b-dcdd-4cfa-9f38-91cd3bf1796b · X = B, Y = A

Sem observações.

### high feedback harmonic · Head × controle pós-tanh

- **Arquivos A/B** high\_feedback\_harmonic/head.wav / high\_feedback\_harmonic/head\_post\_tanh.wav
- **Decisão** APPROVE · 2026-11-16T17:43:53.650Z
- **Fidelidade / Musicalidade** 5 / 5
- **Artefatos** Nenhum
- **Diferenças** Volume
- **Monitoração** MATCHED · canal Stereo · ganhos A/B 0.8764 / 1.0000 · proteção 1.0000
- **Escuta cega** X · confiança 5 · semente 3788c86e-ad0e-4844-aed1-71498fc05a69 · X = B, Y = A

Sem observações.

### ping pong · Baseline × Optimized

- **Arquivos A/B** ping\_pong/before.wav / ping\_pong/head.wav
- **Decisão** APPROVE · 2026-11-16T17:55:59.004Z
- **Fidelidade / Musicalidade** 5 / 5
- **Artefatos** Nenhum
- **Diferenças** —
- **Monitoração** MATCHED · canal Stereo · ganhos A/B 1.0000 / 1.0000 · proteção 1.0000
- **Escuta cega** Equivalentes · confiança 5 · semente 79a4d9e4-b1c4-4c40-97ce-05d44924d74a · X = A, Y = B

Sem observações.

### ping pong · Strict × Fast

- **Arquivos A/B** ping\_pong/strict.wav / ping\_pong/fast.wav
- **Decisão** APPROVE · 2026-11-16T17:56:27.044Z
- **Fidelidade / Musicalidade** 5 / 5
- **Artefatos** Nenhum
- **Diferenças** —
- **Monitoração** MATCHED · canal Stereo · ganhos A/B 0.9997 / 1.0000 · proteção 1.0000
- **Escuta cega** Equivalentes · confiança 5 · semente a0f239f4-616d-4ffc-83f9-10c313f5cb4e · X = A, Y = B

Sem observações.

### wow flutter excerpt · Baseline × Optimized

- **Arquivos A/B** wow\_flutter\_excerpt/before.wav / wow\_flutter\_excerpt/head.wav
- **Decisão** APPROVE · 2026-11-16T17:38:45.034Z
- **Fidelidade / Musicalidade** 5 / 5
- **Artefatos** —
- **Diferenças** —
- **Monitoração** MATCHED · canal Stereo · ganhos A/B 1.0000 / 1.0000 · proteção 1.0000
- **Escuta cega** Equivalentes · confiança 5 · semente f6c33344-4e69-4e62-8f8f-9e5f1bda7c0f · X = B, Y = A

Sem observações.

### wow flutter excerpt · Strict × Fast

- **Arquivos A/B** wow\_flutter\_excerpt/strict.wav / wow\_flutter\_excerpt/fast.wav
- **Decisão** APPROVE · 2026-11-16T17:39:50.616Z
- **Fidelidade / Musicalidade** 5 / 5
- **Artefatos** Nenhum
- **Diferenças** —
- **Monitoração** MATCHED · canal Stereo · ganhos A/B 0.9998 / 1.0000 · proteção 1.0000
- **Escuta cega** Equivalentes · confiança 5 · semente 0df2d850-2cdf-4ae4-b708-69c268beb585 · X = B, Y = A

Sem observações.

### drive ramp excerpt · Baseline × Optimized

- **Arquivos A/B** drive\_ramp\_excerpt/before.wav / drive\_ramp\_excerpt/head.wav
- **Decisão** APPROVE · 2026-11-16T17:49:37.195Z
- **Fidelidade / Musicalidade** 5 / 5
- **Artefatos** Nenhum
- **Diferenças** —
- **Monitoração** MATCHED · canal Stereo · ganhos A/B 1.0000 / 1.0000 · proteção 1.0000
- **Escuta cega** Equivalentes · confiança 5 · semente 4231450b-26bb-43ca-9d72-9e75476498af · X = B, Y = A

Sem observações.

### drive ramp excerpt · Strict × Fast

- **Arquivos A/B** drive\_ramp\_excerpt/strict.wav / drive\_ramp\_excerpt/fast.wav
- **Decisão** APPROVE · 2026-11-16T17:50:03.942Z
- **Fidelidade / Musicalidade** 5 / 5
- **Artefatos** Nenhum
- **Diferenças** —
- **Monitoração** MATCHED · canal Stereo · ganhos A/B 0.9999 / 1.0000 · proteção 1.0000
- **Escuta cega** Equivalentes · confiança 5 · semente 699f9a67-2942-4e46-8c20-0e7865036379 · X = A, Y = B

Sem observações.

### drive ramp excerpt · Head × controle pós-tanh

- **Arquivos A/B** drive\_ramp\_excerpt/head.wav / drive\_ramp\_excerpt/head\_post\_tanh.wav
- **Decisão** APPROVE · 2026-11-16T17:51:09.409Z
- **Fidelidade / Musicalidade** 5 / 5
- **Artefatos** Nenhum
- **Diferenças** —
- **Monitoração** MATCHED · canal Stereo · ganhos A/B 0.9391 / 1.0000 · proteção 1.0000
- **Escuta cega** Equivalentes · confiança 5 · semente eae8ea2a-66ff-4a23-92de-ae010ac63a7e · X = B, Y = A

Sem observações.

### reverse stereo · Baseline × Optimized

- **Arquivos A/B** reverse\_stereo/before.wav / reverse\_stereo/head.wav
- **Decisão** APPROVE · 2026-11-16T17:57:09.032Z
- **Fidelidade / Musicalidade** 5 / 5
- **Artefatos** Nenhum
- **Diferenças** —
- **Monitoração** MATCHED · canal Stereo · ganhos A/B 1.0000 / 1.0000 · proteção 1.0000
- **Escuta cega** Equivalentes · confiança 5 · semente 437cbcf1-7522-41bd-af5f-48b46ce27f3e · X = B, Y = A

Sem observações.

### reverse stereo · Strict × Fast

- **Arquivos A/B** reverse\_stereo/strict.wav / reverse\_stereo/fast.wav
- **Decisão** APPROVE · 2026-11-16T17:57:35.512Z
- **Fidelidade / Musicalidade** 5 / 5
- **Artefatos** Nenhum
- **Diferenças** —
- **Monitoração** MATCHED · canal Stereo · ganhos A/B 1.0000 / 0.9980 · proteção 1.0000
- **Escuta cega** Equivalentes · confiança 5 · semente eef81330-f44a-4a14-80ba-d6d3190f4a62 · X = B, Y = A

Sem observações.

### warp musical · Baseline × Optimized

- **Arquivos A/B** warp\_musical/before.wav / warp\_musical/head.wav
- **Decisão** APPROVE · 2026-11-16T17:27:58.063Z
- **Fidelidade / Musicalidade** 5 / 5
- **Artefatos** Nenhum
- **Diferenças** —
- **Monitoração** MATCHED · canal Stereo · ganhos A/B 1.0000 / 1.0000 · proteção 1.0000
- **Escuta cega** Equivalentes · confiança 5 · semente 20b763b6-d239-46c0-9fe3-df39ccabaf93 · X = B, Y = A

Sem observações.

### warp musical · Strict × Fast

- **Arquivos A/B** warp\_musical/strict.wav / warp\_musical/fast.wav
- **Decisão** APPROVE · 2026-11-16T17:29:51.822Z
- **Fidelidade / Musicalidade** 5 / 5
- **Artefatos** Nenhum
- **Diferenças** —
- **Monitoração** MATCHED · canal Stereo · ganhos A/B 0.9997 / 1.0000 · proteção 1.0000
- **Escuta cega** Equivalentes · confiança 5 · semente 9096b083-086e-4d62-a88d-f00a0c183833 · X = B, Y = A

Sem observações.

### warp raw integer · Baseline × Optimized

- **Arquivos A/B** warp\_raw\_integer/before.wav / warp\_raw\_integer/head.wav
- **Decisão** APPROVE · 2026-11-16T17:28:48.377Z
- **Fidelidade / Musicalidade** 5 / 5
- **Artefatos** Nenhum
- **Diferenças** —
- **Monitoração** MATCHED · canal Stereo · ganhos A/B 1.0000 / 1.0000 · proteção 1.0000
- **Escuta cega** Equivalentes · confiança 5 · semente 8c80e86c-db8b-42d6-af0d-fac03cc253ca · X = A, Y = B

Sem observações.

### warp raw integer · Strict × Fast

- **Arquivos A/B** warp\_raw\_integer/strict.wav / warp\_raw\_integer/fast.wav
- **Decisão** APPROVE · 2026-11-16T17:30:46.094Z
- **Fidelidade / Musicalidade** 5 / 5
- **Artefatos** Nenhum
- **Diferenças** —
- **Monitoração** MATCHED · canal Stereo · ganhos A/B 0.9997 / 1.0000 · proteção 1.0000
- **Escuta cega** Equivalentes · confiança 5 · semente edfcf016-93dd-41c7-b454-d742bc5a43d8 · X = A, Y = B

Sem observações.

### warp stereo · Baseline × Optimized

- **Arquivos A/B** warp\_stereo/before.wav / warp\_stereo/head.wav
- **Decisão** APPROVE · 2026-11-16T17:58:18.995Z
- **Fidelidade / Musicalidade** 5 / 5
- **Artefatos** Nenhum
- **Diferenças** —
- **Monitoração** MATCHED · canal Stereo · ganhos A/B 1.0000 / 1.0000 · proteção 1.0000
- **Escuta cega** Equivalentes · confiança 5 · semente 8cbed680-e3fc-47dc-b684-4efce95709ba · X = B, Y = A

Sem observações.

### warp stereo · Strict × Fast

- **Arquivos A/B** warp\_stereo/strict.wav / warp\_stereo/fast.wav
- **Decisão** APPROVE · 2026-11-16T17:59:03.511Z
- **Fidelidade / Musicalidade** 5 / 5
- **Artefatos** Nenhum
- **Diferenças** —
- **Monitoração** MATCHED · canal Stereo · ganhos A/B 0.9998 / 1.0000 · proteção 1.0000
- **Escuta cega** Equivalentes · confiança 5 · semente ae1a5744-54f6-40a7-aa89-cea3f50aa172 · X = A, Y = B

Sem observações.

### warp feedback cascade · Baseline × Optimized

- **Arquivos A/B** warp\_feedback\_cascade/before.wav / warp\_feedback\_cascade/head.wav
- **Decisão** APPROVE · 2026-11-16T17:45:06.175Z
- **Fidelidade / Musicalidade** 5 / 5
- **Artefatos** Nenhum
- **Diferenças** —
- **Monitoração** MATCHED · canal Stereo · ganhos A/B 1.0000 / 1.0000 · proteção 1.0000
- **Escuta cega** X · confiança 5 · semente e1b09336-c88f-4560-8c46-236626bed1c0 · X = A, Y = B

> Cliquei equivocadamente. Ambos soam equivalentes.

### warp feedback cascade · Strict × Fast

- **Arquivos A/B** warp\_feedback\_cascade/strict.wav / warp\_feedback\_cascade/fast.wav
- **Decisão** APPROVE · 2026-11-16T17:45:36.880Z
- **Fidelidade / Musicalidade** 5 / 5
- **Artefatos** Nenhum
- **Diferenças** —
- **Monitoração** MATCHED · canal Stereo · ganhos A/B 1.0000 / 0.9992 · proteção 1.0000
- **Escuta cega** Equivalentes · confiança 5 · semente c4f3e4a6-52bd-452c-ae35-cfc683abcd8c · X = A, Y = B

Sem observações.

### warp automation · Baseline × Optimized

- **Arquivos A/B** warp\_automation/before.wav / warp\_automation/head.wav
- **Decisão** APPROVE · 2026-11-16T17:52:13.687Z
- **Fidelidade / Musicalidade** 5 / 5
- **Artefatos** Nenhum
- **Diferenças** —
- **Monitoração** MATCHED · canal Stereo · ganhos A/B 1.0000 / 1.0000 · proteção 1.0000
- **Escuta cega** Equivalentes · confiança 5 · semente f74d8f6a-1bd9-4bf4-a92d-1cd0df8b9030 · X = A, Y = B

Sem observações.

### warp automation · Strict × Fast

- **Arquivos A/B** warp\_automation/strict.wav / warp\_automation/fast.wav
- **Decisão** APPROVE · 2026-11-16T17:53:07.685Z
- **Fidelidade / Musicalidade** 5 / 5
- **Artefatos** Nenhum
- **Diferenças** —
- **Monitoração** MATCHED · canal Stereo · ganhos A/B 0.9997 / 1.0000 · proteção 1.0000
- **Escuta cega** Equivalentes · confiança 5 · semente 0a0be12f-8f0b-460b-89b4-f0fd935bd4cc · X = A, Y = B

Sem observações.

### silence decay · Baseline × Optimized

- **Arquivos A/B** silence\_decay/before.wav / silence\_decay/head.wav
- **Decisão** APPROVE · 2026-11-16T17:46:19.850Z
- **Fidelidade / Musicalidade** 5 / 5
- **Artefatos** Nenhum
- **Diferenças** —
- **Monitoração** MATCHED · canal Stereo · ganhos A/B 1.0000 / 1.0000 · proteção 1.0000
- **Escuta cega** Equivalentes · confiança 5 · semente 30c0a04a-baf1-4db5-aee8-0c5325da3bba · X = B, Y = A

Sem observações.

### silence decay · Strict × Fast

- **Arquivos A/B** silence\_decay/strict.wav / silence\_decay/fast.wav
- **Decisão** APPROVE · 2026-11-16T17:46:52.093Z
- **Fidelidade / Musicalidade** 5 / 5
- **Artefatos** Nenhum
- **Diferenças** —
- **Monitoração** MATCHED · canal Stereo · ganhos A/B 1.0000 / 1.0000 · proteção 1.0000
- **Escuta cega** Equivalentes · confiança 5 · semente bdd5ce84-302e-49e9-81c5-81c85e077d28 · X = B, Y = A

Sem observações.

### noise dropout long · Baseline × Optimized

- **Arquivos A/B** noise\_dropout\_long/before.wav / noise\_dropout\_long/head.wav
- **Decisão** APPROVE · 2026-11-16T17:47:56.704Z
- **Fidelidade / Musicalidade** 5 / 5
- **Artefatos** Nenhum
- **Diferenças** —
- **Monitoração** MATCHED · canal Stereo · ganhos A/B 1.0000 / 1.0000 · proteção 1.0000
- **Escuta cega** Equivalentes · confiança 5 · semente 8fdc3965-6340-428d-a4d4-c1d5647c8dad · X = B, Y = A

Sem observações.

### noise dropout long · Strict × Fast

- **Arquivos A/B** noise\_dropout\_long/strict.wav / noise\_dropout\_long/fast.wav
- **Decisão** APPROVE · 2026-11-16T17:48:49.208Z
- **Fidelidade / Musicalidade** 5 / 5
- **Artefatos** Nenhum
- **Diferenças** —
- **Monitoração** MATCHED · canal Stereo · ganhos A/B 1.0000 / 0.9976 · proteção 1.0000
- **Escuta cega** Equivalentes · confiança 5 · semente eb286d91-ae54-4a6e-aad3-984b5ab0ff7b · X = A, Y = B

Sem observações.

### freeverb drive pcm 55hz · PCM atual × controle pós-tanh

- **Arquivos A/B** freeverb\_drive\_pcm\_55hz/current.wav / freeverb\_drive\_pcm\_55hz/post\_tanh\_control.wav
- **Decisão** APPROVE · 2026-11-16T17:40:53.066Z
- **Fidelidade / Musicalidade** 5 / 5
- **Artefatos** Nenhum
- **Diferenças** —
- **Monitoração** MATCHED · canal Stereo · ganhos A/B 0.7952 / 1.0000 · proteção 0.9798
- **Escuta cega** Y · confiança 5 · semente 0da1e751-747a-406c-8a01-2d79415dc515 · X = A, Y = B

Sem observações.
