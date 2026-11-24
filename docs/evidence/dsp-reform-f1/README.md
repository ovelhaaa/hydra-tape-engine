# F1 — Classic C0 experimental congelada

Decisão: **READY_FOR_F2_IMPLEMENTATION**, exclusivamente para experimentação
isolada F2 com filtros existentes. Não autoriza promoção, merge, firmware ou
início automático de F2. C0 é a referência experimental prioritária por decisão
explícita do usuário; A permanece produção. F1-Lean não será implementada.

## Referência e preflight

Branch: `codex/dsp-reform-f1-c0-reference`, worktree `hydra-f1`, derivada de
`18d92369aa7ecb30890c9509e552526240f4ba30` (#65 F0.2). DSP dessa base é
idêntico à main `6d94af5557f8ec23ef4627af3d0a584f7e5fd517`; C0 é ativada
somente nos diretórios de build, por `HYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING=1`.
A #58 em `d2d36dd2b5d0fe06e66aca6d309a532a13001d87` tem **diff vazio em
`dsp/core` e `include`** contra main: identidade de implementação estabelecida
por todo o código canônico e dependências locais, não somente pelo nome da macro.
Suas diferenças de CMake/PlatformIO/web/UI/identity não são incorporadas.
Não alegamos identidade binária entre targets/hosts distintos da #58.
Escolher esta base preserva defaults A e permite reutilizar F0.2 sem merges.

Leitura: AGENTS, auditoria musical, roteiro completo G0/F0–F11, reconciliação,
F0/CONSOLIDATION, F0.2 README/protocolo/manifesto/receita, CMake/workflow
core-equivalence, TapeCore/filtros/roteamento, API, testes core e scripts de
render/estabilidade. Não reaberta a investigação de WAVs históricos.
[Snapshot atual das PRs](pr-snapshot.json): #58–#65 abertas/draft, sem merge;
heads/dependências iguais à F0.2. #63 não incorporada. Nenhuma F1–F11
anterior aprovada/incorporada identificada. Aprovação musical histórica C
não equivale à aprovação de release da #58.

Mapa: `tape_core.cpp:498–533` filtros de feedback; `:557–573` playback e
coeficientes privados C; `:1163–1174` seleção; `:1182–1212` eletrônica;
`:1218–1229` gravação; `:1240–1269` degradação C e troca Ping-Pong.
Fonte continua `src/main.cpp` → adaptador ESP32 → API C → TapeCore, memória
Tape/Reverse/Warp compartilhada. Nenhuma modificação de DSP, ABI ou preset.
CPU/RAM incremental de F1 no firmware: **zero**.

## Resultados e limites

- [Baseline](c0_baseline_manifest.json): dez cenários × dois processos,
  igualdade exata de inputs, float32, PCM24 e WAV/proveniência; mesmos inputs,
  38 parâmetros e lifecycle da F0.2, sem diferença além da seleção C.
- [Validação](validation.json): comandos reais de todas as unidades DSP Native
  e WASM têm C=1; API 1.7/38. A receita e a DLL têm hashes próprios.
- [Native/WASM](equivalence.json): cinco cenários **bit-exact**, max_abs=0,
  RMSE=0, mantendo 1e-5/1e-6. [Input/LUT](input-lut.json): 4096/1025 iguais.
- CTest C0 Native **15/16**, WASM **13/14** (goldens A não aplicados à C).
  Ambos falham na mesma assertion A `regression_core.cpp:255`: energia R
  6.10860251982e-6 abaixo de 1e-5 no teste de wrap de delay máximo.
  Não é o teste de Ping-Pong. Não mudamos teste/limiar nem declaramos suíte verde.
- [Observação completa](regression-observation.log) avalia todas as assertions,
  sem abortar na primeira: somente a citada falha. Full/split de ambos canais
  bit-exact; teste Ping-Pong, freeze/reset e demais assertions passam. A menor
  energia é compatível com a coloração adicional C já aprovada; não prova bug
  de wrap. Aceitamos esta discrepância como expectativa sonora específica A,
  não como tolerância de paridade relaxada. A prova de bloco independe desse piso.
  `validate_contracts.py` executa o CTest original e a observação completa; só
  qualifica essa falha identificada e rejeita qualquer outra.
  [Resultado local](contract-qualification.json).
- Reverse V2, Warp, Multi-Head, Ping-Pong, transport, Tap/MIDI, ABI/símbolos,
  controles e LUT têm evidências C0; não foi usada CI verde A como prova C.
- [Motor completo](engine-gates.json): **27/27**, 44.1/48 kHz, seis tails 60 s.
  [Feedback](feedback-stability.json): seis casos decaem, sem NaN/Inf ou samples
  no teto; não reproduzem o limited sustain histórico A nesses parâmetros.
  Feedback alto 50/75/85/100 e near-limit também medido na bancada espectral.
  Não se infere ausência de estados internos ocultos a partir só da saída.
- Goldens A/ABI/presets e manifesto F0.2 preservados; nenhum golden regenerado.

## Entregas e integração

[Reprodução](C0_BASELINE_PROTOCOL.md), [análise](FEEDBACK_SPECTRAL_ANALYSIS.md),
[desenho F2 e audição](F2_EXPERIMENT_DESIGN.md). Artefatos grandes ficam em
`artifacts/dsp-reform-f1/`; hashes versionados permitem regeneração.
Workflow `dsp-reform-f1.yml` publica WAVs C0 e proveniência da execução CI,
comparando dois processos no ambiente CI. Isso não implica igualdade automática
entre MinGW congelado e Linux CI; seus manifestos permanecem distintos.

Compatibilidade: C0/Reverse/Warp/Multi-Head/Ping-Pong são testáveis nesta base.
C1–C3 estão só na #59; Motion separado #60, unificado M0–M3 #61; Studio #62
sobre #61. Calibração futura deverá ser reaplicada/testada nessas branches;
combinação Aging+Unified+Studio não existe nesta entrega. Nenhuma integração
foi feita para facilitar testes. IDs experimentais 38–48 não foram tocados,
nem introduzidos na ABI 1.7 desta base.

## Handoff e orçamento físico

FASE F1; referência offline pronta; aprovação da PR PENDING. Base HEAD acima,
HEAD final no commit/PR desta entrega. Só documentação/bancada/workflow.
Hipótese: calibração conjunta dos filtros existentes pode preservar mais corpo
80–300 Hz. Medição não substitui preferência humana.
ESP32-S3 240 MHz, estéreo 44100/128, deadline 2902.49 µs. Referência histórica
C otimizada: P99 ~2333 µs, full-work peak ~2604 µs, zero overruns naquela
medição; **não são medidas desta build**. Sem filtros/buffers/alocações novos.
**PENDING_PHYSICAL** para esta build: P50/P95/P99/peak DSP/full-work,
param-update, I2S, overruns, heap/PSRAM/stack e rollback. Nenhum flash executado.
**PENDING_HUMAN** para nova audição; aprovação histórica C permanece válida
como decisão arquitetural, sem inferir superioridade de F2-B/C.
Rollback F1: descartar esta branch/baseline, preservando F0.2/A intactas.
Próxima etapa recomendada: implementar somente F2-B/C em nova fase autorizada,
comparar com F2-A/C0; exigir A/B/C humano e hardware antes de promoção.
