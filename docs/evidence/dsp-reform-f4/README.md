# F4 — Transições de performance (Vintage F3-A preservada)

Estado: **READY_FOR_HUMAN_LISTENING** na fonte local; hardware **PENDING_PHYSICAL**;
publicação CI / decisão musical humana **pendentes** (ver `published-artifact.json`).

A F4 parte da HEAD verificada da F3
`29f50258f1ac47139ac8f1fdb21aa59424ca9a75` (PR #68) e mantém a referência
musical **Vintage F3-A** com `Classic C=1`, `F2=2`, `F3=0`. As melhorias são
opt-in em `HYDRA_DSP_REFORM_F4_TRANSITIONS=1`; o default 0 é a Vintage original,
compilada sem as expressões novas.

## Resultado em uma frase

Saltos de seleção de cabeçote e de liberação do Freeze foram reduzidos por
rampas curtas nos ganhos já existentes; Mix e Drive receberam rampas de 10 ms
quando a medição mostrou descontinuidade; Tone não foi alterado porque o
`pbTransition` de 20 ms já protege a troca de coeficientes. Nenhum coeficiente
Vintage, saturação, compressor, modelo magnético, ABI ou preset mudou.
F4=0 continua **bit-exact** com a F3-A em 10/10 renders; F4=1 é bit-exact com
F4=0 em 10/10 cenários estáticos.

## Números principais

| Verificação | Resultado |
|---|---|
| F4=0 vs manifesto F3-A (10 cenários F0.2) | **10/10 bit-exact** |
| F4=1 vs F4=0, mesmos parâmetros, sem automação | **10/10 bit-exact** |
| Blocos 1/37/64/128/256, comando no mesmo frame | **max_abs = 0** nas cinco transições |
| Head 1→3: passo de 1 ms no comando | **0,03800 → 0,00527** (−86%) |
| Head 1→3: HF 4–16 kHz 5 ms vs pré-janela | **+9,97 dB → −0,87 dB** |
| Head 1+2→2+3: passo de 1 ms | **0,03028 → 0,00421** (−86%) |
| Head 3→1+2+3: passo de 1 ms | **0,00824 → 0,00462** (−44%) |
| Mix 0→100: passo de borda | **0,03964 → 0,00027** |
| Freeze release no arrivo do eco: HF vs controle | **+5,4 dB → −18,9 dB** (fb 70) |
| Drive 0→100 no arrivo do eco: passo de 1 ms | **0,04013 → 0,01985** (−51%) |
| Objeto Native `.text` | 84.160 → 85.960 bytes (+2,1%), data/bss 0 |
| Estado novo | ~64 bytes no `Impl` (heap; buffers inalterados) |

Onde a medição não mostrou ganho consistente (segundo evento de Drive,
cabeçote 3→123 em material correlacionado) o resultado está registrado como
não-superioridade, não como melhoria. O Freeze de entrada permanece idêntico
ao original: a medição não sustenta um clique de entrada.

## Pacote A/B

13 cenas, 52 WAVs PCM24 (RAW + MATCHED), gabarito separado, player baseado no
`Deck` do Listening Lab existente e SHA-256 de todos os arquivos:
`artifacts/dsp-reform-f4/listening/`.

- Guia: [LISTENING_GUIDE.md](LISTENING_GUIDE.md)
- Manifesto de renderes: [render-manifest.json](render-manifest.json)
- Medições completas: [AUTOMATION_MEASUREMENTS](AUTOMATION_MEASUREMENTS.md)
- Auditoria: [TRANSITION_AUDIT.md](TRANSITION_AUDIT.md)
- Implementação: [IMPLEMENTATION.md](IMPLEMENTATION.md)
- Freeze: [FREEZE_BEHAVIOR.md](FREEZE_BEHAVIOR.md)
- Cabeçotes: [HEAD_SWITCHING.md](HEAD_SWITCHING.md)
- Native/WASM: [NATIVE_WASM_VALIDATION.md](NATIVE_WASM_VALIDATION.md)
- Custo estimado: [PERFORMANCE_ESTIMATE.md](PERFORMANCE_ESTIMATE.md)

## O que não foi feito

- Nenhum merge, flash, promoção de firmware ou início da F5.
- Nenhum parâmetro público, preset, ID, estrutura ABI ou golden histórico alterado.
- Nenhum buffer novo, thread, lock, alocação na audio task ou timer externo.
- Nenhuma alteração em Tone por conveniência: o mecanismo existente foi
  inspecionado e a decisão registrada é `ALREADY_SATISFACTORY`.
- Nenhuma alegação de performance física: benchmarks Native são apenas
  indicadores. Falta campanha na placa (DSP P50/P95/P99/peak, full-work,
  memória, I2S, overruns e comandos sob carga).

## Reprodução

```sh
python docs/evidence/dsp-reform-f4/run.py build
python docs/evidence/dsp-reform-f4/run.py baseline
python docs/evidence/dsp-reform-f4/run.py measure
python docs/evidence/dsp-reform-f4/run.py blocksize
python docs/evidence/dsp-reform-f4/run.py render
python docs/evidence/dsp-reform-f4/run.py package
```

Build explícito: CMake Release com `-ffp-contract=off`,
`HYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING=1`,
`HYDRA_DSP_REFORM_F2_VARIANT=2`, `HYDRA_DSP_REFORM_F3_VARIANT=0` e
`HYDRA_DSP_REFORM_F4_TRANSITIONS=0|1`. Nenhum arquivo F0.2/F1/F2/F3 ou golden
foi sobrescrito.

## Limitações e pendências

- **Publicação:** o pacote local está verificado (52/52 WAVs, SHA-256), mas o
  artifact público depende do workflow `dsp-reform-f4.yml`; até a execução da
  CI o estado é `LOCAL_PACKAGE_VERIFIED_PENDING_CI`.
- **Físico:** nenhuma placa foi usada. DSP P50/P95/P99/peak, full-work,
  memória, I2S e overruns continuam `PENDING_PHYSICAL`.
- **WASM CTest completo:** bloqueado neste Windows por permissão de diretório
  temporário do emcc; o runtime WASM foi qualificado por build direto,
  comparador estrito 0/0 e contrato F4 PASS. Ver
  `NATIVE_WASM_VALIDATION.md`.
- **Tone:** dependência de tamanho de bloco é pré-existente e idêntica em
  F4=0/F4=1 (20 ms por bloco). Não foi alterada.
- **Drive:** a rampa melhora o primeiro evento 0→100, mas não todos os
  sentidos de variação; a escuta decide se o gesto é preferível.
- **Freeze:** a semântica de gate de gravação não foi redesenhada.

## Recomendação para a F5

A F4 entrega o gesto de cabeçote e o release do Freeze com evidência objetiva
e um pacote A/B curto. Recomenda-se:

1. escuta humana do pacote e decisão de manter/ajustar/remover as rampas de
   Drive e Freeze;
2. campanha física F4=0 vs F4=1 na mesma placa, com comandos sob carga;
3. somente depois, iniciar a F5 (tempo/tap/MIDI/varispeed) preservando a
   referência Vintage e o default F4=0.

Nenhuma dessas etapas está autorizada por este relatório.

