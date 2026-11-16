# Hydra / Listening Lab

Estação de escuta local para a [PR #47](https://github.com/ovelhaaa/hydra-tape-engine/pull/47). Execute na raiz do repositório.

```sh
python -m http.server 8765 --bind 127.0.0.1 --directory tools/listening-lab
```

Abra [Listening Lab](http://127.0.0.1:8765/) no Edge ou Chrome. A aplicação lê os arquivos selecionados no navegador e funciona sem conexão externa.

## Pacotes

| Fonte | Conteúdo e relacionamento |
| --- | --- |
| `artifacts/release-gate/listening/`, quando presente | Manifesto de regressão por cenário. Baseline/Optimized (`before`/`head`), Strict/Fast e controles pós-tanh documentados no sign-off. Renders principais estéreo PCM24 a 48 kHz, com entradas F32, eventos e parâmetros. O ensaio Freeverb a 44,1 kHz tem metadados próprios. |
| `accidental-warp-ab` da CI ou `artifacts/warp-ab/` | Manifesto com commit, entrada, configurações, frames, taxa, variantes e status de cada engine. Worklet × Python, Worklet × TapeCore e Python × TapeCore quando disponíveis. Toggle pode ter apenas TapeCore e aparece como escuta individual. |
| `listening-review` da CI | `review.json`, Native/WASM, entrada seca, condições, taxa e eventos. |
| A/B Offline Review | `package_summary.json` com pares Native/WASM. |

Use **Selecionar pasta** na pasta extraída do artifact, ou **Selecionar arquivos** para WAVs e JSON. Se houver vários manifestos, escolha o pacote pelo caminho. Com arquivos avulsos, aceite uma sugestão ou selecione A e B. Use **Adicionar par** para cada comparação e **Abrir pares confirmados** quando terminar. Para retomar uma seleção manual, confirme os mesmos pares, nomes e ordem. Caminhos ambíguos precisam da seleção por pasta.

Preencha **Commit dos renders** antes de abrir o pacote quando o manifesto não trouxer essa informação e o SHA tiver sido verificado. Para o release gate local, consulte a proveniência dos builds no [registro de escuta](../../docs/ACCIDENTAL_WARP_LISTENING_SIGNOFF.md). Para artifacts da CI, consulte o SHA da execução que gerou os WAVs. A aplicação exibe commit indisponível quando não há identificação.

## Audição

- A/B compartilham o relógio, o deslocamento e o loop. A troca ajusta apenas os ganhos, com rampas complementares de 8 ms ou 1 ms para transientes. O deck usa o trecho comum desde 0 s quando as durações diferem.
- RAW preserva o ganho relativo. MATCHED mede o RMS de todo o trecho comum no canal selecionado, calcula a média quadrática dos canais em Stereo e atenua o mais alto até o RMS do mais baixo. Um canal silencioso mantém ambos os ganhos. A proteção comum limita o pico previsto das amostras a 0,98 FS antes do volume de monitor. Os indicadores mostram essa atenuação.
- As formas de onda usam as amostras decodificadas no relógio do navegador. A−B compara amostras RAW na mesma posição, com lag 0 ms. A diferença de amostras depende do alinhamento e da fase. Julgue a diferença perceptual por escuta.
- Clique na forma de onda para ir ao trecho e arraste para marcar um loop. Os campos numéricos também ajustam os limites. Use Zoom e Janela para navegar. A linha do tempo usa eventos com posição documentada no pacote. Crie marcadores manuais para outras ocorrências.
- Espaço reproduz ou pausa quando o foco está fora dos controles. A/B ou X/Y selecionam a fonte, Shift+A alterna, L ativa ou desativa loop, R retorna ao início e ←/→ deslocam 0,5 s. Tab mantém a navegação por foco.
- A escuta cega de regressões usa MATCHED, com semente e associação X/Y fixas por par. Registre preferência ou equivalência e confiança antes de revelar. O resultado é uma observação de escuta. A ocultação vale para a interface do Lab, que roda inteiramente no navegador.

O DriftBrigade orientou a navegação por cenários, a decodificação sob demanda, a troca por rampas de ganho e o fluxo preferência → revelação → exportação. No Hydra, cada variante tem seus pares de validação, a comparação mostra as duas formas de onda e as decisões ficam vinculadas ao conteúdo dos WAVs.

## Sessões e sign-off

**Salvar avaliação** grava o formulário por par no armazenamento local da origem. Reabra o mesmo pacote para retomar. A identidade SHA-256 inclui o manifesto, o commit informado e os WAVs usados, de modo que uma atualização de áudio produz uma identidade diferente. JSON transporta a sessão para outro perfil. A importação exige a mesma identidade e substitui a sessão daquele pacote.

**Export Listening Report** baixa um Markdown no formato do [sign-off](../../docs/ACCIDENTAL_WARP_LISTENING_SIGNOFF.md). Pareceres de investigação ou rejeição resultam em REQUIRES FIXES. Sem esses pareceres, um par sem decisão ou um pacote incompleto resulta em INCOMPLETE. A aprovação exige decisões para todos os pares do pacote aberto. Exporte um relatório por pacote avaliado. A decisão de release continua com o revisor.

## Testes

```sh
node --test tools/listening-lab/lab.test.mjs
```

Para Playwright, instale a dependência em uma pasta temporária e aponte para o Chromium fornecido pelo Playwright ou para o Edge local. Exemplo no PowerShell, com os dois pacotes de WAVs existentes.

```powershell
$labTestDeps = Join-Path ([IO.Path]::GetTempPath()) 'hydra-lab-playwright'
npm install --prefix $labTestDeps playwright
$env:HYDRA_PLAYWRIGHT = Join-Path $labTestDeps 'node_modules/playwright'
$env:HYDRA_BROWSER = 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'
$env:HYDRA_RELEASE_ARTIFACT = 'artifacts/release-gate/listening'
$env:HYDRA_WARP_ARTIFACT = 'artifacts/warp-ab'
node --test tools/listening-lab/browser.test.mjs
```

O teste de transporte renderiza a saída real do Web Audio em memória e verifica a continuidade da troca, o alinhamento, o loop e o roteamento. Os testes de interface com WAVs reais exigem os caminhos dos artifacts nas variáveis acima. Para testar também o ZIP `listening-review` da CI, extraia-o e defina `HYDRA_CI_ARTIFACT` com o caminho da pasta que contém `review.json`.
