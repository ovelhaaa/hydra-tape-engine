# F5 — Estado de publicação e verificação

**Estado:** `PUBLISHED_AND_VERIFIED`
**Atualizado:** 2026-11-25 (execução 45532866456)

## Pacote publicado

| Campo | Valor |
|---|---|
| Workflow | [`dsp-reform-f5.yml`](../../../.github/workflows/dsp-reform-f5.yml), execução [45532866456](https://github.com/ovelhaaa/hydra-tape-engine/actions/runs/45532866456) |
| Head da execução | `a8bc9c56d3a0c81d784260dae643cf2560a33b42` |
| Conclusão | **success** (produção, qualificação A/B/C, evidências e ESP32 verdes) |
| Artifact | `hydra-dsp-reform-f5-listening` — [id 14985866516](https://github.com/ovelhaaa/hydra-tape-engine/actions/runs/45532866456/artifacts/14985866516) |
| Tamanho | 117.591.429 bytes |
| SHA-256 do ZIP | `a7115ef0c54a5d62798a217b237086b22bf8409631baf23f15d796caf2153feb` |
| Conteúdo | 11 cenas, 66 WAVs PCM24 (RAW e MATCHED), player, gabarito, `sha256.json` com 82 arquivos |
| Retenção | 30 dias |

### Verificação feita após o download

1. O ZIP foi baixado pela API e o SHA-256 recalculado localmente: **igual ao
   digest publicado pelo GitHub**.
2. 95 entradas no ZIP; 83 do pacote de escuta.
3. `listening/sha256.json` conferido arquivo por arquivo: **82/82**, zero
   divergências; **66 WAVs** e **11 cenas** confirmados.
4. `manifest.json` publicado: SHA-256
   `02927878f51cf39dcbb0c6859129394d5e1e584bae7d9c697d9f2d346b4b2697`.
5. Gabarito publicado: SHA-256
   `6baaa1336a8317922ce955ce1c37166ace0fdd6a5983f5b0d0cafcc30a1f35ab`.

O CI também verifica o pacote antes de publicar (contagem de cenas/WAVs e todos
os hashes) e falha o job se algo divergir; um artifact com o nome
`hydra-dsp-reform-f5-listening` só é enviado quando tudo passa.

### Reprodutibilidade

O pacote foi reconstruído localmente (Windows, MinGW) a partir do mesmo commit:
os **66 hashes de WAV são idênticos** aos publicados. `manifest.json` e
gabarito diferem apenas em proveniência (hash das bibliotecas e `source_commit`)
e no fim de linha do checkout; o conteúdo JSON do gabarito é igual.

Registro completo: [`results/published-artifact.json`](results/published-artifact.json).

## Histórico: artifacts que não devem ser usados

| Artifact | Execução | Classificação | Motivo |
|---|---|---|---|
| `14985866516` | 45532866456 | **PUBLISHED_AND_VERIFIED** | pacote completo, hashes e digest conferidos |
| `14985175520` | 45529728140 | `SUPERSEDED_UNSEEDED_SOURCE` | cena 02 usava RNG sem semente e não podia ser regenerada |
| `14982026827` | 45524614234 | `INVALID_PARTIAL` | 97.286 bytes, **sem WAVs**: enviado pela execução que falhou antes do pacote existir |

## O bloqueio anterior não era de credenciais

O registro antigo (`PUBLICATION_BLOCKED.md`) dizia não haver credenciais nem
rede. O diagnóstico real: o Git para Windows falhava no backend Schannel
(`SEC_E_NO_CREDENTIALS`) e o `gh` não está instalado. A credencial do owner já
existia no Windows Credential Manager; com `http.sslBackend=openssl` foi
possível fazer push, e a API do GitHub respondeu via cliente Python/OpenSSL.
Nenhum token foi escrito no repositório, em arquivo versionado ou em log.

## Como reverificar

```sh
sha256sum hydra-dsp-reform-f5-listening.zip
# deve ser a7115ef0c54a5d62798a217b237086b22bf8409631baf23f15d796caf2153feb

python - <<'PY'
import hashlib, json, pathlib
p = pathlib.Path('listening')
h = json.loads((p / 'sha256.json').read_text())
bad = [k for k, v in h.items() if hashlib.sha256((p / k).read_bytes()).hexdigest() != v]
print(len(h), 'arquivos; divergentes:', bad)
PY
```

## Pendências

- **Escuta humana:** não iniciada; nenhuma candidata promovida. Link do artifact
  acima; o gabarito está em `results/answer-key.json` (não abrir antes de ouvir).
- **Física:** `PENDING_PHYSICAL` — nenhum flash, nenhuma medição na placa.
- **Expiração:** o artifact expira em 30 dias; `run.py`/`f5bench.py` regeneram o
  pacote a partir do commit. Os hashes publicados estão registrados para
  comparação futura.
