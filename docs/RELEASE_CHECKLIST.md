# Release checklist — estabilidade sonora (offline)

Checklist para releases quando não houver validação imediata no ESP32.

## 1) Evidência objetiva (obrigatória)

- [x] CI principal verde (native 10/10 testes + web 21/21 testes + equivalência).
- [x] Suite offline A/B executada com sucesso para todos os 12 cenários-chave (manifest.json aprovado).
- [x] Pacote de revisão gerado com áudios comparativos em `tests/output/warp_ab_validation/`.
- [x] Goldens preservados 100% para modo forward e saturator.
- [x] ABI pública preservada (`core_abi_symbols` aprovado, suporte explícito a `hydra_dsp_set_params_v1_2`).
- [x] Documentação sincronizada (`ACCIDENTAL_WARP_SPEC.md`, `HYDRA_2_AUDIO_VALIDATION.md`, `HYDRA_2_DSP_ARCHITECTURE.md`, `HYDRA_2_ROADMAP.md`).
- [x] Firmware ESP32-S3 compilado com sucesso (PlatformIO: RAM 7.8%, Flash 3.3%) e instrumentado para telemetria em microssegundos.

## 2) Evidência perceptual (obrigatória)

- [ ] Escuta A/B por pelo menos 1 revisor usando os pares WAV gerados.
- [ ] Escuta do arquivo `diff.wav` para checar artefatos inesperados.
- [ ] Registro textual no PR/release indicando: cenário auditado, resultado, e decisão.

## 3) Critério de aceite

Uma build é aceita somente quando **as duas evidências** abaixo existem:

1. **Objetiva:** métricas de equivalência dentro de tolerância e null-test sem falhas.
2. **Perceptual:** revisão A/B documentada, sem regressão sonora relevante reportada.

Se qualquer item acima falhar, a release deve ser bloqueada até correção ou justificativa formal aprovada.
