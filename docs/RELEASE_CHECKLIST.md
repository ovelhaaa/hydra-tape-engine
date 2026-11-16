# Checklist de release — Accidental Warp V1.3

## Evidência objetiva

- [x] Registrar o HEAD de código auditado (`d942a7b`) e a conclusão de cada job no [gate de release](ACCIDENTAL_WARP_RELEASE_GATE.md).
- [x] Confirmar Native Release (`11/11` CTest), goldens WASM (`3/3` `-R _wasm$`), integração funcional do Worklet/WASM (`validate_warp_prototype.py`) e build PlatformIO ESP32-S3 (`esp32-s3-devkitc-1`).
- [x] Confirmar consumidor C legado contra a biblioteca atual (`core_abi_legacy_consumer` e `core_warp_test` aprovados em Native Release e com AddressSanitizer no job `validate_warp`).
- [x] Gerar e validar localmente `warp-ab/manifest.json` e os WAVs A/B no commit `c9bc17e` (12 cenários Warp + suíte `listening/` contra `f8d2851`: correlação `1,00000000` nos cenários Warp, `WORST_RMSE = 1,029e-04` em `listening/ping_pong/native.wav`); upload do artefato `accidental-warp-ab` ocorre no job `validate_warp` do CI.
- [x] Reproduzir a falha Native/WASM no HEAD de código e na `main` com o mesmo toolchain e registrar os resultados Linux e Windows no [gate de release](ACCIDENTAL_WARP_RELEASE_GATE.md#ci-e-proposta-de-exceção).
- [ ] Registrar decisão do responsável pelo merge sobre a exceção Native/WASM. O job estrito permanece falho.
- [x] Registrar telemetria física com firmware identificado (`FW Nov 15 2026 20:18:28`, commit `c9bc17e`, ESP32-S3 rev. 0.2): `0` overruns e `0` falhas I2S em `S01..S11` e nos ensaios sustentados de 60 s e 5 min (`P99 <= 1.917 µs` na varredura e `1.861 µs` / `64,12%` em `S11_FullChain_Normal_5min`). Consulte [Auditoria ESP32-S3](ESP32_S3_PERFORMANCE_AUDIT.md).
- [x] Revalidar o firmware com a correção de PCM e Freeverb (`b9fbbd3`) no ESP32-S3. Em cinco minutos de cadeia completa, P99 de 1.875 µs, pico de 2.155 µs, heap e PSRAM estáveis e `0` overruns ou falhas I2S.

## Evidência auditiva

- [x] Registrar o parecer humano para os 35 pares do pacote avaliado. O [registro de escuta](ACCIDENTAL_WARP_LISTENING_SIGNOFF.md) contém 34 aprovações e uma aprovação com ressalva.
- [x] Conferir os WAVs do sign-off pelo SHA-256 do pacote de escuta e registrar a [procedência por render](../tools/listening-lab/provenance/release-gate-tapecore.json).
- [x] Reproduzir os transientes de Tape Clean e Multi-Head nos WAVs Native e isolar a comutação abrupta do bias magnético.
- [x] Aplicar a rampa de ganho e medir a redução dos transientes nos mesmos estímulos do sign-off.
- [x] Registrar a escuta humana do [pacote Magnetic Bias De-Click](MAGNETIC_BIAS_DECLICK_SIGNOFF.md), com 17 aprovações em 17 pares.

O merge requer revisão das categorias de correção DSP, similaridade numérica e percepção. Divergências pendentes precisam de resolução ou justificativa aprovada na PR. Consulte o [protocolo de validação](HYDRA_2_AUDIO_VALIDATION.md).
