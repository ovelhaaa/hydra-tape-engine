# Comparação F3

| Variante | Hipótese | Situação musical |
|---|---|---|
| A Vintage Reference | F2-C intacta, referência congelada | selecionada somente como base experimental |
| B Smooth Saturation | menos excitação no primeiro estágio, trim maior | equivalência perceptual registrada, sem preferência |
| C Harmonic Bloom | mais excitação no primeiro estágio, maior atenuação intermediária | equivalência perceptual registrada, sem preferência |

Os nomes descrevem hipóteses. C não acrescenta drive por repetição: a densidade
só evolui com a recirculação natural. Não há garantia de que harmônicos cresçam
monotonicamente em cada repetição, pois filtros e decay também participam.

Escuta humana concluída:26/26 avaliações equivalentes,24 confianças médias/2
altas. Nenhuma vantagem perceptual demonstrada neste pacote; sem rejeição ou
vencedora. [Registro completo](HUMAN_LISTENING_RESULT.md). A permanece referência
existente; nenhuma promoção. Hardware continua PENDING_PHYSICAL.

Matriz dinâmica local:192 cenas/candidata,44.1/48kHz, feedback0/25/50/65/75/85/95/100,
níveis−24/−12/−6/0dBFS e seno/dois tons/burst. B e C diferem de A em168/192
hashes;24 cenas FB0 permanecem exatas, como esperado sem recirculação.
Instrumentação vs motor não instrumentado: bit-exact em todas as576 cenas.

Exemplo **estimado do motor completo**,44.1kHz,FB85,−6dBFS:

| Métrica | A | B | C |
|---|---:|---:|---:|
| THD2–10, seno1kHz, janela650–850ms | .199836 | .197404 | .201000 |
| Crest saída do render4s (dB) | 9.40662 | 9.41764 | 9.39985 |
| Pico feedback interno | 1.031664 | 1.034274 | 1.024052 |
| IMD3, tons900/1100Hz | .085410 | .085017 | .084868 |

Os valores THD/IMD são razões lineares. Mudanças pequenas e mensuráveis:
B preserva um pouco mais de pico/crest nessa cena; C comprime mais o pico
do loop e eleva modestamente a proporção harmônica. Isso não comprova
melhoria audível; IMD não aumenta nessa comparação. A diferença é sutil,
portanto a escuta pode legitimamente concluir equivalência ou rejeição.
Não confundir com medições laboratoriais de um único estágio sem memória.

Clipping ativo da guitarra/fast/new/FB100 a44100Hz: A38/B38/C30.
Referência A38 reproduz F2-C; nenhum ganho/limiter alterado para esse resultado.
Resultados completos nas duas taxas, headroom e caudas no
`validation-summary.json` publicado com o pacote e no relatório de estabilidade.

Pacote13 cenas/78WAVs RAW e RMS-MATCHED, alocação X/Y/Z fixa por cena,
gabarito separado. Sete fontes sintéticas; caudas16/60s; modos Single,
Multi-Head, Reverse e Ping-Pong. Não é audição humana nem gravação DI.
Critérios e método: [GAIN_STAGING_ANALYSIS.md](GAIN_STAGING_ANALYSIS.md).
Nenhuma candidata foi promovida. Warm e C0 continuam disponíveis com F3=0.
