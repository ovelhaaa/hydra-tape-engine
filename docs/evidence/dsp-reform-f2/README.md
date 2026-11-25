# F2 — Musical Feedback Calibration

**READY_FOR_HUMAN_LISTENING** — pacote final publicado e conferido.
[Baixar 84 WAVs, Listening Lab e relatórios](https://github.com/ovelhaaa/hydra-tape-engine/actions/runs/45312475778/artifacts/14885385251).
CI em `693312c` concluída com sucesso; commits posteriores só registram evidências.
ZIP de 349.948.838 bytes: SHA-256 igual ao digest GitHub, CRC íntegro,
100 entradas SHA verificadas e 84 WAVs PCM24/44.100 Hz/estéreo utilizáveis.
Pico MATCHED ≤ 0,98; diferença RMS máxima entre slots de 1,914e−10 na janela comum.
Gabarito separado. Ver `ci-artifact.json` e `published-artifact-summary.json`.

Fase experimental autorizada: Classic C0 × Warm Tape × Vintage Progressive.
Base congelada F1 `f3281d0e9230ad151bcad94c574042adc7996467` (#66), empilhada
sobre F0.2/#65 e F0.1/#64; todas as PRs #58–#66 continuam abertas/draft, sem
merge (snapshot nesta pasta). A permanece produção; nenhuma integração de
#59/#61/#62, promoção, merge ou flash nesta fase. Branch:
`experiment/dsp-reform-f2-feedback-calibration`.

Implementação: apenas três calibrações dos filtros existentes, selecionadas por
`HYDRA_DSP_REFORM_F2_VARIANT=0|1|2`, dependentes da Classic C explícita.
API1.7/38, presets e buffers Tape/Reverse/Warp preservados. F0.2/F1 congeladas.
Hardware atual: **PENDING_PHYSICAL**. Audição/decisão: **PENDING_HUMAN**.
Pacotes local e CI funcionais: 14 cenas, RAW/MATCHED; browser QA de 28 avaliações
PASS e arquivo publicado baixado e re-hasheado. Nenhum sucesso automático
constitui aprovação musical ou física.

Ver [implementação](IMPLEMENTATION.md), [filtros](SPECTRAL_COMPARISON.md),
[estabilidade](FEEDBACK_STABILITY.md), [Native/WASM](NATIVE_WASM_VALIDATION.md),
[guia de audição](LISTENING_GUIDE.md) e [ficha](LISTENING_DECISION_TEMPLATE.md).

Reprodução (mesma toolchain para todas as variantes): build CMake Release com
`-ffp-contract=off`, `CMAKE_CXX_FLAGS="-DHYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING=1 -DHYDRA_DSP_REFORM_F2_VARIANT=N"`,
Native em `build/f2-N-native`, Emscripten3.1.62 em `build/f2-N-wasm`, BUILD_WEB=ON,
compile_commands habilitado. Workflow `dsp-reform-f2.yml` contém comandos exatos.
Python3.14, numpy2.4.1, scipy1.17.0. Não execute Python com -O.

```
python docs/evidence/dsp-reform-f1/prepare_audition.py --output artifacts/dsp-reform-f2/inputs
python docs/evidence/dsp-reform-f2/run.py baseline
python docs/evidence/dsp-reform-f2/run.py variant --variant 0
python docs/evidence/dsp-reform-f2/run.py variant --variant 1
python docs/evidence/dsp-reform-f2/run.py variant --variant 2
python docs/evidence/dsp-reform-f2/run.py package
```

Use pasta nova de outputs. A ação `baseline` compila a fonte congelada F1 com
o mesmo compilador e compara todas as saídas; no Windows exige também os hashes
do manifesto MinGW F1. CI Linux conserva proveniência própria, sem presumir
igualdade de libm entre sistemas. WAVs grandes só em artifacts/CI, nunca Git.
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

No ZIP extraído, a entrada correta é
`artifacts/dsp-reform-f2/listening/index.html`, servida por HTTP conforme o guia.
O pacote CI tem proveniência Linux própria; os hashes MinGW locais permanecem
separados. Nenhum golden ou baseline foi regenerado para obter esses resultados.
Hardware PENDING_PHYSICAL e decisão musical PENDING_HUMAN continuam explícitos.
