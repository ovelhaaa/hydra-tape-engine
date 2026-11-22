# Hydra 7.3 — testar o Classic C na demo web (antes do merge)

## O que mudou

- **ESP32-S3 padrão:** `pio run -e esp32-s3-devkitc-1` compila Classic C.
- **Rollback:** `pio run -e esp32-s3-legacy-a` compila a arquitetura A.
- **Demo Web:** o target `hydra_dsp_web` compila a arquitetura C por padrão.
- **Web Legacy A (apenas comparação):** configure o CMake com `-DHYDRA_WEB_LEGACY_A=ON`.
- Os executáveis Native/WASM de regressão histórica continuam A, deliberadamente, nesta PR. Isso não afeta o WASM específico da demo web.

Não faça merge nem flash automaticamente apenas porque o build passou.

## Testar a versão web de C imediatamente, pelo GitHub Actions

1. Abra a PR da branch `feat/phase7-3-classic-c-web-default`.
2. Na aba **Checks**, abra o workflow **Web Demo (Build + Deploy Pages)** do HEAD da PR.
3. Após o job `build` ficar verde, baixe o artifact **web-demo-build**.
4. Extraia o ZIP num diretório local com `index.html`, `hydra_dsp.js`, `hydra_dsp.wasm`, `hydra-processor.js`, `hydra-build-info.json` etc.
5. Abra um terminal na pasta com `index.html` e execute:
   ```bash
   python -m http.server 8000
   ```
6. Acesse `http://localhost:8000/` no Chrome/Edge. **Não** abra `index.html` diretamente via `file://` — AudioWorklet e fetch/WASM precisam de uma origem HTTP(S) adequada.
7. Confirme no cabeçalho: **DSP: Classic C (WASM)**. O workflow também verifica o flag da unidade `tape_core.cpp` compilada para o alvo `hydra_dsp_web`.
8. Carregue um áudio conhecido na demo, inicie o contexto de áudio, reproduza e compare o delay, feedback, tape age, reverse, multi-head, etc. A função de exportação offline usa o mesmo `hydra_dsp.wasm`.

**Importante:** a PR fornece um artifact para download; o workflow atual **não publica uma URL pública de preview para PRs**. A versão hospedada no GitHub Pages só recebe a mudança após o merge da PR na `main`. Não confunda o site atual (ainda A) com o artifact C desta PR.

Se o navegador mostrar estado desconhecido, confira se `hydra-build-info.json` está junto ao WASM e faça hard reload (Ctrl+Shift+R). Se não houver som, capture console e mensagens do AudioWorklet.

## Compilar e abrir localmente com Emscripten instalado

```bash
emcmake cmake -S . -B build-web -DBUILD_WEB=ON -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-web --target hydra_dsp_web -j
python -m http.server 8000 --directory build-web/web
```

Abra `http://localhost:8000/`. Para comparar com A, use outro diretório de build:

```bash
emcmake cmake -S . -B build-web-legacy -DBUILD_WEB=ON -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release -DHYDRA_WEB_LEGACY_A=ON
cmake --build build-web-legacy --target hydra_dsp_web -j
python -m http.server 8001 --directory build-web-legacy/web
```

Abra `http://localhost:8001/` para A. Os dois builds devem usar o mesmo source commit, sem mutação de estados em tempo real. Verifique os metadados de cada um antes da comparação.

## Testar e gravar na placa (etapa independente)

Primeiro identifique a porta e preserve a possibilidade de restaurar o firmware anterior. Não use `erase_flash`.

```bash
pio run -e esp32-s3-devkitc-1
pio run -e esp32-s3-legacy-a
pio device list
```

Após flash **somente com a placa conectada e consentimento do operador**, execute `identity` e confirme `ProductionArchitecture=CLASSIC_C` no padrão e `LEGACY_A_ROLLBACK` no fallback. Repita os cenários T1-T7 e as alternâncias `src 1` ↔ `src 2`; sem ADC I2S conectado, a validação acústica da entrada é limitada.

## Gates antes do merge

- [ ] Web Demo build (incluindo compilação com C=1) e navegador Chrome/AudioWorklet PASS.
- [ ] Core Equivalence, Native/WASM histórico, Reverse, Bias e Listening Lab PASS.
- [ ] Firmware padrão C e Legacy A compilam sem redefinição ambígua da macro.
- [ ] Audição web humana, incluindo feedback e reverse.
- [ ] Teste físico do firmware padrão C e rollback, ou declaração explícita de pendência.
- [ ] Nenhuma alteração dos goldens A, da topologia DSP ou do sample rate para forçar PASS.
