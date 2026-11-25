# Handoff F5

```
FASE: F5 — música: tempo, capstan e clock
STATUS: READY_FOR_HUMAN_LISTENING (CI/artifact pendente) / PENDING_PHYSICAL
BASE HEAD: 29f50258f1ac47139ac8f1fdb21aa59424ca9a75 (F3, PR #68)
CANDIDATE HEAD: experiment/dsp-reform-f5-musical-timing (SHA em build.json)
ARQUIVOS LIDOS: AGENTS.md; docs/TAPE_DELAY_DSP_MUSICAL_AUDIT_2026-11-23.md;
  docs/DSP_REFORM_IMPLEMENTATION_PHASES.md; docs/DSP_REFORM_PR_RECONCILIATION.md;
  docs/evidence/dsp-reform-f3/*; docs/evidence/dsp-reform-f4/* (via git show);
  tape_core.cpp/.hpp; global_transport_clock.hpp; tap_tempo.hpp;
  midi_clock_sync.hpp; reverse_windows.hpp; docs/REVERSE_V2.md;
  tests/core/{transport_clock,tap_tempo,midi_clock,reverse_core}_test.cpp;
  host/native/esp32/hydra_dsp_esp32_adapter.cpp; host/web/*; src/main.cpp.
CAUSA / HIPÓTESE AUDÍVEL: A2 do relatório — modo ms com one-pole de 200 ms vs modo
  musical sem smoothing; mudança só de transporte não recalcula os cabeçotes.
DIFERENÇAS IMPLEMENTADAS:
  dsp/core/musical_timing_experiment.hpp (novo, políticas)
  dsp/core/f5_timing_hooks.hpp (novo, hook read-only de teste)
  dsp/core/tape_core.cpp (glide + alvo coerente + hook)
  dsp/core/tape_core.hpp (diagnóstico sob macro de teste)
  dsp/core/hydra_dsp_c_api.cpp (função de teste sob macro)
  CMakeLists.txt (3 alvos F5)
  platformio.ini (3 envs de build F5)
  tests/core/f5_timing_test.cpp (novo)
  docs/evidence/dsp-reform-f5/*; .github/workflows/dsp-reform-f5.yml
SEM MUDANÇAS NÃO RELACIONADAS: sim; nenhum ABI/preset/golden/peso de cabeçote,
  feedback, saturação, LUT, Reverse, Warp, UI ou default alterado.
BACKEND NATIVE/WASM: max_abs 0 / RMSE 0; F5 v0/v1/v2 PASS em ambos.
TESTES CORE/ABI/REVERSE/WARP: Native 23/23; WASM goldens 3/3; F5 3/3; Reverse
  effective==requested em 9 divisões; blocos 37/64/128/256 max_abs 0.
SONIC A/B RAW/MATCHED: 11 cenas / 66 WAVs; manifesto + gabarito + SHA-256;
  publicação pendente do workflow. Nenhuma decisão humana inferida.
CPU FÍSICA: PENDING_PHYSICAL. Objeto +3,1/+3,2 KB text; firmware +980/+1.136 B;
  estado 56 B; nenhuma alocação/lock/thread.
I2S / OVERRUNS / MEMÓRIA / STACK: não medidos na placa.
GOLDENS / API / PRESETS: preservados; F5=0 bit-exact 10/10 F3-A.
RISCO RESIDUAL: C é mais lenta em mudanças grandes; sem leitor de transição (F7).
ROLLBACK: F5=0 (default) ou remover a flag; código novo está atrás de macro.
AUTORIZAÇÃO DA PRÓXIMA FASE: ausente.
```

## Recomendação técnica

1. **Ouvir o pacote A/B/C** e decidir se o gesto de B ou C é preferível à Vintage. A
   escuta é o gate; os números não promovem nada.
2. **Campanha física F5=0 vs escolhida** na mesma placa, com Tap/MIDI e automação de BPM;
   medir P99/peak, full-work, I2S e memória. Até lá, `PENDING_PHYSICAL`.
3. **F6** (EQ/graves) pode começar em paralelo independente, pois não depende da política
   temporal; preservar a referência Vintage e a rota aprovada.
4. **F7** só se a escuta rejeitar a limitação de leitor único em mudanças grandes
   (ex.: 120→90 em <0,5 s sem pitch dive). Nesse caso, avaliar um leitor de transição
   curto/crossfade com custo medido; **não** implementar dois TapeCores nem duplicar
   buffers.
5. **F10** não é gatilho: não há evidência de que o Hermite atual seja o gargalo; o
   problema é de política de alvo, não de interpolador.
6. Não integrar #59, #61, #62 ou #69 nesta branch; F4 e F5 permanecem avaliações
   separadas.
