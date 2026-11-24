# F2 — Musical Feedback Calibration

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
Estado de entrega atualizado após verificar o pacote; nenhum sucesso de CI
isolado constitui aprovação musical ou física.

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
