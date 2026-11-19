# Phase 6.2 — escuta controlada do bias

A candidata permanece **BLOCKED — REGRESSION / PERFORMANCE / HUMAN AUDITION**. A medição física encontrou overruns no Reverse rítmico tanto no baseline quanto na candidata; consulte o [acompanhamento físico](PHASE6_BIAS_HARDWARE_FOLLOWUP.md). A disponibilidade deste pacote não representa aprovação técnica ou musical.

Abra o player existente em `player/` por localhost e selecione a pasta extraída `phase6-bias-ab`. O pacote complementar contém 60 comparações: 12 fontes/configurações, cada uma contra cinco variantes. Os 20 cenários originais são preservados.

O player abre em **Phase average (candidate)**. Os 60 pares aparecem em 12 grupos por cenário, com cinco alternativas em cada grupo. **A · Triangular baseline representa a saída do baseline; B identifica a variante processada pelo nome.** As mesmas identificações aparecem nas formas de onda. Ambos são sinais processados. Não há normalização nem compensação automática de volume. Cada par usa a mesma fonte, seed, parâmetros, BPM, duração de seis segundos e eventos. Os hashes de bibliotecas, fonte e áudio constam no manifesto. Os trechos curtos complementam, mas não substituem, os ensaios de estabilidade e decay prolongado.

`bias_off` é somente ablação diagnóstica. `sine` usa senoide de 18 kHz. `bandlimited` conserva a fundamental do triângulo (amplitude 8/π²); ela é a única harmônica triangular representável abaixo de Nyquist a 48 kHz. As amplitudes das duas variantes não são iguais. `oversampled` é experimento localizado. `averaged` representa o efeito médio do bias na não linearidade. Nenhuma alternativa recebe aprovação humana automática.

Escute com volume moderado e constante; mantenha loop e posição ao alternar A/B. Comece por senoide e silêncio com hiss para localizar apitos, depois guitarra limpa e acordes, drive, sintetizadores e bateria. Compare feedback moderado/alto, Reverse V2 e Multi-Head. O silêncio com hiss não é silêncio absoluto na saída.

Avalie apito residual, aspereza, agudos, compressão, harmônicos, transientes, sensação de fita, feedback, Reverse e coerência estéreo. Registre diferenças de volume como observação, sem compensar cada versão separadamente. Marque instante e gravidade dos problemas.

Esses dez critérios aparecem no painel de avaliação ao carregar o pacote de bias. As notas desse pacote são armazenadas separadamente das notas do pacote musical de 20 cenários, mesmo quando os commits coincidem. JSONs anteriores do mesmo commit e dos mesmos arquivos continuam importáveis. Ao trocar de par, a reprodução para e o lado A é selecionado, sem autoplay.

Todos os itens começam em `NOT REVIEWED`. Use `PASS`, `NEEDS ATTENTION` ou `FAIL` somente após sua escuta e exporte o JSON por **Export reviews**. Esse JSON, vinculado ao commit e aos hashes, é o registro de revisão humana; métricas ou a ausência do pico de 6 kHz não o substituem.

Reprodução: `python scripts/package_bias_ab.py --archive artifacts/hydra-phase6-bias-ab.zip`, após gerar `artifacts/bias-antialiasing/libraries.json`. O render usa 48 kHz e blocos de 128 frames; as medições técnicas separadas incluem 44,1 kHz. A validação do pacote verifica PCM24 estéreo, frames, telemetria, caminhos e hashes antes da distribuição.
