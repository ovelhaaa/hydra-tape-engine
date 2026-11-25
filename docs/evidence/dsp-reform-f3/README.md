# F3 — Vintage, Smooth Saturation e Harmonic Bloom

Estado: **HUMAN_LISTENING_COMPLETED_EQUIVALENT**. O usuário concluiu26/26
avaliações, todas equivalentes (24 confianças médias,2 altas), sem preferência
ou rejeição. [Resultado humano e proveniência](HUMAN_LISTENING_RESULT.md).
Vintage F3-A permanece referência existente; B/C experimentais, sem promoção.
Hardware **PENDING_PHYSICAL**. READY_FOR_HUMAN_LISTENING foi o estado anterior.
Nenhuma candidata promovida; sem merge ou release.

[Baixar ZIP78WAVs + player + relatórios](https://github.com/ovelhaaa/hydra-tape-engine/actions/runs/45321905632/artifacts/14888527811).
[CI completa aprovada](https://github.com/ovelhaaa/hydra-tape-engine/actions/runs/45321905632),
fonte `1e454eb`. ZIP382733731bytes, SHA256
`13055c4bdf231a35e2dd2857a787ffae04fd3e285a4e4347ef7543401b1391d0`.
Baixado: digest GitHub/CRC/94 hashes/78WAVs conferidos. Veja
[guia simples](LISTENING_GUIDE.md) e [registro](published-artifact.json).
PR [#68 Draft](https://github.com/ovelhaaa/hydra-tape-engine/pull/68).

Resultados locais: A10/10 bit-exact F2-C; B/C168/192 cenas dinâmicas diferentes
de A,24/192 FB0 exatas; Native/WASM0/0; motor27/27/candidata; estresse154/154,
60 caudas60s +4 compostas/candidata; zero teto após entrada. Clipping ativo
44100/48000:A38/17,B38/17,C30/13. Sem aumento nem headroom interno piorado.
CTest16/17 Native e14/15 WASM: assertion A conhecida mantida. Um flake Tap
inicial preservado; repetição completa passou no Tap. Observação completa
Native/WASM só identifica a assertion A conhecida. Objetos Native:84160bytes
text em todas; data/bss0. Nenhuma inferência de CPU física.

CI da fonte1e454eb: Core Equivalence, Bias Anti-Aliasing, Reverse Real-Time,
Listening Lab, Web Demo e F3 completos/sucesso. Mudanças posteriores de relatório
ou verificação não mudam core/header experimental, entradas ou renderização.
Os manifestos F0.2/F1/F2 e goldens anteriores têm diff vazio.

Base F2 `2c15b88680ff65aff8bd5ac09543fab44b52c785`, branch
`experiment/dsp-reform-f3-saturation`. F3-A mantém Vintage bit-exact;
F3-B/C redistribuem exclusivamente os ganhos dos dois saturadores existentes.
Veja [arquitetura](SATURATION_ARCHITECTURE.md) e
[decisão F2](F2_HUMAN_DECISION_REFERENCE.md).

Builds: CMake Native/WASM Release com `-ffp-contract=off`,
Classic C=1/F2=2/F3=0|1|2 em todos os targets DSP. Exemplos executáveis
em `.github/workflows/dsp-reform-f3.yml`. Firmware experimental tem ambientes
`esp32-s3-f3-a/b/c`; build não representa execução física. Default F3=0.

Bancada: `run.py baseline|contracts|stress|render|package --variant 0|1|2`,
`compatibility.py`, `compound.py`, `dynamics.py`, `summarize.py`.
Inputs via protocolo F1 `prepare_audition.py`, seed20261123. Diretório novo
`artifacts/dsp-reform-f3`; não sobrescrever F0.2/F1/F2 nem goldens.
Estabilidade e instrumentação reutilizam gates F2 sem relaxamento; clipping
ativo é confrontado separadamente com F3-A.

Native/WASM preserva limites1e-5/1e-6. CTest original continua visível, incluindo
assertion de energia A incompatível com C. Observação completa é complementar.
Nenhum benchmark desktop é convertido em estimativa ESP32. Exigir campanha
física comparável antes de promoção; a escuta registrada não demonstrou
vantagem perceptual de B/C.
