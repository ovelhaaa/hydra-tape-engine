# F2 â€” Musical Feedback Calibration

Fase experimental autorizada: Classic C0 Ã— Warm Tape Ã— Vintage Progressive.
Base congelada F1 `f3281d0e9230ad151bcad94c574042adc7996467` (#66), empilhada
sobre F0.2/#65 e F0.1/#64; todas as PRs #58â€“#66 continuam abertas/draft, sem
merge (snapshot nesta pasta). A permanece produÃ§Ã£o; nenhuma integraÃ§Ã£o de
#59/#61/#62, promoÃ§Ã£o, merge ou flash nesta fase. Branch:
`experiment/dsp-reform-f2-feedback-calibration`.

ImplementaÃ§Ã£o: apenas trÃªs calibraÃ§Ãµes dos filtros existentes, selecionadas por
`HYDRA_DSP_REFORM_F2_VARIANT=0|1|2`, dependentes da Classic C explÃ­cita.
API1.7/38, presets e buffers Tape/Reverse/Warp preservados. F0.2/F1 congeladas.
Hardware atual: **PENDING_PHYSICAL**. AudiÃ§Ã£o/decisÃ£o: **PENDING_HUMAN**.
Estado de entrega atualizado apÃ³s verificar o pacote; nenhum sucesso de CI
isolado constitui aprovaÃ§Ã£o musical ou fÃ­sica.

Ver [implementaÃ§Ã£o](IMPLEMENTATION.md), [filtros](SPECTRAL_COMPARISON.md),
[estabilidade](FEEDBACK_STABILITY.md), [Native/WASM](NATIVE_WASM_VALIDATION.md),
[guia de audiÃ§Ã£o](LISTENING_GUIDE.md) e [ficha](LISTENING_DECISION_TEMPLATE.md).

ReproduÃ§Ã£o (mesma toolchain para todas as variantes): build CMake Release com
`-ffp-contract=off`, `CMAKE_CXX_FLAGS="-DHYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING=1 -DHYDRA_DSP_REFORM_F2_VARIANT=N"`,
Native em `build/f2-N-native`, Emscripten3.1.62 em `build/f2-N-wasm`, BUILD_WEB=ON,
compile_commands habilitado. Workflow `dsp-reform-f2.yml` contÃ©m comandos exatos.
Python3.14, numpy2.4.1, scipy1.17.0. NÃ£o execute Python com -O.

```
python docs/evidence/dsp-reform-f1/prepare_audition.py --output artifacts/dsp-reform-f2/inputs
python docs/evidence/dsp-reform-f2/run.py baseline
python docs/evidence/dsp-reform-f2/run.py variant --variant 0
python docs/evidence/dsp-reform-f2/run.py variant --variant 1
python docs/evidence/dsp-reform-f2/run.py variant --variant 2
python docs/evidence/dsp-reform-f2/run.py package
```

Use pasta nova de outputs. A aÃ§Ã£o `baseline` compila a fonte congelada F1 com
o mesmo compilador e compara todas as saÃ­das; no Windows exige tambÃ©m os hashes
do manifesto MinGW F1. CI Linux conserva proveniÃªncia prÃ³pria, sem presumir
igualdade de libm entre sistemas. WAVs grandes sÃ³ em artifacts/CI, nunca Git.
SHA-256 em `listening/sha256.json`; gabarito separado `answer-key.json`.

## Evidência local concluída

- C0:10/10 inputs/saídas/readbacks bit-exact com F1; wrap congelado exato.
- Native/WASM: cinco cenários ×três candidatas, max_abs=0/RMSE=0, LUT/input exatos.
- CTest original:16/17 Native,14/15 WASM por candidata; única falha é a assertion
  A de energia conhecida. Contrato C0 específico e observação integral passam.
- Motor:27/27 por candidata. Estresse:154/154 por candidata, incluindo60
  caudas60s/candidata, mais4 caudas compostas Multi-Head+Ping-Pong.
- Produção A:10/10 exact; Warp negativo: três candidatas exact.
- 14 cenas/84WAVs:12 principais16s e duas de estresse60s, oito fontes sintéticas.
  Arquivos de estresse bit-exact com os renders tecnicamente qualificados.
- Player: hashes, áudio do terceiro slot, troca sincronizada, exportação de28
  avaliações, persistência e revelação condicionada:PASS (teste automatizado,
  **não** audição humana).

Dados versionados: `validation-summary.json`, `spectral-summary.json`,
`calibration-search.json`, `hpf-crosscheck-summary.json`,
`native-benchmark-summary.json`, `browser-summary.json`, `render-manifest.json`.
Os JSONs completos por caso, comandos efetivos, logs originais CTest, WAVs e
gabarito separado acompanham o artifact. Presets, ABI e F0.2/F1 inalterados.

**Diferença em relação ao pedido inicial:** B180Hz e C240Hz foram implementadas
e rejeitadas pelo gate60s. Após sweep restrito ao HPF, finais B260/C280Hz
passam a mesma matriz; bumps .6/.8dB e gap C×.75 mantidos. Não esconder essa
recalibração: o ganho de corpo é menor que o inicialmente esperado.

**Limite dinâmico observado:** guitarra full-level fast/new/FB100 já clipa
transitoriamente no monitor C0. Candidatas têm mais samples no teto durante a
entrada; zero teto após a parada e estados internos estáveis. Ver tabela em
`FEEDBACK_STABILITY.md`; isso requer julgamento musical, não promoção automática.
