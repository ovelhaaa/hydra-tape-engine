# Qualificação Native/WASM F2

Builds explícitos por candidata, incluindo target web real e runners de testes.
Limites preservados max_abs≤1e-5/RMSE≤1e-6, input e LUT exatos. Primeiras
execuções locais: as três paridades são bit-exact (0/0). Resultados completos
nos JSONs da bancada/CI.

C0 confrontada com dez hashes congelados F1 e com uma biblioteca compilada
da fonte F1 com a mesma toolchain: bit-exact. O novo contrato de wrap também
confronta a resposta inteira com F1: energia L2.4174529244191945e−5,
R6.1086025198164862e−6 no Windows; hashFNV1a64 `09add861dd5ec3c1`.
Isto caracteriza C0 sem criar um piso arbitrariamente reduzido.

CTest original permanece visível: a assertion sonora A em regression_core:255
não corresponde a C0. A cópia diagnóstica avalia todas as assertions e só
qualifica a falha conhecida, rejeitando qualquer outra. Não declara o CTest
original integralmente verde. Novo contrato exige igualdade exata full/split,
finitude e resposta não silenciosa nos dois canais. Probe adicional compara
readHistory ao endereçamento modular independente em todas as posições do
buffer com delays fracionários, incluindo guard/wrap. Nenhum golden regenerado.

Preservados ABI1.7/38, símbolos e consumidor legado, Reverse/Warp,
Multi-Head/Ping-Pong, Tap/MIDI/transport, LUT e presets. Goldens A não usados
como expectativa sonora C. Produção A e controle negativo Warp serão comparados
separadamente; detalhes finais nos resultados desta fase.

Qualificação local final: Native16/17 e WASM14/15 por candidata; única falha
original é `core_regression` na assertion A conhecida. Observação completa
confirma todas as outras assertions, full/split exato e teste específico de wrap.
Suíte canônica de motor27/27 por candidata;154/154 screens/candidata, incluindo
60 caudas60s, mais4 caudas Multi-Head+Ping-Pong por candidata.
Produção A:10/10 renders bit-exact contra fonte congelada F1 sem macro C.
Warp: três variantes bit-exact no controle negativo musical. Guards de build
inválidos, ABI1.7/38, input/LUT e comandos efetivos de todos os targets DSP
verificados. `tests/fixtures`, presets, host e F0.2/F1 têm diff vazio.

Observação completa também compilada e executada emWASM para as três
candidatas: todas as assertions após a falha de energia original foram
avaliadas; nenhuma falha adicional. `wasm-observation-summary.json` contém
comandos em++ reproduzíveis, stdout integral e identidade full/split L/R.
Isso complementa o CTest original sem alterá-lo nem declarar sua suite verde.
