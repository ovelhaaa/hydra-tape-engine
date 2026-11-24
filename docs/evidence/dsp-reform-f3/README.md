# F3 — Vintage, Smooth Saturation e Harmonic Bloom

Estado durante os ensaios: **BLOCKED_FOR_HUMAN_LISTENING** até fechar gates e
publicar/verificar o ZIP. Hardware **PENDING_PHYSICAL**, audição **PENDING_HUMAN**.
Nenhuma candidata promovida; sem merge ou release.

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
física comparável e escuta humana antes de promoção.
