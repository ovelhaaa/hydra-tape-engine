# Release Notes Template (Áudio)

## Release

- Versão:
- Data:
- Commit/Tag:
- Responsável:

## Resumo sonoro

- Objetivo da alteração de som:
- Escopo (parâmetros/algoritmos afetados):
- Risco esperado (baixo/médio/alto):

## Mudanças de DSP

- [ ] Delay (`delayTimeMs`, cabeças, modo musical)
- [ ] Feedback
- [ ] Drive/Saturação
- [ ] Tone/Coloração
- [ ] Modulação (wow/flutter)
- [ ] Ruído/Dropouts
- [ ] Reverb/Spring
- [ ] Outro:

Descrição detalhada:

## Evidência objetiva

- CI:
  - [ ] native
  - [ ] wasm
  - [ ] equivalência native vs wasm
- Goldens:
  - [ ] sem atualização
  - [ ] atualizados com justificativa
- ABI:
  - [ ] sem quebra
  - [ ] quebra intencional + versionamento

### Métricas A/B por cenário

| Cenário | max_abs | rmse | null_test_energy | Status |
|---|---:|---:|---:|---|
| baseline_glue |  |  |  |  |
| short_slap_bright |  |  |  |  |
| feedback_edge |  |  |  |  |
| saturated_dark |  |  |  |  |
| modulated_space |  |  |  |  |

## Evidência perceptual

- Revisores:
- Ambiente de escuta (fones/monitores):
- Resultado A/B:
- Resultado escuta do `diff.wav`:
- Regressões percebidas:

## Compatibilidade e documentação

- Impacto em hosts (native/web/ESP32):
- Docs atualizadas:
  - [ ] README
  - [ ] docs/ técnicas
  - [ ] changelog/release notes

## Decisão

- [ ] Aprovado para release
- [ ] Bloqueado
- Justificativa:
