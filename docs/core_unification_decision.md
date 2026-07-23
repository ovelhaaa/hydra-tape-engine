# Decisão de unificação do núcleo DSP

## Insumos objetivos

O relatório estático identifica divergências de alto impacto em profundidade de wow/flutter, head bump, mistura `dryWet`/`delayWet`, ping-pong, reverse state compartilhado e defaults de spring. Consulte `docs/divergence_report.md`. O pacote reproduzível é gerado por:

```bash
cmake --build build --target hydra_core_vs_firmware_runner
python3 tests/core/build_ab_review_core_vs_firmware.py --runner build/hydra_core_vs_firmware_runner
```

O pacote ordena todas as combinações por RMSE e conserva `native.wav`, `firmware.wav`, `diff.wav` e `summary.json` por cenário.

## Espaço de escuta manual — Alexandre

| Cenário/caminho do pacote | Observação subjetiva | Aprovado para fidelidade? |
|---|---|---|
| _preencher após gerar pacote_ |  |  |

## Recomendação técnica (não é decisão de fidelidade sonora)

Estruturalmente, a recomendação é **TapeCore (`dsp/core`)**: possui API C estável, goldens, regressões native/wasm, reset determinístico, controle `delayWet` explícito e não contém o estado reverse compartilhado do firmware. Isto não declara que seu som é preferível; a fidelidade deve ser aprovada depois da escuta A/B acima. `FreeverbEngine`, `FrippEngine` e `BubblesEngine` permanecem módulos externos, não candidatos a entrar automaticamente na API pública.

## Aprovação explícita exigida antes da Fase C.2

- Núcleo escolhido: ______________________________
- Aprovado por Alexandre: _________________________
- Data: __________________________________________
- Justificativa/fidelidade validada: ______________

Sem estes campos preenchidos, nenhuma refatoração do firmware, atualização de goldens, mudança de ABI ou congelamento de comportamento deve ser executado.
