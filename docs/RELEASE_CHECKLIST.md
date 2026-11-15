# Checklist de release — Accidental Warp V1.3

## Evidência objetiva

- [ ] Registrar o HEAD da PR e a conclusão de cada job do GitHub Actions.
- [ ] Confirmar Native Release, goldens WASM, integração funcional do Worklet/WASM e build PlatformIO ESP32-S3.
- [ ] Confirmar consumidor C legado contra a biblioteca atual, AddressSanitizer e testes dos setters versionados.
- [ ] Anexar `accidental-warp-ab` com manifesto e WAVs do HEAD. Esperar 11 cenários nos três motores, com o toggle executado apenas no TapeCore.
- [ ] Registrar status e evidência do teste Native/WASM e da comparação contra o baseline da `main`. Preservar os limites existentes.
- [ ] Registrar telemetria física com firmware identificado quando houver placa ESP32-S3 disponível. O build e a medição de processamento são evidências distintas.

## Evidência auditiva

- [ ] Escutar os pares A/B com níveis ajustados e registrar cenários, artefatos e decisão.
- [ ] Conferir textura nos quatro modos, transições de automação e toggle, brilho e degradação das repetições, incluindo a cascata longa e o impulso a 95%.

O merge requer revisão das categorias de correção DSP, similaridade numérica e percepção. Divergências pendentes precisam de resolução ou justificativa aprovada na PR. Consulte o [protocolo de validação](HYDRA_2_AUDIO_VALIDATION.md).
