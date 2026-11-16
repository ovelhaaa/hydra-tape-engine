# Escuta A/B — Accidental Warp V1.3

- **Status** `APPROVED`
- **PR** [#47](https://github.com/ovelhaaa/hydra-tape-engine/pull/47)
- **Identificador do pacote de escuta** Magnetic Bias De-Click — V1.3 · `f91f2f74a708cb1b715e065352286461a2cc7ede88d2262fa19c317ec34abd89`
- **Commit baseline** f8d2851c663d62ae4850087dc5bb255bd8b818be
- **Commit otimizado** 04c89cc85b6e2175216d9c976a85ce360f0dc7e9
- **Commit do motor de renderização** 3bbcd720a2302cdd8b38d5d1e690d8ee521a9efb, 04c89cc85b6e2175216d9c976a85ce360f0dc7e9, f8d2851c663d62ae4850087dc5bb255bd8b818be
- **SHA-256 do inventário de renders** 653b744237966a665739cb0dbd3496471cc9f98493ee022442f6122333b870bc
- **Verificação dos WAVs A/B** Completa

## Cenários pendentes

Nenhum.

## Avaliações e problemas encontrados

### tape clean · Original Fast × Corrected Fast

- **Arquivos A/B** tape\_clean/original\_fast.wav / tape\_clean/corrected\_fast.wav
- **Procedência A** TapeCore Native · original\_fast · 3bbcd720a2302cdd8b38d5d1e690d8ee521a9efb · Release · -O2 -ffast-math -fno-finite-math-only -DNDEBUG -std=c++17 · SHA-256 `53f033a528068cbe4d7a35a3a60eda7980cc78def2f50c3f6eb90127da868ac1`
- **Procedência B** TapeCore Native · corrected\_fast · 04c89cc85b6e2175216d9c976a85ce360f0dc7e9 · Release · -O2 -ffast-math -fno-finite-math-only -DNDEBUG -std=c++17 · SHA-256 `6332fa9a6652b2de3839941b50d3950d426c50447b673e6c43671f7e2ab0b9f1`
- **Decisão** APPROVE · 2026-11-16T19:50:30.926Z
- **Fidelidade / Musicalidade** — / —
- **Artefatos** —
- **Diferenças** —
- **Monitoração** MATCHED · canal Stereo · ganhos A/B 1.0000 / 1.0000 · proteção 1.0000
- **Escuta cega** Y · confiança 5 · semente f27b36b5-a7ec-4b55-a2a6-819ec2d17154 · X = A, Y = B

Sem observações.

### tape clean · Original Strict × Corrected Strict

- **Arquivos A/B** tape\_clean/original\_strict.wav / tape\_clean/corrected\_strict.wav
- **Procedência A** TapeCore Native · original\_strict · 3bbcd720a2302cdd8b38d5d1e690d8ee521a9efb · Release · -O2 -fno-fast-math -DNDEBUG -std=c++17 · SHA-256 `3ae40bb824a39a796713bb4135d9962d4e2da47682f6905845b7080d7b0f7481`
- **Procedência B** TapeCore Native · corrected\_strict · 04c89cc85b6e2175216d9c976a85ce360f0dc7e9 · Release · -O2 -fno-fast-math -DNDEBUG -std=c++17 · SHA-256 `91e632c7a1745dcec1b746c0cc7cef2fdb768ddde5df5b72fb99ec8d687ba612`
- **Decisão** APPROVE · 2026-11-16T19:57:14.438Z
- **Fidelidade / Musicalidade** — / —
- **Artefatos** —
- **Diferenças** —
- **Monitoração** MATCHED · canal Stereo · ganhos A/B 1.0000 / 1.0000 · proteção 1.0000
- **Escuta cega** Y · confiança 5 · semente 38afed97-aa62-4b45-97ab-44b4778a2fe9 · X = A, Y = B

Sem observações.

### tape clean · Corrected Strict × Corrected Fast

- **Arquivos A/B** tape\_clean/corrected\_strict.wav / tape\_clean/corrected\_fast.wav
- **Procedência A** TapeCore Native · corrected\_strict · 04c89cc85b6e2175216d9c976a85ce360f0dc7e9 · Release · -O2 -fno-fast-math -DNDEBUG -std=c++17 · SHA-256 `91e632c7a1745dcec1b746c0cc7cef2fdb768ddde5df5b72fb99ec8d687ba612`
- **Procedência B** TapeCore Native · corrected\_fast · 04c89cc85b6e2175216d9c976a85ce360f0dc7e9 · Release · -O2 -ffast-math -fno-finite-math-only -DNDEBUG -std=c++17 · SHA-256 `6332fa9a6652b2de3839941b50d3950d426c50447b673e6c43671f7e2ab0b9f1`
- **Decisão** APPROVE · 2026-11-16T19:57:35.135Z
- **Fidelidade / Musicalidade** — / —
- **Artefatos** —
- **Diferenças** —
- **Monitoração** MATCHED · canal Stereo · ganhos A/B 0.9999 / 1.0000 · proteção 1.0000
- **Escuta cega** Equivalentes · confiança 5 · semente ac6c44eb-ccff-4a15-87d4-eb9e86c52cb7 · X = B, Y = A

Sem observações.

### tape clean · Baseline × Corrected Fast

- **Arquivos A/B** tape\_clean/baseline.wav / tape\_clean/corrected\_fast.wav
- **Procedência A** TapeCore Native · baseline · f8d2851c663d62ae4850087dc5bb255bd8b818be · Release · -O3 -DNDEBUG -std=c++17 · SHA-256 `78ab7c5b821dcb65da2c5bf59bf95ba705e422f1011a6a7479ccdcf33de7b72a`
- **Procedência B** TapeCore Native · corrected\_fast · 04c89cc85b6e2175216d9c976a85ce360f0dc7e9 · Release · -O2 -ffast-math -fno-finite-math-only -DNDEBUG -std=c++17 · SHA-256 `6332fa9a6652b2de3839941b50d3950d426c50447b673e6c43671f7e2ab0b9f1`
- **Decisão** APPROVE · 2026-11-16T19:58:06.408Z
- **Fidelidade / Musicalidade** — / —
- **Artefatos** —
- **Diferenças** —
- **Monitoração** MATCHED · canal Stereo · ganhos A/B 0.9999 / 1.0000 · proteção 1.0000
- **Escuta cega** Equivalentes · confiança 5 · semente 01ef000f-f9eb-4d7f-9538-9ee8a8ee6ff5 · X = B, Y = A

Sem observações.

### tape high drive · Original Fast × Corrected Fast

- **Arquivos A/B** tape\_high\_drive/original\_fast.wav / tape\_high\_drive/corrected\_fast.wav
- **Procedência A** TapeCore Native · original\_fast · 3bbcd720a2302cdd8b38d5d1e690d8ee521a9efb · Release · -O2 -ffast-math -fno-finite-math-only -DNDEBUG -std=c++17 · SHA-256 `68f5fd614c770b8ed112a0c5844273e43a82772a114c8b18fe7c00013905271c`
- **Procedência B** TapeCore Native · corrected\_fast · 04c89cc85b6e2175216d9c976a85ce360f0dc7e9 · Release · -O2 -ffast-math -fno-finite-math-only -DNDEBUG -std=c++17 · SHA-256 `892adee36fae46ef7bed8f05a3fae029a07581fb1c4043f43b45ace6c31e5289`
- **Decisão** APPROVE · 2026-11-16T19:51:02.142Z
- **Fidelidade / Musicalidade** — / —
- **Artefatos** —
- **Diferenças** —
- **Monitoração** MATCHED · canal Stereo · ganhos A/B 1.0000 / 1.0000 · proteção 1.0000
- **Escuta cega** Equivalentes · confiança 5 · semente de6e37e8-8977-41eb-b874-3fc0a9e54521 · X = B, Y = A

Sem observações.

### multi head transients · Original Fast × Corrected Fast

- **Arquivos A/B** multi\_head\_transients/original\_fast.wav / multi\_head\_transients/corrected\_fast.wav
- **Procedência A** TapeCore Native · original\_fast · 3bbcd720a2302cdd8b38d5d1e690d8ee521a9efb · Release · -O2 -ffast-math -fno-finite-math-only -DNDEBUG -std=c++17 · SHA-256 `2e094ec9deaa1fc852786a4e2ff69194c3f11090b0fd6b5291aa5e5fe29d58c4`
- **Procedência B** TapeCore Native · corrected\_fast · 04c89cc85b6e2175216d9c976a85ce360f0dc7e9 · Release · -O2 -ffast-math -fno-finite-math-only -DNDEBUG -std=c++17 · SHA-256 `e4a7c8cbed9251cb1583e3ad6444737f5ea052030ec631356c894712e48d596b`
- **Decisão** APPROVE · 2026-11-16T19:54:18.998Z
- **Fidelidade / Musicalidade** — / —
- **Artefatos** —
- **Diferenças** —
- **Monitoração** MATCHED · canal Stereo · ganhos A/B 1.0000 / 1.0000 · proteção 1.0000
- **Escuta cega** Equivalentes · confiança 5 · semente 412bcda8-da78-48dc-a0fb-583b4082987c · X = B, Y = A

Sem observações.

### multi head transients · Original Strict × Corrected Strict

- **Arquivos A/B** multi\_head\_transients/original\_strict.wav / multi\_head\_transients/corrected\_strict.wav
- **Procedência A** TapeCore Native · original\_strict · 3bbcd720a2302cdd8b38d5d1e690d8ee521a9efb · Release · -O2 -fno-fast-math -DNDEBUG -std=c++17 · SHA-256 `d264bc913a4d0303d787601b69af90f08340352f57c00220c8b951350ed8ec96`
- **Procedência B** TapeCore Native · corrected\_strict · 04c89cc85b6e2175216d9c976a85ce360f0dc7e9 · Release · -O2 -fno-fast-math -DNDEBUG -std=c++17 · SHA-256 `c1a5e94d718e37d7bfab62594bddf258a412a649b8827227c6c000aa29d82bfd`
- **Decisão** APPROVE · 2026-11-16T19:54:51.249Z
- **Fidelidade / Musicalidade** — / —
- **Artefatos** —
- **Diferenças** —
- **Monitoração** MATCHED · canal Stereo · ganhos A/B 1.0000 / 1.0000 · proteção 1.0000
- **Escuta cega** Y · confiança 5 · semente f3943e2d-b673-41fe-9e17-01ea9dc07bdf · X = A, Y = B

Sem observações.

### multi head transients · Corrected Strict × Corrected Fast

- **Arquivos A/B** multi\_head\_transients/corrected\_strict.wav / multi\_head\_transients/corrected\_fast.wav
- **Procedência A** TapeCore Native · corrected\_strict · 04c89cc85b6e2175216d9c976a85ce360f0dc7e9 · Release · -O2 -fno-fast-math -DNDEBUG -std=c++17 · SHA-256 `c1a5e94d718e37d7bfab62594bddf258a412a649b8827227c6c000aa29d82bfd`
- **Procedência B** TapeCore Native · corrected\_fast · 04c89cc85b6e2175216d9c976a85ce360f0dc7e9 · Release · -O2 -ffast-math -fno-finite-math-only -DNDEBUG -std=c++17 · SHA-256 `e4a7c8cbed9251cb1583e3ad6444737f5ea052030ec631356c894712e48d596b`
- **Decisão** APPROVE · 2026-11-16T19:55:47.155Z
- **Fidelidade / Musicalidade** — / —
- **Artefatos** —
- **Diferenças** —
- **Monitoração** MATCHED · canal Stereo · ganhos A/B 0.9999 / 1.0000 · proteção 1.0000
- **Escuta cega** Equivalentes · confiança — · semente 0dda39ef-f0f2-4a2b-ab57-4a69daa99b44 · X = B, Y = A

Sem observações.

### multi head transients · Baseline × Corrected Fast

- **Arquivos A/B** multi\_head\_transients/baseline.wav / multi\_head\_transients/corrected\_fast.wav
- **Procedência A** TapeCore Native · baseline · f8d2851c663d62ae4850087dc5bb255bd8b818be · Release · -O3 -DNDEBUG -std=c++17 · SHA-256 `a668222e9007aa531e0d6385d8e3d88550085e61da6dce3dc626f94273a3f05f`
- **Procedência B** TapeCore Native · corrected\_fast · 04c89cc85b6e2175216d9c976a85ce360f0dc7e9 · Release · -O2 -ffast-math -fno-finite-math-only -DNDEBUG -std=c++17 · SHA-256 `e4a7c8cbed9251cb1583e3ad6444737f5ea052030ec631356c894712e48d596b`
- **Decisão** APPROVE · 2026-11-16T19:56:17.484Z
- **Fidelidade / Musicalidade** — / —
- **Artefatos** —
- **Diferenças** —
- **Monitoração** MATCHED · canal Stereo · ganhos A/B 0.9999 / 1.0000 · proteção 1.0000
- **Escuta cega** Equivalentes · confiança 5 · semente 64756aa6-dd6f-4ab6-b7c3-26201048e2e4 · X = B, Y = A

Sem observações.

### high feedback harmonic · Original Fast × Corrected Fast

- **Arquivos A/B** high\_feedback\_harmonic/original\_fast.wav / high\_feedback\_harmonic/corrected\_fast.wav
- **Procedência A** TapeCore Native · original\_fast · 3bbcd720a2302cdd8b38d5d1e690d8ee521a9efb · Release · -O2 -ffast-math -fno-finite-math-only -DNDEBUG -std=c++17 · SHA-256 `e668b04172190615f08d0c5e0a2ef7a0dbfa916f9322b1c2f2f492f849108c08`
- **Procedência B** TapeCore Native · corrected\_fast · 04c89cc85b6e2175216d9c976a85ce360f0dc7e9 · Release · -O2 -ffast-math -fno-finite-math-only -DNDEBUG -std=c++17 · SHA-256 `dc75884d3405c03a0c7ebfa6f65162d2393301205557f2bed173411c8d355da1`
- **Decisão** APPROVE · 2026-11-16T19:51:55.377Z
- **Fidelidade / Musicalidade** — / —
- **Artefatos** —
- **Diferenças** —
- **Monitoração** MATCHED · canal Stereo · ganhos A/B 1.0000 / 1.0000 · proteção 1.0000
- **Escuta cega** Equivalentes · confiança — · semente d437e234-27a5-4c94-a2b7-645748759ce7 · X = B, Y = A

Sem observações.

### ping pong · Original Fast × Corrected Fast

- **Arquivos A/B** ping\_pong/original\_fast.wav / ping\_pong/corrected\_fast.wav
- **Procedência A** TapeCore Native · original\_fast · 3bbcd720a2302cdd8b38d5d1e690d8ee521a9efb · Release · -O2 -ffast-math -fno-finite-math-only -DNDEBUG -std=c++17 · SHA-256 `2dbd81204e68dc3fe472ec6c962ffd8a2434257054320e78bd475f36060fc49c`
- **Procedência B** TapeCore Native · corrected\_fast · 04c89cc85b6e2175216d9c976a85ce360f0dc7e9 · Release · -O2 -ffast-math -fno-finite-math-only -DNDEBUG -std=c++17 · SHA-256 `ca16f5dc16f9f50e62b1b83ed0e56ab8e8c78a0acaa1acab4072b1fafba25663`
- **Decisão** APPROVE · 2026-11-16T19:56:38.517Z
- **Fidelidade / Musicalidade** — / —
- **Artefatos** —
- **Diferenças** —
- **Monitoração** MATCHED · canal Stereo · ganhos A/B 1.0000 / 0.9996 · proteção 1.0000
- **Escuta cega** Equivalentes · confiança 5 · semente f4364531-9551-4473-b9c2-0e64cea9f923 · X = B, Y = A

Sem observações.

### wow flutter excerpt · Original Fast × Corrected Fast

- **Arquivos A/B** wow\_flutter\_excerpt/original\_fast.wav / wow\_flutter\_excerpt/corrected\_fast.wav
- **Procedência A** TapeCore Native · original\_fast · 3bbcd720a2302cdd8b38d5d1e690d8ee521a9efb · Release · -O2 -ffast-math -fno-finite-math-only -DNDEBUG -std=c++17 · SHA-256 `1ed3238aa3c3558f2cbc2c2a09ee8076fd11c1ad7d9971997b213f50634a815f`
- **Procedência B** TapeCore Native · corrected\_fast · 04c89cc85b6e2175216d9c976a85ce360f0dc7e9 · Release · -O2 -ffast-math -fno-finite-math-only -DNDEBUG -std=c++17 · SHA-256 `17f52e337bf6778e4e1834f3a4b4b71508a51aaaf7d9ef724bddd8ffdbe2a2ef`
- **Decisão** APPROVE · 2026-11-16T19:51:24.264Z
- **Fidelidade / Musicalidade** — / —
- **Artefatos** —
- **Diferenças** —
- **Monitoração** MATCHED · canal Stereo · ganhos A/B 1.0000 / 1.0000 · proteção 1.0000
- **Escuta cega** Equivalentes · confiança 5 · semente 6089f186-0ce8-4d70-b969-7a23c8430c5a · X = B, Y = A

Sem observações.

### warp musical · Original Fast × Corrected Fast

- **Arquivos A/B** warp\_musical/original\_fast.wav / warp\_musical/corrected\_fast.wav
- **Procedência A** TapeCore Native · original\_fast · 3bbcd720a2302cdd8b38d5d1e690d8ee521a9efb · Release · -O2 -ffast-math -fno-finite-math-only -DNDEBUG -std=c++17 · SHA-256 `d4e134b68216910638e346589bfb6a76af48ce8b21e7f26418b1fb6d9bb3f05c`
- **Procedência B** TapeCore Native · corrected\_fast · 04c89cc85b6e2175216d9c976a85ce360f0dc7e9 · Release · -O2 -ffast-math -fno-finite-math-only -DNDEBUG -std=c++17 · SHA-256 `2c1d59ff6f982b5011fecf38cdafa29c39daa899ac143d857315adcd6ba2b187`
- **Decisão** APPROVE · 2026-11-16T19:49:12.442Z
- **Fidelidade / Musicalidade** — / —
- **Artefatos** —
- **Diferenças** —
- **Monitoração** MATCHED · canal Stereo · ganhos A/B 1.0000 / 1.0000 · proteção 1.0000
- **Escuta cega** Equivalentes · confiança — · semente e795560d-2af0-4a20-9bfe-0cd58f1b3110 · X = A, Y = B

Sem observações.

### warp raw integer · Original Fast × Corrected Fast

- **Arquivos A/B** warp\_raw\_integer/original\_fast.wav / warp\_raw\_integer/corrected\_fast.wav
- **Procedência A** TapeCore Native · original\_fast · 3bbcd720a2302cdd8b38d5d1e690d8ee521a9efb · Release · -O2 -ffast-math -fno-finite-math-only -DNDEBUG -std=c++17 · SHA-256 `33d3e817fad3f2d8e1fcaad148f2eec6d9469991b75f513c2ae53179174b66cc`
- **Procedência B** TapeCore Native · corrected\_fast · 04c89cc85b6e2175216d9c976a85ce360f0dc7e9 · Release · -O2 -ffast-math -fno-finite-math-only -DNDEBUG -std=c++17 · SHA-256 `a2e6b55284f5453ef1d0d898c4144152c23bdabecce243206ee18a9b441c835f`
- **Decisão** APPROVE · 2026-11-16T19:49:37.403Z
- **Fidelidade / Musicalidade** — / —
- **Artefatos** —
- **Diferenças** —
- **Monitoração** MATCHED · canal Stereo · ganhos A/B 1.0000 / 1.0000 · proteção 1.0000
- **Escuta cega** Equivalentes · confiança 5 · semente a34c7259-abbc-4c25-b222-74af18b1a313 · X = B, Y = A

Sem observações.

### warp feedback cascade · Original Fast × Corrected Fast

- **Arquivos A/B** warp\_feedback\_cascade/original\_fast.wav / warp\_feedback\_cascade/corrected\_fast.wav
- **Procedência A** TapeCore Native · original\_fast · 3bbcd720a2302cdd8b38d5d1e690d8ee521a9efb · Release · -O2 -ffast-math -fno-finite-math-only -DNDEBUG -std=c++17 · SHA-256 `16216e75fcca8977cb166bb29fde8918e2b489ead47c83618dbee9b8f9d81053`
- **Procedência B** TapeCore Native · corrected\_fast · 04c89cc85b6e2175216d9c976a85ce360f0dc7e9 · Release · -O2 -ffast-math -fno-finite-math-only -DNDEBUG -std=c++17 · SHA-256 `07eac8198c5fcab6f8164e4f3930746909908a23eb737ce84f3a1fc83fac5f8c`
- **Decisão** APPROVE · 2026-11-16T19:53:00.547Z
- **Fidelidade / Musicalidade** — / —
- **Artefatos** —
- **Diferenças** —
- **Monitoração** MATCHED · canal Stereo · ganhos A/B 1.0000 / 0.9925 · proteção 1.0000
- **Escuta cega** Equivalentes · confiança 5 · semente 8a1148e2-6e55-4184-bc0e-35b6b01b73dd · X = A, Y = B

Sem observações.

### silence decay · Original Fast × Corrected Fast

- **Arquivos A/B** silence\_decay/original\_fast.wav / silence\_decay/corrected\_fast.wav
- **Procedência A** TapeCore Native · original\_fast · 3bbcd720a2302cdd8b38d5d1e690d8ee521a9efb · Release · -O2 -ffast-math -fno-finite-math-only -DNDEBUG -std=c++17 · SHA-256 `b9973bab1fc0b9c5854bdb5f6c5593025199f1bb5dca9f487ffec14e241c9548`
- **Procedência B** TapeCore Native · corrected\_fast · 04c89cc85b6e2175216d9c976a85ce360f0dc7e9 · Release · -O2 -ffast-math -fno-finite-math-only -DNDEBUG -std=c++17 · SHA-256 `f083caf3f9d0fb107ec6667446a7e01e787d6dfbe82c0e5cb3946f1a7ea0d6f9`
- **Decisão** APPROVE · 2026-11-16T19:53:26.756Z
- **Fidelidade / Musicalidade** — / —
- **Artefatos** —
- **Diferenças** —
- **Monitoração** MATCHED · canal Stereo · ganhos A/B 1.0000 / 0.9997 · proteção 1.0000
- **Escuta cega** Equivalentes · confiança 5 · semente b4f8637f-de59-49b7-a36e-2883daf4cd30 · X = A, Y = B

Sem observações.

### warp feedback 95 · Original Fast × Corrected Fast

- **Arquivos A/B** warp\_feedback\_95/original\_fast.wav / warp\_feedback\_95/corrected\_fast.wav
- **Procedência A** TapeCore Native · original\_fast · 3bbcd720a2302cdd8b38d5d1e690d8ee521a9efb · Release · -O2 -ffast-math -fno-finite-math-only -DNDEBUG -std=c++17 · SHA-256 `e14e9931183eb1899469ffb030a587593813e018a6deeadaddb826d64d938923`
- **Procedência B** TapeCore Native · corrected\_fast · 04c89cc85b6e2175216d9c976a85ce360f0dc7e9 · Release · -O2 -ffast-math -fno-finite-math-only -DNDEBUG -std=c++17 · SHA-256 `6bacb8da315b191f84588a30f5a177206c262d356229ecbb54f61650290e44dc`
- **Decisão** APPROVE · 2026-11-16T19:53:50.181Z
- **Fidelidade / Musicalidade** 5 / —
- **Artefatos** —
- **Diferenças** —
- **Monitoração** MATCHED · canal Stereo · ganhos A/B 1.0000 / 0.9954 · proteção 1.0000
- **Escuta cega** Equivalentes · confiança 5 · semente 7dc28191-5719-4a0d-9427-37908362957f · X = A, Y = B

Sem observações.
