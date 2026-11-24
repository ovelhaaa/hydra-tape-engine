# QualificaÃ§Ã£o Native/WASM F2

Builds explÃ­citos por candidata, incluindo target web real e runners de testes.
Limites preservados max_absâ‰¤1e-5/RMSEâ‰¤1e-6, input e LUT exatos. Primeiras
execuÃ§Ãµes locais: as trÃªs paridades sÃ£o bit-exact (0/0). Resultados completos
nos JSONs da bancada/CI.

C0 confrontada com dez hashes congelados F1 e com uma biblioteca compilada
da fonte F1 com a mesma toolchain: bit-exact. O novo contrato de wrap tambÃ©m
confronta a resposta inteira com F1: energia L2.4174529244191945eâˆ’5,
R6.1086025198164862eâˆ’6 no Windows; hashFNV1a64 `09add861dd5ec3c1`.
Isto caracteriza C0 sem criar um piso arbitrariamente reduzido.

CTest original permanece visÃ­vel: a assertion sonora A em regression_core:255
nÃ£o corresponde a C0. A cÃ³pia diagnÃ³stica avalia todas as assertions e sÃ³
qualifica a falha conhecida, rejeitando qualquer outra. NÃ£o declara o CTest
original integralmente verde. Novo contrato exige igualdade exata full/split,
finitude e resposta nÃ£o silenciosa nos dois canais. Probe adicional compara
readHistory ao endereÃ§amento modular independente em todas as posiÃ§Ãµes do
buffer com delays fracionÃ¡rios, incluindo guard/wrap. Nenhum golden regenerado.

Preservados ABI1.7/38, sÃ­mbolos e consumidor legado, Reverse/Warp,
Multi-Head/Ping-Pong, Tap/MIDI/transport, LUT e presets. Goldens A nÃ£o usados
como expectativa sonora C. ProduÃ§Ã£o A e controle negativo Warp serÃ£o comparados
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
