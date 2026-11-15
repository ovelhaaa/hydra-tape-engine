# Checklist de release — Accidental Warp V1.3

## Evidência objetiva

- [ ] Registrar o HEAD da PR e a conclusão de cada job do GitHub Actions (pendente execução remota após push final).
- [x] Confirmar Native Release (`11/11` CTest), goldens WASM (`3/3` `-R _wasm$`), integração funcional do Worklet/WASM (`validate_warp_prototype.py`) e build PlatformIO ESP32-S3 (`esp32-s3-devkitc-1`).
- [ ] Confirmar consumidor C legado contra a biblioteca atual (`core_abi_legacy_consumer` e `core_warp_test` aprovados localmente em MSVC Release; AddressSanitizer com Clang aguarda execução em Linux CI).
- [x] Gerar e validar localmente `warp-ab/manifest.json` e os WAVs A/B no commit `c9bc17e` (12 cenários Warp + suíte `listening/` contra `f8d2851`: correlação `1,00000000` nos cenários Warp, `WORST_RMSE = 1,029e-04` em `listening/ping_pong/native.wav`); upload do artefato `accidental-warp-ab` ocorre no job `validate_warp` do CI.
- [x] Registrar status e evidência do teste Native/WASM e da comparação contra o baseline da `main` mantendo os limites existentes (`main` `e4cae43`: `max_abs = 7,436e-6 <= 1e-5`, `RMSE = 2,49805e-6 > 1e-6`; HEAD `c9bc17e`: `max_abs = 8,479e-6 <= 1e-5`, `RMSE = 2,74890e-6 > 1e-6`).
- [x] Registrar telemetria física com firmware identificado (`FW Nov 15 2026 20:18:28`, commit `c9bc17e`, ESP32-S3 rev. 0.2): `0` overruns e `0` falhas I2S em `S01..S11` e nos ensaios sustentados de 60 s e 5 min (`P99 <= 1.917 µs` na varredura e `1.861 µs` / `64,12%` em `S11_FullChain_Normal_5min`). Consulte [Auditoria ESP32-S3](ESP32_S3_PERFORMANCE_AUDIT.md).

## Evidência auditiva

- [ ] Escutar os pares A/B com níveis ajustados e registrar cenários, artefatos e decisão (validação numérica e espectral A/B executada; avaliação auditiva humana pendente).
- [ ] Conferir textura nos quatro modos, transições de automação e toggle, brilho e degradação das repetições, incluindo a cascata longa e o impulso a 95%.

O merge requer revisão das categorias de correção DSP, similaridade numérica e percepção. Divergências pendentes precisam de resolução ou justificativa aprovada na PR. Consulte o [protocolo de validação](HYDRA_2_AUDIO_VALIDATION.md).
