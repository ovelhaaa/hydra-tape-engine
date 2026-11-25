# F5 — Musical Timing, Capstan Glide & Clock Synchronization

**Branch:** `experiment/dsp-reform-f5-musical-timing`  
**Base:** F3 `29f50258f1ac47139ac8f1fdb21aa59424ca9a75` (PR #68)  
**Default:** `HYDRA_DSP_REFORM_F5_TIMING_VARIANT=0` — Vintage F3-A, bit-exact.  
**Estado:** `READY_FOR_HUMAN_LISTENING` após o artifact da CI; **hardware
`PENDING_PHYSICAL`**; decisão musical **humana e pendente**.

## Resultado em uma frase

A F5 separou o tempo musical da leitura do delay: o Global Transport Clock continua exato,
enquanto os três cabeçotes musicais ganharam um glide de capstan (B) e uma política
focada em Tap/MIDI (C). F5=0 continua bit-exact com a Vintage; nenhuma candidata foi
promovida.

## Números principais

| Verificação | Resultado |
|---|---|
| F5=0 vs manifesto F3-A (10 cenários F0.2) | **10/10 bit-exact** |
| F5=1/2 vs F5=0 sem mudança de tempo (10 + controles) | **10/10 + 2/2 bit-exact** |
| Instrumentado vs não instrumentado | **39/39 bit-exact** |
| CTest Native | **23/23 PASS** |
| Native vs WASM | **max_abs = 0, RMSE = 0** |
| F5 v0/v1/v2 no WASM | **3/3 PASS** |
| Blocos 37/64/128/256 vs 128 | **max_abs = 0** nas três variantes |
| 120→100 musical: A / B / C | salto de 4.410 amostras / 1,270 s / 1,270 s até 1 ms |
| 120→125 MIDI: A / B / C | 20,0 ms de erro permanente / 0,416 s / 0,242 s até 1 ms |
| 120→90 MIDI: A / B / C | 166,7 ms de erro / 1,188 s / 1,751 s |
| Pitch de pico (maior gesto manual) | B/C 223 cents (A: descontinuidade) |
| Jitter MIDI ±1,5 ms | B 50 cents / C 53 cents (sem chasing) |
| Reverse V2, 9 divisões, 120→90 | `effective == requested` exato em A/B/C |
| Feedback stability (FB95/multi/Reverse, 60 s) | **9/9 PASS** (3 casos × 3 variantes), 0 clipped |
| Firmware ESP32-S3 | **3/3 builds SUCCESS**, +980/+1.136 B de flash |
| Pacote de escuta | **11 cenas, 66 WAVs RAW+MATCHED** em `artifacts/dsp-reform-f5/listening/` |
| Hardware físico | **PENDING_PHYSICAL** |

## O que foi encontrado

1. **Duas semânticas de tempo.** O modo ms usa um one-pole de ~200 ms; o modo musical
   troca o endereço de leitura instantaneamente ao mudar o BPM. Em 120→100 o cabeçote 3
   salta 4.410 amostras (100 ms).
2. **Alvo obsoleto em mudança só de transporte.** `hydra_dsp_transport_set_bpm` (caminho
   do MIDI e da UI web) atualiza o GTC, mas os cabeçotes só são recalculados no próximo
   commit de parâmetros. Em 120→125 via MIDI o delay fica 882 amostras (20 ms) fora do
   tempo.
3. **Dive de uma oitava no modo ms.** 500→600 ms produz ~1.200 cents no primeiro instante
   e o one-pole trava ~8,6 amostras antes do alvo por resolução de float.
4. **Reverse V2 já estava correto**: reconfigura pelo BPM exato do GTC e não foi alterado.

## O que mudou em B e C

- **B (Capstan Glide):** um estado de erro em amostras/beat move os três cabeçotes com
  `alpha` de ~200 ms e teto de 0,12 amostra/amostra; razões 0,333/0,75/1 preservadas;
  dead-band de 0,05 amostra; modo ms reutiliza `smoothedDelaySamples`.
- **C (Clock-Focused):** classifica o episódio de clock (micro ≤1% → capstan; 1–5% →
  tau 30 ms/teto 5%; >5% → tau 150 ms/teto 8,5%), com dead-band de 2 ms no alvo,
  filtro de jitter e finalização sem mover a cabeça. Internal/ms continuam capstan.
- O alvo musical passou a acompanhar o GTC em `transport_set_bpm`, corrigindo o caso 2.
- Nenhum leitor extra, buffer, ABI, preset, golden ou peso de cabeçote foi alterado.

## Pacote e reprodutibilidade

```sh
python docs/evidence/dsp-reform-f5/run.py build
python docs/evidence/dsp-reform-f5/run.py baseline
python docs/evidence/dsp-reform-f5/run.py static
python docs/evidence/dsp-reform-f5/run.py measure
python docs/evidence/dsp-reform-f5/run.py measure-clock
python docs/evidence/dsp-reform-f5/run.py reverse
python docs/evidence/dsp-reform-f5/run.py equiv
python docs/evidence/dsp-reform-f5/run.py blocksize
python docs/evidence/dsp-reform-f5/run.py drift
python docs/evidence/dsp-reform-f5/run.py tail
python docs/evidence/dsp-reform-f5/run.py render
python docs/evidence/dsp-reform-f5/run.py package
```

- `f5bench.py` — build, engine ctypes, fontes, métricas e glide.
- `f5scenes.py` — catálogo de cenas (manual, ms, Tap, MIDI, modos, controles).
- `summarize.py` — gera `MEASUREMENT_TABLES.md` a partir dos JSON.
- `run.py` — CLI e empacotamento.
- `listening.html` / `listening.mjs` — player A/B/C com marcador de comando.

## Limitações e pendências

- **Físico:** nenhum benchmark na placa; DSP P50/P95/P99/peak, full-work, I2S, overruns,
  memória e stack continuam `PENDING_PHYSICAL`. Builds ESP32 não são execução.
- **C em mudanças grandes:** mais lenta que B por escolha; se a escuta exigir
  instantaneidade, a decisão é F7 (leitor de transição), não esta fase.
- **Custo:** `.text` +3,1 KB no objeto e +1 KB no firmware; estado 56 B. Não é aprovação
  física.
- **Publicação:** o pacote local está verificado por SHA-256; o artifact público depende
  do workflow `dsp-reform-f5.yml`.
- **F4/#69:** não integrada; esta branch não contém as transições da F4.
- **Golden/ABI:** preservados; nenhuma regeneração.

## Documentos

| Arquivo | Conteúdo |
|---|---|
| [TIMING_ARCHITECTURE.md](TIMING_ARCHITECTURE.md) | camadas, variantes, critérios pré-registrados |
| [BASELINE_TIMING_MEASUREMENTS.md](BASELINE_TIMING_MEASUREMENTS.md) | caracterização da Vintage/A2 |
| [CAPSTAN_GLIDE.md](CAPSTAN_GLIDE.md) | F5-B, medições |
| [CLOCK_FOCUSED_TIMING.md](CLOCK_FOCUSED_TIMING.md) | F5-C, troca velocidade × suavidade |
| [MIDI_TAP_RECOVERY.md](MIDI_TAP_RECOVERY.md) | Tap/MIDI, perda, holdover, reaquisição |
| [REVERSE_SYNC_VALIDATION.md](REVERSE_SYNC_VALIDATION.md) | 9 divisões, 120→90 |
| [NATIVE_WASM_VALIDATION.md](NATIVE_WASM_VALIDATION.md) | CTest, WASM, paridade estrita |
| [PERFORMANCE_ESTIMATE.md](PERFORMANCE_ESTIMATE.md) | estado, flash, custo, pendência física |
| [LISTENING_GUIDE.md](LISTENING_GUIDE.md) | como ouvir o pacote A/B/C |
| [MEASUREMENT_TABLES.md](MEASUREMENT_TABLES.md) | tabelas geradas das medições |
| [PR_DRAFT.md](PR_DRAFT.md) | texto da PR Draft |
| [HANDOFF.md](HANDOFF.md) | handoff e recomendação F6/F7 |
