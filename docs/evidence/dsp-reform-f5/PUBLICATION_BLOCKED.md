# F5 — Estado de entrega e publicação

**Data:** 2026-11-24  
**Branch:** `experiment/dsp-reform-f5-musical-timing` (local)  
**Base:** `29f50258f1ac47139ac8f1fdb21aa59424ca9a75` (F3 / PR #68)  
**Head do código:** registrado em `build.json` (`source_commit` dos renders).

## O que está pronto

- DSP, testes, medições, pacote de escuta local e documentação.
- F5=0 bit-exact com a F3-A (10/10).
- Pacote local: 11 cenas, 66 WAVs PCM24, 82 arquivos com SHA-256 verificado.
- Builds ESP32-S3 a/b/c: SUCCESS (não é validação física).

## Bloqueio de publicação (não inventar links)

Este ambiente **não tem credenciais de push nem acesso de rede ao GitHub**:

```
git ls-remote --heads origin
fatal: unable to access 'https://github.com/ovelhaaa/hydra-tape-engine.git/':
schannel: AcquireCredentialsHandle failed: SEC_E_NO_CREDENTIALS
```

`gh` não está instalado; a API do GitHub também não responde (SSL). Portanto:

- **Não foi criada PR remota.**
- **Não foi executado o workflow `dsp-reform-f5.yml`.**
- **Não existe URL pública do artifact** — nenhum link é declarado.
- O texto da PR Draft está em `PR_DRAFT.md`; o pacote local está em
  `artifacts/dsp-reform-f5/listening/`.

## Receita de publicação (quando houver credenciais)

```sh
git push -u origin experiment/dsp-reform-f5-musical-timing
# abrir PR Draft com base experiment/dsp-reform-f3-saturation, usando PR_DRAFT.md
# o push dispara .github/workflows/dsp-reform-f5.yml
# baixar o artifact hydra-dsp-reform-f5-listening e verificar:
python - <<'PY'
import json, hashlib, pathlib
p = pathlib.Path('artifacts/dsp-reform-f5/listening')
h = json.load(open(p / 'sha256.json'))
bad = [k for k, v in h.items()
       if hashlib.sha256((p / k).read_bytes()).hexdigest() != v]
print(len(h), 'files; bad:', bad)
PY
```

O workflow publica o ZIP de audição e os JSON de evidência; o `sha256.json` do pacote é a
autoridade. Só declarar `READY_FOR_HUMAN_LISTENING` depois de baixar e conferir o artifact
publicado.

## Pendências

- **Físico:** `PENDING_PHYSICAL` (DSP P50/P95/P99/peak, full-work, I2S, overruns, memória,
  stack; nenhum flash executado).
- **Escuta:** humana, ainda não iniciada; nenhuma candidata promovida.
- **F4/#69:** não integrada.
