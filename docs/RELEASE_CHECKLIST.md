# Release checklist — estabilidade sonora (offline)

Checklist para releases quando não houver validação imediata no ESP32.

## 1) Evidência objetiva (obrigatória)

- [ ] CI principal verde (native + wasm + equivalência).
- [ ] Suite offline A/B executada com sucesso para todos os cenários-chave.
- [ ] Pacote de revisão gerado com pares `native.wav` vs `wasm.wav` e `diff.wav` (null-test).
- [ ] Goldens atualizados **somente** se a mudança sonora for intencional, com justificativa.
- [ ] ABI pública preservada (`core_abi_symbols`) ou versionada explicitamente.
- [ ] Documentação sincronizada (`README`, docs técnicas e release notes).

## 2) Evidência perceptual (obrigatória)

- [ ] Escuta A/B por pelo menos 1 revisor usando os pares WAV gerados.
- [ ] Escuta do arquivo `diff.wav` para checar artefatos inesperados.
- [ ] Registro textual no PR/release indicando: cenário auditado, resultado, e decisão.

## 3) Critério de aceite

Uma build é aceita somente quando **as duas evidências** abaixo existem:

1. **Objetiva:** métricas de equivalência dentro de tolerância e null-test sem falhas.
2. **Perceptual:** revisão A/B documentada, sem regressão sonora relevante reportada.

Se qualquer item acima falhar, a release deve ser bloqueada até correção ou justificativa formal aprovada.
