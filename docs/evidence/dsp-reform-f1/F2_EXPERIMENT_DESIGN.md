# F2 — duas hipóteses, nenhuma implementada

Referência F2-A: **Classic C0 atual**, receita e hashes F1. A produção A continua
separada. Não escolher candidata automaticamente por curva teórica. Condições
iniciais sugeridas abaixo são hipóteses de calibração, sujeitas a medição e
reversão, não coeficientes aprovados. F2 permanece dependente de ordem própria.

| Item | F2-B — Warm Tape | F2-C — Vintage Progressive |
|---|---|---|
| Hipótese distinta | Recuperar corpo80–300Hz sem boom | Escurecimento por passagem preservando definição inicial |
| Parâmetros internos propostos | HPF300→180Hz, Q=.5; ganho do bump eletrônico1→~.6dB em amount50, preservando centro dependente de speed e Q=.8 | HPF300→240Hz, Q=.5; cutoff do gap **de feedback** ×.75 com limites atuais; bump eletrônico1→~.8dB em amount50 |
| Preservar | Playback privado C e monitor, gap atual, ganho/sat/compressor | Playback privado C e monitor, ganho/sat/compressor |
| Bandas/repetições esperadas | Menor queda80–150 e150–300;40–80 ainda atenuado, médio/HF similares antes das interações não lineares | Menor mudança de corpo que B; perda>4k maior nas repetições2–8, primeira leitura conserva playback C; verificar1–4k não sumir cedo |
| Guitarra/baixo | Fundamentais mais presentes; risco de encobrir ataques/palm-mute, baixo deve manter articulação | Ataque inicial claro e cauda escura; baixo/synth podem ficar melhores ou excessivamente médios |
| Feedback75/85/100 | Maior loop gain grave pode mudar sweet spot, compressor e conteúdo harmônico | Menor HF pode concentrar energia média e mudar sustain; não presumir estabilidade por cortar agudos |
| Riscos | Acúmulo grave, ringing e ressonância bump/HPF | Perda de articulação, ressonância média, tail curta demais |
| CPU/RAM previsto | ~zero delta por amostra/zero estado ou buffer; coeficientes existentes | ~zero delta por amostra/zero estado ou buffer; coeficientes existentes |
| Reversão | Remover seleção opt-in e voltar aos coeficientes/hashes C0 | Idem, sem mexer em presets/ABI |
| Teste específico | Notas E1/E2, synth40–150Hz, acordes e kick; medir bandas1–20 e tails60s | Pratos/palhetada/percussão e acordes; resposta1/2/4/8/16, verificar corpo e duração |

Não usar o parâmetro público headBump para mudar inadvertidamente monitor e
feedback juntos: calibrar apenas mapeamento interno identificado, opt-in e
reversível. Não criar parâmetros/IDs; manter38–48 reservados às branches
existentes. Não adicionar filtros ou alterar pré/de-emphasis/saturação nesta
fase. Ajustes apenas nos coeficientes existentes ainda exigem benchmark físico
porque novo sinal pode mudar atividade dos caminhos condicionais.

## Cenários determinísticos e audição A/B/C

Matriz executável preparada:8 fontes ×4 feedbacks ×5 modos =160 cenas,
parâmetros completos no manifesto; sem renders de candidatas inexistentes.

Matriz mínima: guitarra limpa em arpejo, guitarra saturada, **baixo elétrico**,
synth mono grave, acordes synth, bateria e percussão irregular. F1 fornece as
seis fontes musicais F0.2; `prepare_audition.py` prepara adicionalmente baixo plucked sintético e synth
grave41–82Hz, com hashes em `f2-audition-manifest.json`. Gravações DI reais
continuam pendentes e deverão ser congeladas antes de novos renders.
Não apresentar synth grave como gravação real de baixo ou fonte sintética como
avaliação humana. Gravações humanas adicionais são desejáveis, não inventadas.

Cada fonte × feedback50/75/85/100 (limite público, clamp interno.88) × single,
Multi-Head(mask7), ReverseV2, Ping-Pong. Usar também Multi-Head+Ping-Pong como
stress, silêncio/hiss, impulsos e caudas60s. Separar os modos na análise por
repetição: Reverse e multi-taps sobrepostos não têm a mesma janela single500ms.
Cobrir speed0/50/100, age0/50/100 e níveis−24/−12/−6dBFS em subconjunto crítico.
Sine/bursts80/130/200/1000Hz ajudam harmônicos/ringing, sem substituir música.

Congelar entradas e todos os38 parâmetros, seeds, SR44100 (checagem adicional
48000), bloco128, lifecycle novo handle/prepare/reset/set/play/commit por cena,
sem preroll salvo receita explícita idêntica. Mesma toolchain/flags C0 para os
três renders. Exportar float32 e PCM24 com hashes/proveniência; iniciar e alinhar
no mesmo frame. Incluir seleções cegas aleatórias persistidas com seed; revelar
A/B/C só depois das notas. Não trocar nome/identidade do áudio sem manifesto.

- **RAW:** saída sem ajuste, mesma entrada/ganho/mix para A/B/C. Ouvir decay,
  sustain, headroom e volume real; preservar silêncio e cauda.
- **MATCHED:** calcular loudness integrado de cada cena sobre trecho musical
  comum e ativo; aplicar **um único ganho constante por arquivo**, relativo à
  C0. Documentar método, janela, ganho e pico/true-peak. Se não houver medidor
  LUFS validado, usar RMS estéreo da janela explicitamente rotulado RMS-matched.
  Reduzir todos os slots pelo mesmo trim se necessário para headroom. Nunca
  normalizar cada repetição nem limitar para esconder diferença.

Ouvinte avalia1–5 e anota: corpo primeiras repetições, definição de ataques,
escurecimento progressivo, acúmulo grave, sweet spot, caudas longas e acordes/
notas graves; preferência/equivalência/rejeição e confiança. Pausas e entrada/
saída Reverse, troca de modo, Warp controle negativo e mono também cobertos.
Não selecionar B/C se indistinguível ou pior; C0 permanece rollback aprovado.

## Gates de decisão F2

Native/WASM mesmo candidato max_abs≤1e-5/RMSE≤1e-6, declarar bit-exact quando
ocorrer. LUT/input exatos; ABI, Reverse/Warp, Multi-Head/Ping-Pong, Tap/MIDI,
presets, estabilidade60s, finitude e amplitude preservados. Nunca usar goldens A
como expectativa C sem qualificar; manter a falha de energia A documentada e
comparar C0×candidata, sem relaxar o teste original.

Comparar energia absoluta/relativa nas seis bandas, delta por repetição,
RMS/pico, harmônicos, ringing/decay e acumulado grave; verificar nível interno
quando necessário para excluir limitação de saída escondendo crescimento.
A/B humano deve preferir ou aceitar a candidata com benefício identificável;
resultado ambíguo preserva C0. Não impor tolerâncias sonoras arbitrárias como
substituto de audição.

**PENDING_PHYSICAL**: executar C0 e cada candidata na mesma placa ESP32-S3
240MHz/44100/128, flags efetivas arquivadas/identity/hash do firmware, automação
confirmada, P50/P95/P99/peak DSP e full-work, deadline2902.49µs, zero overruns/
I2S, memória/stack. Histórico2333/2604µs não valida nova build.

Integração posterior: testar separadamente C1–C3/#59, M0–M3/#61 e Studio/#62
nas respectivas branches sem merges artificiais, preservando C0 de cada uma.
C0 desta entrega não contém esses algoritmos/IDs adicionais. Aceitação offline
F1 permite preparar/implementar F2 isolada quando autorizada; promoção ao
firmware exige audição e hardware documentados, em PR e decisão separadas.
