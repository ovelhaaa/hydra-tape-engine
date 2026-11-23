# F0.2 — Protocolo de reprodução

Referência offline independente, rota **A de produção**, fonte congelada
`3cc6c1b5f4ce8b93d7665744d266a82cb42e80e2`. Essa revisão da #64 contém
documentos; DSP/build/testes são idênticos à main `6d94af5` e à auditoria
`6849edf`. Não usar fontes das PRs #58–#62. Não substituir F0 ou goldens.

## Ambiente e build

Windows 10 build 19045; MinGW UCRT GCC 14.2.0 r2, CMake 4.1.1;
Python 3.14.0, NumPy 2.4.1, SciPy 1.17.0. Binário utilizado:
SHA-256 `8f6836c77368a3554a2a6c50ec681aa58d8e09b09758847d4d85f596d91724dc`.
As versões e os hashes das fontes também estão no manifesto. NumPy usa o
gerador global legado MT19937, reiniciado com 20261123 antes de cada fonte.
As seeds internas estão no manifesto e no código congelado; nenhuma seed nova
é injetada na ABI. Não presumir hashes idênticos em outras versões/plataformas.

Na branch F0.2, com toolchain acima no PATH, em PowerShell:

```powershell
python -m pip install numpy==2.4.1 scipy==1.17.0
cmake -S . -B build-native -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-native -j 6
python docs/evidence/dsp-reform-f0-2/baseline.py render --library build-native/libhydra_abi_test_library.dll --output artifacts/dsp-reform-f0-2/replay1
python docs/evidence/dsp-reform-f0-2/baseline.py render --library build-native/libhydra_abi_test_library.dll --output artifacts/dsp-reform-f0-2/replay2
python docs/evidence/dsp-reform-f0-2/baseline.py verify --output artifacts/dsp-reform-f0-2/replay1
python docs/evidence/dsp-reform-f0-2/baseline.py verify --output artifacts/dsp-reform-f0-2/replay2
```

Cada diretório de render deve ser novo. O script não sobrescreve referências.
O verificador consulta o manifesto versionado, não aceita o manifesto recém-gerado
como sua própria referência. Verifica input float32, output float32, PCM, WAV,
parâmetros e proveniência. Não executar Python com `-O`: as verificações usam
assertions. Copiar um manifesto novo sobre o congelado não é reprodução.

O hash de uma DLL recompilada pode mudar por metadados PE/identidade de build.
Nesse caso, conservar o hash novo e usar `verify --allow-rebuilt-library`:
essa opção mostra ambos os hashes e permite **somente** essa diferença, mantendo
todos os hashes de áudio e demais campos obrigatoriamente iguais. Não permite
novas tolerâncias nem equivalência aproximada. Uma divergência de ambiente
requer investigação e relatório próprio, não atualização automática do manifesto.

## Receita efetiva

Dez cenários, na ordem do manifesto: duas guitarras, synth mono, synth poli com
três cabeçotes, bateria ping-pong, percussão irregular, três impulsos (10/20/60 s)
e silêncio com hiss. Fontes musicais de 6 s via `musical_sources.py`, sem
normalização adicional. Os sete inputs não-impulse mantêm os hashes históricos.

Para cada cenário: criar **novo** handle `create_v1_4(44100,2000,8000)`;
`prepare(128,2)`; `reset`; definir IDs crescentes; tempo interno; transport play;
commit explícito; processar blocos de 128 frames, com último bloco curto se
necessário; destruir o handle. Sem preroll, sem estado compartilhado entre
cenários, sem eventos/automação. Nenhuma reutilização de motor.

Todos os 38 IDs aparecem no manifesto, incluindo readback float32. O ID 29 é
**comando de reset**, não estado: seu valor 0 registra ausência de disparo e ele
não passa ao setter (o setter reseta mesmo se receber zero). O reset explícito
anterior define a fronteira de estado. Os demais 37 IDs são definidos.

Impulsos **novos**: frame 0, amplitude `float32(0.5)`, ambos os canais, restante
zero, sem silêncio inicial, duração e feedback conforme cenário. Seus hashes
não coincidem com F0; não são apresentados como receitas históricas.

Inputs/outputs brutos: float32 little-endian, estéreo interleaved.
WAV: RIFF PCM24 little-endian estéreo a 44100 Hz, escala 8388607, clip [-1,1],
round-to-even NumPy, sem dither/normalização; writer histórico de Phase 6.
PCM canônico de análise: inteiros signed24 divididos por 8388608 em float64,
ordem frame/canal, hash little-endian float64. `pcm_sha256` é o payload `data`
original; hashes PCM e contêiner são independentes.

## Diagnóstico histórico separado

Preservar o checkout original com os WAVs e a DLL histórica
`5222a9b60a5b8f35cdad1e217f25f8be7e9ca9160531529b692cc4dd999cb929`.
Não usar a DLL nova para alegar reprodução do probe histórico.

```powershell
python docs/evidence/dsp-reform-f0/reproduce_probe.py --source-root ../hydra --library ../hydra/build-native/libhydra_abi_test_library.dll --output artifacts/dsp-reform-f0-2/probe-new
python docs/evidence/dsp-reform-f0-2/baseline.py diagnose --historical-root ../hydra --probe artifacts/dsp-reform-f0-2/probe-new --output artifacts/dsp-reform-f0-2/comparison-new.json
python docs/evidence/dsp-reform-f0-2/differential.py --library ../hydra/build-native/libhydra_abi_test_library.dll --historical-root ../hydra --output artifacts/dsp-reform-f0-2/hypotheses-new
```

O probe retorna 1 quando há divergência, como documentado na F0.1. O diagnóstico
do relatório usou o probe F0.1 já preservado e conferiu seus sete hashes com
`reproduction-probe.json`; a execução controle diferencial regenerou a guitarra
com o mesmo hash do probe. Comparação compensada é somente diagnóstico: busca
lag de ±44100 frames pela soma de correlação L/R e estima ganho por mínimos
quadrados; mantém também métricas brutas e prefixos 128/512/4096. Nenhuma
correlação, alinhamento ou ganho autoriza classificar equivalência fora dos
limites brutos `max_abs <= 1e-5`, `RMSE <= 1e-6`.

## Gates executáveis

```powershell
ctest --test-dir build-native --output-on-failure
ctest --test-dir build-native -V -R test_golden
& C:/emsdk/upstream/emscripten/emcmake.bat cmake -S . -B build-wasm -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_WEB=ON -DBUILD_TESTING=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-wasm -j 6
ctest --test-dir build-wasm --output-on-failure -R '_wasm$'
python tests/core/verify_numerical_inputs.py --native build-native/hydra_render_trace_native.exe --wasm-js build-wasm/wasm-tests/hydra_render_trace.js --output artifacts/dsp-reform-f0-2/input-lut.json
python tests/core/compare_native_wasm.py --native build-native/hydra_render_trace_native.exe --wasm-js build-wasm/wasm-tests/hydra_render_trace.js --report-json artifacts/dsp-reform-f0-2/parity.json
python scripts/phase64_feedback_stability.py --output artifacts/dsp-reform-f0-2/feedback-replay
python scripts/phase64_validate_engine.py --output artifacts/dsp-reform-f0-2/engine-replay
```

Emscripten usado: 3.1.62; Node no PATH: 24.11.1 (emulador CMake configurado pelo
SDK: 22.16.0). A comparação existente cobre cinco cenários; não alegar que ela
compara os dez renders F0.2. Hardware e escuta de candidatos são gates futuros.

## Arquivos grandes e obtenção

WAVs e float32 ficam em `artifacts/dsp-reform-f0-2/`, ignorado pelo Git.
Pacotes locais versionados por hash: [archive_manifest.json](archive_manifest.json).
**URL pública ainda não disponível** (`artifact_url: null`); os caminhos locais
não constituem hospedagem. Publicação externa permanece pendência de distribuição,
sem substituir a receita executável. O pacote inclui DLL e fontes de receita;
se não estiver disponível, recompilar a fonte congelada e verificar como acima.
Não apagar o pacote F0 histórico: a receita antiga continua desconhecida.
