# Escuta A/B — Accidental Warp V1.3

- **Status** `PENDING HUMAN REVIEW`
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

- **Revisor e data:** pendentes.
- **Avaliação por trecho e equipamento:** pendente.
- **Decisão:** `PENDING HUMAN REVIEW`.
