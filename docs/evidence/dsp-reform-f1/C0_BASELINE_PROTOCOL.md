# C0 — protocolo reproduzível

Fonte/branch: `18d92369aa7ecb30890c9509e552526240f4ba30`,
`codex/dsp-reform-f0-2-reproducible-baseline`; branch de trabalho F1 indicada no
README. Core/include idênticos à #58 `d2d36dd`, sem Aging/Motion/Studio.
Macro C **comprovada no compile_commands**, todos os targets desta build C0,
incluindo DLL, trace Native/WASM e target web. A permanece padrão fora dela.

Ambiente de referência: MinGW UCRT GCC 14.2.0 r2, CMake 4.1.1, Emscripten
3.1.62, Python 3.14.0, NumPy 2.4.1, SciPy 1.17.0, Node 24.11.1 (SDK 22.16.0).
Versões, flags, seeds e hashes em `c0_baseline_manifest.json`/`validation.json`.
DLL SHA256: `5c3b06e9512d0096ff6767722f7311fe8edf30e9dec6cd9201d0ddb9242067c3`.
Receita: adaptação isolada de F0.2 `baseline.py`; nenhuma alteração na A.

```powershell
cmake -S . -B build-c0-native -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_CXX_FLAGS=-DHYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING=1
cmake --build build-c0-native -j 6
python docs/evidence/dsp-reform-f1/baseline.py render --library build-c0-native/libhydra_abi_test_library.dll --output artifacts/dsp-reform-f1/replay1
python docs/evidence/dsp-reform-f1/baseline.py render --library build-c0-native/libhydra_abi_test_library.dll --output artifacts/dsp-reform-f1/replay2
python docs/evidence/dsp-reform-f1/baseline.py verify --output artifacts/dsp-reform-f1/replay1
python docs/evidence/dsp-reform-f1/baseline.py verify --output artifacts/dsp-reform-f1/replay2
& C:/emsdk/upstream/emscripten/emcmake.bat cmake -S . -B build-c0-wasm -G Ninja -DBUILD_WEB=ON -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_CXX_FLAGS=-DHYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING=1
cmake --build build-c0-wasm -j 6
python tests/core/compare_native_wasm.py --native build-c0-native/hydra_render_trace_native.exe --wasm-js build-c0-wasm/wasm-tests/hydra_render_trace.js --report-json parity.json
ctest --test-dir build-c0-native --output-on-failure -E test_golden
ctest --test-dir build-c0-wasm --output-on-failure -E test_golden
```

Diretórios de render novos obrigatórios. Não executar Python com -O. Verificar
contra manifesto versionado, nunca substituir pelo recém-gerado. Rebuild PE
pode mudar SHA da DLL; `verify --allow-rebuilt-library` permite **somente** essa
diferença e exige todo o áudio/proveniência restante idêntico. Em outro OS/compiler,
registrar resultado independente, não sobrescrever baseline MinGW.

Mesmos dez estímulos, seed global MT19937 20261123 reiniciada por fonte,
44100 Hz/128 frames. Novo handle create_v1_4(44100,2000,8000), prepare/reset,
set IDs crescentes exceto ID29 comando=0 não enviado; internal tempo/play/commit;
sem preroll, processa último bloco curto, destroy por cena. Valores/readback dos
38 IDs constam de cada cena. Impulsos frame0/0.5 ambos canais. Input/output
float32 LE interleaved; PCM24 round-even clip*8388607, sem normalização/dither;
PCM hash separado do RIFF e hash float64 canônico signed24/8388608.

A mesma recipe foi executada em dois processos separados; ambos verificados,
10/10 bit-exact. A não é comparada com C como se devesse ter mesma saída.
O hash da receita, hashes individuais e binário são próprios C0.
Não regenerar goldens A para C. CTest tem a assertion sonora A incompatível
explicitada no README; saída não é ocultada nem tratada como sucesso integral.

## Bancada

```powershell
g++ -std=c++17 -O3 -ffp-contract=off -DHYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING=1 -Iinclude -Idsp/core docs/evidence/dsp-reform-f1/filter_probe.cpp -o artifacts/dsp-reform-f1/filter_probe.exe
# Criar artifacts/dsp-reform-f1/filters antes; probe deve retornar zero.
& ./artifacts/dsp-reform-f1/filter_probe.exe artifacts/dsp-reform-f1/filters
python docs/evidence/dsp-reform-f1/analyze.py --library build-c0-native/libhydra_abi_test_library.dll --artifacts artifacts/dsp-reform-f1
python scripts/phase64_validate_engine.py --library "$PWD/build-c0-native/libhydra_abi_test_library.dll" --output artifacts/dsp-reform-f1/engine
```

`filter_probe.cpp` inclui o core canônico diretamente e instancia seus filtros,
sem duplicar algoritmo nem instrumentar a audio task. Configurações explícitas
speed/age=0/50/100, tone50, bump50. Impulsos unitários 1 s/44100 Hz por estágio;
JSON contém hash do impulso processado, espectro e residual. `analyze.py` separa
filtros do loop C0 completo com impulsos 0.1 e bursts200Hz, feedback50/75/85/100,
20 janelas de 500ms; bandas/decay/pico/RMS/espectro/harmônicos e WAVs.
A observação de regressão é uma cópia diagnóstica com assertions originais,
que continua para registrar todas as falhas; **não é substituto do teste original**.

Qualificação explícita (sem suprimir outras falhas):
```powershell
python docs/evidence/dsp-reform-f1/validate_contracts.py --build build-c0-native --output artifacts/dsp-reform-f1/contracts
```
Mantém CTest original falhando na assertion de energia A; compila/avalia todas
as assertions da cópia diagnóstica e rejeita qualquer falha adicional.
