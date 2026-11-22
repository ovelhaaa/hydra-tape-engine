# Fase 7.2E — Resumo da Investigação de Deadline

Relatório técnico completo: [PHASE7_2E_DEADLINE_INVESTIGATION.md](file:///c:/Users/devx/Documents/PlatformIO/Projects/hydra/docs/PHASE7_2E_DEADLINE_INVESTIGATION.md).

## Achado Principal (Dados Físicos em ESP32-S3 / COM15)

Utilizando a instrumentação dedicada `[env:esp32-s3-classic-c-dltrace]` (`-DHYDRA_PHASE72E_DEADLINE_TRACE=1`, buffer circular estático em RAM interna, zero alocação dinâmica), confirmou-se que:

1. **Causa raiz do pico `Full Work` de `2.918 µs` em T4 (Fase 7.2D):**  
   `memset(g_bench.hist, 0, kBenchHistBins * sizeof(uint32_t))` zerava **32 KB em PSRAM** dentro da `audioTask` (Core 1) exatamente no bloco de transição `BENCH_WARMUP -> BENCH_RUNNING` (`seq=325` em T2: `postUs=650 µs`; `seq=326` em T4: `postUs=759 µs`), enquanto todos os demais `10.540+` blocos registraram `postUs = 4–25 µs`.
2. **Risco latente de automação (`updateParams` / `updateFilters`):**  
   Quando parâmetros eram alterados em tempo real (automação), `MaxParamUpdUs` atingia **459–462 µs** por recálculo redundante de biquads e cache misses de Flash em `updateParams()`, `updateFilters()` e `BiquadFilter::set*()`.
3. **Bug de telemetria:**  
   `perf reset` não zerava `g_bench.maxParamUpdateUs` e disparava `dspParamsDirty = true`.
