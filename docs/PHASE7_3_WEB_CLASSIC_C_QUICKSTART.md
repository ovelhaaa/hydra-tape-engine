# Hydra 7.3 — Testar o Classic C na Demo Web (Antes do Merge)

## O que mudou

- **ESP32-S3 padrão:** `pio run -e esp32-s3-devkitc-1` compila **Classic C** (`HYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING=1`).
- **Rollback ESP32-S3:** `pio run -e esp32-s3-legacy-a` compila a arquitetura **Legacy A** (`HYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING=0`).
- **Demo Web:** o target `hydra_dsp_web` compila **Classic C** por padrão e gera `hydra-build-info.json` com arquitetura, commit e versão do compilador Emscripten.
- **Web Legacy A (apenas comparação):** configure o CMake com `-DHYDRA_WEB_LEGACY_A=ON`.
- Os executáveis Native/WASM de regressão histórica continuam A, deliberadamente, nesta PR. Isso não afeta o WASM específico da demo web.

> [!IMPORTANT]
> Não faça merge nem flash automaticamente apenas porque o build passou.

## Testar a versão web de C imediatamente no Windows (via Artifact da PR)

1. Na PR da branch `feat/phase7-3-classic-c-web-default`, abra a aba **Checks** e entre no workflow **Web Demo (Build + Deploy Pages)** do HEAD mais recente.
2. Após o job `build` ficar verde, baixe o artifact **`web-demo-build`**.
3. Extraia o `.zip` em uma pasta local (ex.: `C:\temp\hydra-web-c`) contendo `index.html`, `hydra_dsp.js`, `hydra_dsp.wasm`, `hydra-processor.js`, `hydra-build-info.json` e demais módulos `.js`.
4. Abra o PowerShell nessa pasta e inicie um servidor HTTP local (sem cache):
   ```powershell
   python -m http.server 8000
   ```
   *(Se `python` não estiver no PATH no Windows, use `py -3 -m http.server 8000`.)*
5. Acesse **`http://localhost:8000/`** no Chrome ou Edge.
   - **Nunca** abra `index.html` via `file://` — `AudioWorklet` e `fetch()` de `.wasm` exigem origem `http://` ou `https://`.
6. Confirme no cabeçalho o badge verde: **`DSP: Classic C (WASM)`** (passe o mouse sobre o badge para ver o commit e a versão do compilador em `hydra-build-info.json`).
7. Carregue um arquivo `.wav` ou `.mp3` em **Load Custom Audio Source**, clique em **Start Audio Context** e depois em **Play**.
   - A cadeia DSP já inicia conectada por padrão (**Disconnect DSP Chain** permite alternar seco/molhado para A/B).
   - Teste em tempo real: **Regen Loop (Feedback)**, **Magnetic Formulation Age (Tape Age)**, **Head Tilt Tone EQ (Tone)**, **Active Heads Number**, **Dir Reverse**, **Active Reverb Tank (Spring)** e **Envel Folwr (Guitar Focus)**.
   - Clique em **Execute Offline BWF Render** para exportar o áudio processado pelo mesmo `hydra_dsp.wasm` (48 kHz stereo WAV).

### Evitar cache de arquivos WASM antigos

- O host carrega `hydra-build-info.json` e `hydra_dsp.wasm` com `cache: 'no-store'` e query de commit (`?v=<gitCommit>`).
- Ao trocar entre builds (ex.: Classic C vs Legacy A) na mesma porta local, faça um **Hard Reload** (`Ctrl + Shift + R`) ou use portas distintas (`8000` para C e `8001` para A).
- **Importante:** a PR não sobrescreve o GitHub Pages de produção (`main`); o artifact `web-demo-build` é o caminho oficial de audição pré-merge.

## Compilar e comparar A × C localmente (Windows + Emscripten)

Com o Emscripten SDK (`emsdk`) ativo no PowerShell:

```powershell
# 1. Build Classic C (padrão)
& "C:\emsdk\emsdk_env.ps1"
emcmake cmake -S . -B build-web -DBUILD_WEB=ON -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-web --target hydra_dsp_web -j

# 2. Build Legacy A (comparação controlada no mesmo commit)
emcmake cmake -S . -B build-web-legacy -DBUILD_WEB=ON -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release -DHYDRA_WEB_LEGACY_A=ON
cmake --build build-web-legacy --target hydra_dsp_web -j

# 3. Gerar pacote de escuta comparativa A x C (48 kHz WAV, sem normalização independente)
node scripts/generate_phase73_web_listening.mjs
```

Os arquivos gerados ficam em `artifacts/phase73-web-listening/`:
- `classic-c/*.wav`
- `legacy-a/*.wav`
- `comparison/*_dry.wav` e `*_diff_A_minus_C.wav`
- `manifest.json` e `listening-report.md`

Para abrir os dois servidores lado a lado no navegador:
```powershell
python -m http.server 8000 --directory build-web/web
# Em outro terminal:
python -m http.server 8001 --directory build-web-legacy/web
```
Confirme `DSP: Classic C (WASM)` em `http://localhost:8000/` e `DSP: Legacy A (WASM)` em `http://localhost:8001/`.

## Testar e gravar firmware ESP32-S3 (etapa independente)

> [!CAUTION]
> Não execute flash sem consentimento explícito do operador e sem identificar a porta serial correta. Não use `erase_flash`.

```powershell
pio run -e esp32-s3-devkitc-1
pio run -e esp32-s3-legacy-a
pio device list
```

Após flash autorizado, execute `identity` no monitor serial e confirme `CLASSIC_C` no ambiente padrão e `LEGACY_A_ROLLBACK` no fallback.

## Gates antes do merge

- [x] Web Demo build (`HYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING=1`), testes automatizados de áudio web e Chrome/AudioWorklet PASS.
- [x] Exportação offline usando o mesmo `hydra_dsp.wasm` Classic C validada no Chrome.
- [x] Pacote comparativo A × C em 48 kHz gerado a partir do mesmo commit.
- [x] Core Equivalence, Native/WASM histórico, Reverse, Bias e Listening Lab PASS.
- [x] Firmware padrão C (`esp32-s3-devkitc-1`) e rollback A (`esp32-s3-legacy-a`) compilam sem conflito de macro.
- [ ] Audição web humana final pelo operador antes do merge.
- [ ] Validação física em placa ESP32-S3 conectada (quando autorizada pelo operador).
