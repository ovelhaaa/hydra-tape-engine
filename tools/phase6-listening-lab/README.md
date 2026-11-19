# Hydra Listening Lab

Fase6.2: o mesmo player aceita o suplemento `Hydra Phase 6.2 Bias A/B` com60pares. Consulte o [guia de escuta do bias](../../docs/PHASE6_BIAS_AB_LISTENING_GUIDE.md) e o [relatório técnico](../../docs/PHASE6_BIAS_ANTIALIASING_REPORT.md). A=baseline processado, B=variante processada; todos os estados começam NOT REVIEWED. O pacote original de20cenários continua válido como referência histórica. A distribuição complementar pode coexistir com CI bloqueada e não significa aprovação técnica/musical.

Player offline dedicado à revisão da Fase 6.1; não altera a Web Demo nem os algoritmos DSP. Sem backend, telemetria de uso ou autoplay. Requer navegador moderno com Web Audio, seleção de diretório e Web Crypto em localhost (Chromium é testado).

## Reproduzir e executar

Na raiz do repositório (Linux/macOS com CMake e compilador C++; Windows com MinGW ou Visual Studio):

```sh
python -m pip install -r scripts/listening-lab-requirements.txt
cmake -S . -B build-native -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-native --config Release --target hydra_abi_test_library -j
python scripts/render_phase6_review.py --build-dir build-native --config Release --output artifacts/listening-lab/phase6-musical-validation
python scripts/package_listening_lab.py artifacts/listening-lab/phase6-musical-validation --archive artifacts/hydra-phase6-listening-lab.zip
python -m http.server 8000 --bind 127.0.0.1
```

Abra `http://localhost:8000/tools/phase6-listening-lab/`, selecione **Open extracted package** e escolha a pasta do manifesto. Também pode servir o pacote extraído e abrir `/player/`. `--library PATH` especifica biblioteca explicitamente. A descoberta procura somente o build indicado, sua configuração ativa e subdiretórios `lib`; não percorre o disco.

O workflow `.github/workflows/listening-lab.yml` gera o artifact `hydra-phase6-listening-lab`, com retenção de 30 dias. O link real é registrado na PR #54 depois da execução. O pacote inclui 40 WAVs PCM24 estéreo, 20 CSVs, manifesto com SHA-256, relatórios, guia e player. O empacotador valida presença, hashes, frames e extensão da telemetria antes de publicar. Falhas impedem upload. Artifacts requerem autenticação e não são URLs de áudio públicas; não há dependência de GitHub Pages.

## Revisão

Leia `listening-guide.md` no pacote ou `docs/PHASE6_LISTENING_GUIDE.md`. Dois AudioBufferSourceNodes usam o mesmo AudioContext, instante de início, offset e limites de loop. A/B muda somente ganhos complementares com rampa linear de 8 ms. Volume comum, sem compensação automática. Seek e alterações do loop recriam ambos os sources no mesmo instante. Trocar de cenário para os sources anteriores e não inicia áudio novo. Waveforms mostram o canal esquerdo; marcadores combinam eventos agendados e mudanças reais na telemetria. Os detalhes mostram IDs DSP completos e métricas reais do manifesto.

Decisões e notas são salvas localmente por commit, se permitido. **Export reviews** produz JSON portátil, **Import reviews** valida commit e hashes do conteúdo e mescla por cenário. Nenhum cenário começa aprovado. Arquivos ausentes/corrompidos geram erro e bloqueiam os controles sem mudar decisões humanas.

## Testes

```sh
python -m unittest discover -s scripts -p test_listening_lab.py
npm ci --prefix tools/phase6-listening-lab
npm test --prefix tools/phase6-listening-lab
npx --prefix tools/phase6-listening-lab playwright install chromium
npm run test:browser --prefix tools/phase6-listening-lab
```

O teste Chromium usa os WAVs reais, AudioContext real, importação de pasta, decisões, seek e loop; é teste funcional do player offline, não benchmark Browser Real do DSP. Dependências Python fixadas registram a execução; seed 6100 para jitter e 6100+ID para fontes. PCM depende do compilador/plataforma/NumPy; comparar hashes apenas entre entradas e toolchains equivalentes. A equivalência Native/WASM tem seu comparador próprio e continua falhando historicamente (#48).

Primeira entrega Linux verificada: [baixar artifact](https://github.com/ovelhaaa/hydra-tape-engine/actions/runs/44416301599/artifacts/14478588859), commit `0eaf0485ae623328a7b881adf59cf2d3e131a758`. Consulte a PR #54 para a entrega verificada mais recente. A CI usa Google Chrome já instalado no runner; localmente, o comando acima instala Chromium.
