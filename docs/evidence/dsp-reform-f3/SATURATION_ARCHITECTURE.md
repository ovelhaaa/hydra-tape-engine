# Arquitetura F3

Preflight: branch `experiment/dsp-reform-f3-saturation`, base `2c15b88`.
PRs #58–#67 abertas/Draft/sem merge na consulta desta rodada. #67 sem commits
novos. Autorização F3 explícita do usuário, nenhuma autorização F4/release.

Caminho ativo: `src/main.cpp:899` → adaptador ESP32 → API C
`hydra_dsp_c_api.cpp:548` → TapeCore. TapeModel permanece legado.
Tape/Reverse/Warp compartilham os mesmos buffers e gravador magnético.

Ordem na fonte base `dsp/core/tape_core.cpp`:

1. `:419`: driveGain = drive × .05 (ganho de gravação da entrada nova).
2. `:423–424`: safeFeedback min(FB/100,.88); Reverse tem alvo .83×min(FB/100,.95).
3. `:1250–1268`: reprodução privada C (dropout/azimute/shelf/bump/gap).
4. `:1195–1201`: HPF280/Q.5 → feedbackDrive → bump → gap → allpass.
   feedbackDrive = 1 + feedbackGain×.30×(1−reverseBlend)
   + clamp(inputEnv×.18,0,.22) + wear×.10.
5. `:1203–1211`: pré-ênfase **escalar**, sat racional ×1.10, trim .82,
   sat racional ×1.08, de-emphasis recíproca. F3 B/C mudam só esses três ganhos.
6. `:1213–1215`: feedbackGain → compressor simétrico → enable ramp.
7. `:1225–1233`: entrada condicionada×driveGain + feedback → DC blocker →
   clamp±4 → TapeMagnetics (LUT e bias médio intactos) → memória compartilhada.
8. `:1236`: mix de monitor → limiter; Smear/Spring opcionais posteriores.

Saturação eletrônica no loop, compressor e magnetização são operações distintas.
O compressor em `tape_integrity.hpp:28` é uma curva **estática sem envelope**:
joelho .5–.7, threshold .6, ratio1.5:1. Não há tempos de ataque/release próprios.
Limiter de saída (`:278`): compressão acima .9 e clamp±.99. A contagem no teto
nunca é usada como sinônimo de saturação interna. Nenhum ganho de saída muda.

| F3 | Primeiro drive | Trim | Segundo drive | Produto linear |
|---|---:|---:|---:|---:|
| A | 1.10 | .82 | 1.08 | .97416 |
| B | 1.04 | .86 | 1.08 | .965952 |
| C | 1.16 | .78 | 1.07 | .968136 |

B desloca parte da excitação para depois do primeiro saturador. C excita mais
o primeiro estágio, reduzindo seu nível antes do segundo. São hipóteses de
redistribuição, não novos saturadores nem promessa de superioridade. A pequena
redução de ganho linear (aprox. .0735/.0539dB) deve ser separada do efeito não
linear e da loudness. A progressão temporal só pode vir da recirculação natural;
sem estado/contador por repetição. O drive nominal de feedback e os limites
globais são preservados. FB95 e100 podem compartilhar o alvo .88 no modo normal.

Custo estrutural: zero estados/buffers/alocações/transcendentes adicionais,
mesmos três produtos e dois saturadores. Nenhum novo ramo runtime para seleção.
Comparar tamanho de objeto apenas como indicador; ESP32-S3 físico pendente.
Alvo:240MHz/44100/estéreo/128, deadline2902.49µs. Antes de promoção exigir
P50/P95/P99/peak DSP/full-work, stack/heap/PSRAM/I2S/overruns e flags/binários.

Leituras: AGENTS, auditoria musical, G0/F0–F11, protocolos F0.2/F1; evidência F2
IMPLEMENTATION/STABILITY/SPECTRAL/NATIVE_WASM/validation/render-manifest;
TapeCore/header/LUT/reverse_windows/feedback_routing/tape_integrity/API/mech_noise;
integração main/adaptador e referências legado; relatórios arquitetura/Hydra2,
ESP32/7.2E/7.2 feedback/6 bias/6 estabilidade/equivalência/Reverse/Warp;
CTest/CMake/workflows, instrumentação/audio_analysis/musical_sources/Listening Lab.
Não se reexecuta a matriz espectral F2: coeficientes estão congelados.
