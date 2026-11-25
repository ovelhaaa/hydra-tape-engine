# F0.1 — Consolidação da baseline

Data: 2026-11-23. Escopo: auditoria/documentação; nenhuma mudança de DSP, ABI,
golden, áudio, flash ou merge. Decisão: **BLOCKED para iniciar novo experimento DSP**,
por receita sonora incompleta; a consolidação está disponível para revisão.

## Revisões e integração do histórico

`main` remota e local: `6d94af5557f8ec23ef4627af3d0a584f7e5fd517`.
F0 remota e checkout original: `8358cc4d784d7e0a55242aaadd78fdde1b7c0a7e`,
branch `docs/dsp-reform-f0-baseline`. A diferença é um commit com somente
`README.md` e `baseline_renders_manifest.json` desta pasta. Não estava incorporada
à main. Esta PR, derivada diretamente da main em worktree separado, transporta
esses dois arquivos byte a byte (`git show 8358cc4:<path>`), conservando a branch F0.
O README é **relatório histórico**, inclusive seu `READY_FOR_F1`; este documento
é a avaliação posterior que o qualifica. Não fazer merge duplicado da PR F0:
após revisão, registrar que os dois blobs foram incorporados por esta PR, ou
integrar F0 primeiro e retirar apenas as cópias idênticas do diff desta PR.

Auditoria original: `6849edf738c0d60731f51e96f7a16d1deac08cfe`; até a main
atual só entraram os três documentos de governança. Nenhuma fase F1–F11
aprovada/incorporada foi identificada. PRs #58–#62: abertas, draft, sem merge.
SHAs, bases e checks consultados no GitHub estão em [branch-snapshot.json](branch-snapshot.json).

## Revisão das cinco falhas

Mesmos binários Release MinGW 14.2.0 existentes da F0; a DLL conserva SHA-256
`5222a9b60a5b8f35cdad1e217f25f8be7e9ca9160531529b692cc4dd999cb929`.
Core/fontes de teste são idênticos à main. Não confundir esta reexecução com
um novo build limpo. Comandos executados a partir do checkout original:

```powershell
ctest --test-dir build-native --output-on-failure
$env:TEMP=(Resolve-Path build-native).Path
$env:TMP=$env:TEMP
ctest --test-dir build-native --output-on-failure
```

| Caso histórico | Classificação | Evidência F0.1 |
|---|---|---|
| Tap Tempo / snapshot consistency | 3 — concorrência/agendamento; manifestação não reproduzida nesta rodada | PASS no geral e 30/30 isolados. Em `tests/core/tap_tempo_test.cpp:782–826`, produtor pode concluir 50 mil publicações antes de o consumidor entrar no `while(running)`. Zero snapshots não demonstra corrupção DSP. |
| transition characterization | 1 — infraestrutura reproduzida | C++ `listen.cpp:35` não abre input/automation/output em Temp; PASS com TEMP/TMP no build. |
| golden delay_modes | 1 — infraestrutura reproduzida | `failed to open output path`; PASS com TEMP/TMP no build. |
| golden reset_state | 1 — infraestrutura reproduzida | Mesmo mecanismo de abertura; PASS com TEMP/TMP no build. |
| golden param_automation | 1 — infraestrutura reproduzida | Mesmo mecanismo de abertura; PASS com TEMP/TMP no build. |

Resultado geral original F0: **14/19**. F0.1 sem workaround: **15/19**.
F0.1 com ambiente temporário no build: **19/19**, execução completa real,
não deduzida de testes isolados. Logs: [default](f01-ctest-default.log),
[TEMP no build](f01-ctest-workspace-temp.log), [30 Tap](f01-tap-repeat.json).
Não há evidência de falha determinística de DSP nesses cinco casos.

`golden_test.py:43–49` e `listening_review.py:118–125,295` já fecham os arquivos
Python antes do processo filho; portanto **lock específico não está demonstrado**.
O que foi reproduzido é uma falha de acesso/abertura entre runtimes em diretório
temporário do usuário. Não atribuir sem prova a NamedTemporaryFile ou antivírus.

Patch mínimo preparado **em branch/PR separada de infraestrutura**:
`codex/f0-1-test-infrastructure`. (1) handshake na primeira publicação do teste
de snapshots, com espera limitada a 5 s no produtor, sem reduzir 50 mil iterações
nem relaxar zero violações; (2) `ENVIRONMENT TEMP/TMP` no CTest Windows apenas
para os quatro testes que criam temporários. Não altera wrappers, DSP ou goldens.
A mitigação de diretório não pretende diagnosticar definitivamente a política
de permissões do Windows. Não é dependência de áudio desta PR documental.
Build limpo da branch de infraestrutura: **19/19 CTest PASS**, seguido de
**30/30 repetições CTest de Tap Tempo PASS**; reexecução completa também 19/19.
[Log do build de infraestrutura](f01-infra-ctest.log). Nenhuma tolerância mudou.
PR isolada: [#63](https://github.com/ovelhaaa/hydra-tape-engine/pull/63),
HEAD `6f3bc243bf26b36eabc21236f3e193b2c0fef33d`, draft, sem merge.

## Paridade, estabilidade e bias

Reexecutado `compare_native_wasm.py` com
`build-native/hydra_render_trace_native.exe` e
`build-wasm/wasm-tests/hydra_render_trace.js`: cinco cenários,
**max_abs=0, RMSE=0**, tolerâncias preservadas `1e-5 / 1e-6`.
[Resultado novo](f01-equivalence.json). Compile commands Native registram
`-O3 -DNDEBUG -ffp-contract=off`, sem macro C no target de produção A.

27/27 full-engine e 49/49 bias são resultados da execução F0, assim como
as caudas de estabilidade 60 s; não foram novamente executados nesta consolidação.
Sua classificação é **histórico F0**, não validação física nem validação das
variantes experimentais atuais. A ABI legada e Warp/Reverse passaram no CTest
completo desta rodada. Checks atuais de cada PR são evidência CI atribuída ao
SHA respectivo, não testes locais feitos pelo auditor.

## Integridade não equivale a regenerabilidade

Manifesto histórico preservado: SHA-256
`8f45f62319df08311b73aef746ca04bbbae40e9032afecbc7214b1239bf2726c`.
**10/10 WAVs locais conferem**. Verificação automatizada, sem DSP e sem escrita:

```powershell
python docs/evidence/dsp-reform-f0/verify_baseline.py --root C:/caminho/checkout-com-artifacts
```

Os WAVs continuam no checkout original, fora do Git. Não estão incluídos nesta
PR nem se deve supor que esse caminho local seja um arquivo público durável.
Antes de removê-los, arquivar pacote externo imutável com os hashes existentes.

O manifesto guarda seed, taxa, DLL/hash, tipo/duração, overrides e hashes, mas
**não** o mapa completo de parâmetros, configuração de criação/reset/commit,
receita/amplitude/posição/formato dos três estímulos chamados `impulse`, versões
NumPy/SciPy nem o comando gerador específico. `render_phase6_review.py` produz
outro pacote; simplesmente executá-lo não reconstrói estes dez cenários.

Probe documentado em [reproduction-probe.json](reproduction-probe.json):
NumPy seed 20261123 reiniciada por cenário, `get_musical_source(kind,duration,44100)`
e zeros para silêncio reconstruíram **7/7 hashes de estímulo** não-impulse.
Usando `run_simulation()` com sample_rate=44100, RATE global=44100 e overrides
do manifesto, **0/7 WAV hashes coincidiram**, mesmo com a DLL histórica idêntica.
Tentativa reproduzível (saída 1 significa divergência esperada, não PASS):

```powershell
python docs/evidence/dsp-reform-f0/reproduce_probe.py --source-root C:/checkout-historico --library C:/checkout-historico/build-native/libhydra_abi_test_library.dll --output C:/nova-pasta-probe
```

Os parâmetros efetivos da tentativa foram gravados no probe. Isso aponta para
receita ausente; não permite atribuir diferença a compilador ou algoritmo.
Não aceitar esses novos WAVs como substitutos nem atualizar os hashes antigos.

**Gate aberto:** recuperar o gerador original ou estímulos float32 e os mapas
integrais; registrar criação/prepare/reset/commit, block=128, seeds e dependências;
regenerar em pasta nova e exigir 10/10 estímulos/WAVs SHA iguais. Se impossível,
uma nova baseline requer decisão humana explícita e preservação desta histórica.
O verificador entregue testa integridade; não inventa uma receita de reprodução.

## Físico: separar três procedências

- Histórico main A: auditoria ESP32; histórico C otimizada: decisão 7.2E,
  T2 DSP P99 2333 µs/full-work peak 2604 µs. Não são medições desta rodada.
- Atribuído à #61: JSON e serial COM15 encontrados localmente e preservados em
  [physical-61](physical-61). Há 13 registros de benchmark, heap/PSRAM,
  StackMinWords e zero overruns/I2S. Ausências e inconsistências são detalhadas
  na [reconciliação](../../DSP_REFORM_PR_RECONCILIATION.md).
- F0/F0.1 na main atual, #59, #62 e nova qualificação #60/#61:
  **PENDING_PHYSICAL**. Nenhuma serial foi aberta nem placa gravada nesta rodada.

## Handoff

Recomendação musical: investigar primeiro calibração conjunta HPF/head bump/gap
loss com filtros existentes, sem recriar C nem selecionar C3. A recomendação
não autoriza implementação e depende de recuperar a baseline e escolher
explicitamente a rota de referência. Veja ranking e gates na reconciliação.
CPU/RAM incremental desta PR: zero no firmware; nenhum arquivo DSP alterado.
**BLOCKED** para o próximo experimento, não por suposta instabilidade do core,
mas pela receita congelada incompleta. Aprovação de produção das PRs experimentais
continua separadamente bloqueada por escuta/qualificação física pertinentes.
