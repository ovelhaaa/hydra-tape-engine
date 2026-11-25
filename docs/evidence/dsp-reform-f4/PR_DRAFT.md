# PR draft — F4 performance transitions

**Título:** experiment(F4): opt-in vintage-preserving performance transitions

**Base:** `experiment/dsp-reform-f3-saturation` @
`29f50258f1ac47139ac8f1fdb21aa59424ca9a75` (PR #68)
**Head:** `experiment/dsp-reform-f4-performance-transitions`
**Commits:** `875b130` (DSP, tests, docs, CI), `abbce44` (evidence freeze)

## Status

**BLOCKED_FOR_HUMAN_LISTENING** somente na publicação: este ambiente não tem
credenciais de push (`SEC_E_NO_CREDENTIALS`). O pacote local está verificado
(13 cenas, 52 WAVs, 68 arquivos com SHA-256); o workflow
`.github/workflows/dsp-reform-f4.yml` publica o artifact assim que o branch
for enviado. Todo o restante dos critérios está cumprido. Hardware
**PENDING_PHYSICAL**; decisão musical humana pendente. Sem merge, flash ou
promoção.

## O que muda

Opt-in `HYDRA_DSP_REFORM_F4_TRANSITIONS=1` (default 0):

- cabeçotes: crossfade de 10 ms nos ganhos 0/1 existentes, normalização
  coerente, mesmas posições de leitura, sem leitor/buffer novo;
- Freeze: gate de escrita preservado; release com fade-in de 12 ms, sem
  sobrescrever o conteúdo congelado;
- Mix/Drive: rampas lineares de 10 ms apenas nos ganhos envolvidos;
- Tone: `ALREADY_SATISFACTORY`, sem código novo — o `pbTransition` de 20 ms
  existente já protege os coeficientes.

Nenhum parâmetro público, preset, ABI, golden, coeficiente Vintage,
saturação, compressor ou modelo magnético foi alterado.

## Evidência

| Verificação | Resultado |
|---|---|
| F4=0 vs manifesto F3-A | **10/10 bit-exact** |
| F4=1 estático vs F4=0 | **10/10 bit-exact** |
| Blocos 1/37/64/128/256 (C++) | **max_abs = 0** |
| Native/WASM 5 cenários, 1e-5/1e-6 | **max_abs = 0, RMSE = 0, PASS** |
| Contrato F4 WASM (Node) | **PASS**, métricas idênticas ao Native |
| Head 1→3, passo de 1 ms | 0,03800 → **0,00527**; HF +9,7 → −1,1 dB |
| Mix 0→100, passo de borda | 0,03964 → **0,00027** |
| Freeze release, HF no arrivo | +5,4 → **−18,9 dB** |
| Drive 0→100, passo no arrivo | 0,04013 → **0,01985** |
| `.text` Native | 84.160 → 85.960 B (+2,1%); data/bss 0 |
| Estado novo | ~64 B no `Impl`; zero buffers novos |

Limitações registradas: Drive melhora o primeiro evento mas não todos os
sentidos; Freeze com Reverse tem benefício não demonstrado; Tone tem
dependência de bloco pré-existente; CTest completo WASM e físico pendentes.

## Revisão

Ler, nesta ordem: `README.md`, `TRANSITION_AUDIT.md`, `HEAD_SWITCHING.md`,
`FREEZE_BEHAVIOR.md`, `AUTOMATION_MEASUREMENTS.md`,
`NATIVE_WASM_VALIDATION.md`, `LISTENING_GUIDE.md`,
`PERFORMANCE_ESTIMATE.md`, `render-manifest.json`.

Não fazer merge. O resultado musical continua sujeito à escuta humana.
