# Hydra Vintage Live v0.1 — integração

## Preflight e autorização

Pedido explícito de integração F4 + F5-B para bancada, em 25/11/2026.
Não inicia F6/F7 nem promove defaults. A decisão musical indicada pelo usuário
seleciona C=1/F2=2/F3=0/F4=1/F5=1. Hardware e escuta desta combinação permanecem
PENDING_PHYSICAL; não se exige nova escolha estética para entregar o experimento.

HEADs remotas confirmadas por fetch antes de editar:

- main: `7d8c40a3a925082995f919c0d7c12aa8978d5d21`, merges #63–#68 presentes.
- F4/#69: `aa2178bde97f6be8ff383d8953dec6ed58f95d5c`.
- F5/#70: `d175d7a233c5f794e27a1ce08b444b276f691eec`.

Auditados: AGENTS.md, auditoria musical, roteiro G0/F4/F5, reconciliação F0.1,
consolidação F0 e manifesto/protocolo F0.2; código e contratos F4/F5,
instrument_feedback.py, phase64_feedback_stability.py, workflow Reverse,
CMake/PlatformIO e identidade/CLI/contadores do firmware. Os documentos F0
são históricos: READY_FOR_DSP_EXPERIMENT não promove C. Esta integração usa
a seleção expressa do pedido, sem trazer código #58–#62 por merge.
API permanece 1.7/38; não incorpora as APIs 1.8/47 ou 1.9/49.

Mapa anterior à edição, na HEAD F5 indicada: `tape_core.cpp:401–580`
(glide/estados), `628–672` (parâmetros), `1493–1495` (magnetismo), `1567`
(Freeze), `tape_core.hpp:89–109` (diagnóstico F5), `CMakeLists.txt:62–131`
(alvos F5), `platformio.ini:111–135` (ambientes F5).
Rota canônica: main → adaptador ESP32 → API C → TapeCore.

## Reconciliação

Dois merges normais preservam os pais experimentais. F4 não conflitou em DSP;
o merge F5 combinou automaticamente os trechos separados do mesmo TapeCore.
Conflitos manuais em CMake e PlatformIO foram resolvidos preservando ambos os
conjuntos de testes/ambientes, em vez de escolher uma branch inteira.
Headers/API C e workflows não tiveram conflito textual. Há somente um motor,
com os buffers compartilhados Tape/Reverse/Warp existentes.

Ganhos F4 e posições F5 mantêm seus próprios estados e atualização por amostra.
Retarget de ganho parte do estado atual; retarget temporal usa o erro corrente.
O transporte continua exato, sem rampa. Reset/reativação seguem os caminhos
originais de priming de cada funcionalidade. Freeze conserva a escrita bloqueada
e a liberação F4 atua antes do magnetismo. Não muda saturação/feedback estático.

Hipótese musical: crossfade de seleção e glide de posição podem coexistir sem
resetar a fita ou o transporte. A comprovação objetiva inclui comandos com
espaçamento de 88/176/265 frames (2/4/6 ms arredondados em 44,1 kHz), reversões
de máscara, BPM, Freeze e Mix/Drive simultâneos. Render estéreo deve ser idêntico
para blocos 1/37/64/128/256 com os mesmos eventos nas mesmas amostras.

Custo esperado: soma dos estados escalares F4/F5, sem alocação/leitor/buffer
adicional. F5 registra 56 bytes de estado; F4 mantém rampas de 10 ms e liberação
de 12 ms. Regime estável conserva os atalhos já implementados. Nenhuma medição
desktop é tratada como custo ESP32; medir P99/full-work e transições na placa.

## Instrumentação

Execução histórica F4 `45499484329`, job `138818635587`: regressões Native,
WASM e paridade passaram; o passo de validação de feedback falhou antes dos
builds ESP32. O script procurava a expressão antiga `mag.process(recSig,...)`.
F4 usa `writeSig` com ganho de liberação e um bloco de escrita condicionado.

O ponto explícito `HYDRA_TEST_POINT_MAGNETIC_WRITE` fica após o único store
magnético. A cópia de diagnóstico lê o valor já armazenado; quando Freeze está
ativo registra zero (nenhuma escrita), como o probe anterior. Não altera
aritmética, ordem ou número de chamadas a mag.process. Exige exatamente um
ponto; a ausência/duplicação falha explicitamente. Produção contém só um
comentário, sem memória ou instrução extra. O probe continua limitado a 8192
amostras e ao canal esquerdo. `--define` permite compilar o probe com as mesmas
flags da biblioteca Live; equivalência é checada contra a saída completa,
bit a bit, em janelas aos 5 e 55 segundos e 44,1/48 kHz.

## Gates e entrega

Workflow dedicado: `.github/workflows/hydra-live-v01.yml`. Produção A é testada
sem flags experimentais globais; targets explícitos cobrem 00/10/01/11.
Native/WASM usa limiares originais max_abs ≤1e-5 e RMSE ≤1e-6, sem goldens novos.
Manifesto F3-A congelado e dez cenários verificam referência e estática.
Feedback/Reverse de 60 s roda em produção e Live com instrumentação equivalente.
Somente após os gates o job firmware compila ambos os ambientes, confere SHA,
flags e hashes, e publica `hydra-vintage-live-v0.1-firmware`.
Relatórios e tamanhos efetivos acompanham o artifact; falha de gate impede
publicação como pronto. Não há merge/flash automático.

O relatório local inicial passou Native 29/29 e WASM 29/29. Matriz estática:
10/10 bit-exact entre configurações e com F3-A; musical estático bit-exact.
Resultados finais e SHA são os do run/artifact validado, não os destas notas
de preparação. Roteiro: `docs/HYDRA_LIVE_V01_HARDWARE_TEST.md`.
