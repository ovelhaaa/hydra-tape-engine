# Phase 7.3B — Classic Tape Feedback C: Web Listening Acceptance & Production Gate

## Classificação Final

- **WEB_CLASSIC_C_PASS**
- **FIRMWARE_DEFAULT_PASS**
- **PHYSICAL_PROMOTION_NOT_EXECUTED** *(placa ESP32-S3 detectada em `COM15`, porém sem flash nem execução física nesta etapa por política de consentimento do operador)*

---

## 1. Identificação do Build e Ambiente

| Item | Valor |
|---|---|
| **PR / Branch** | PR #58 (`feat/phase7-3-classic-c-web-default` → `main`) |
| **Commit base avaliado** | `ca42230d481d9632ad45ca948bfe1c26fbe21dbd` (+ alterações locais desta etapa) |
| **Identidade do WASM (`hydra-build-info.json`)** | `feedbackRouting: "PHASE72_CLASSIC_C"`, `buildTarget: "hydra_dsp_web"` |
| **Badge exibido no navegador** | `DSP: Classic C (WASM)` |
| **Emscripten** | `emcc 3.1.62` (Clang `19.0.0`) |
| **Navegador utilizado** | Google Chrome `157.0.8103.79` (via Playwright + AudioWorklet + OfflineAudioContext) |
| **Sample rate efetivo** | `48000 Hz` |
| **SHA-256 `build-web/web/hydra_dsp.wasm` (Classic C)** | `85921b97f94cef90f802657b713f8a9bb421338c447e16f6c38f69468a5ccc50` |
| **SHA-256 `build-web-legacy/web/hydra_dsp.wasm` (Legacy A)** | `C7044F540C3D6E24756C727F1C15F39CABBEE080E36F2AA79CB1A2EC40B67F1E` |

---

## 2. Problemas Encontrados e Corrigidos na Demo Web

1. **Cadeia DSP desconectada por padrão (`host/web/main.js`):**
   - `connected` iniciava em `false`, fazendo o botão **Play** rotear áudio seco direto para `context.destination` até o usuário descobrir o botão **Connect DSP Chain**.
   - **Correção:** `connected = true` por padrão (botão inicia como `Disconnect DSP Chain`), garantindo audição imediata do Classic C ao carregar arquivo e pressionar Play.
2. **Inicialização assíncrona do AudioWorklet sem aguardar `ready` (`host/web/main.js`):**
   - `ensureAudioGraph()` não aguardava `waitForWorkletReady(fxNode)` nem resumia o `AudioContext` antes de esperar o worklet.
   - **Correção:** `ensureAudioGraph()` agora resume o `AudioContext` quando necessário e aguarda `waitForWorkletReady(fxNode)` antes de reportar sucesso.
3. **Cache de WASM e rastreabilidade de build (`CMakeLists.txt` + `host/web/main.js`):**
   - `getWasmBinary()` fazia `fetch('./hydra_dsp.wasm')` sem `cache: 'no-store'` nem query de commit.
   - **Correção:** `CMakeLists.txt` agora grava `gitCommit` e `emscriptenVersion` em `hydra-build-info.json`, e `main.js` carrega `hydra-build-info.json` e `hydra_dsp.wasm` com `cache: 'no-store'` + `?v=<gitCommit>`.
4. **Rampa inicial de parâmetros no render offline (`host/web/main.js`):**
   - O render offline (`OfflineAudioContext`) usava `linearRampToValueAtTime(..., t + 0.015)` a partir dos defaults em vez de aplicar o valor imediato no sample `0`.
   - **Correção:** `syncAllParams(offlineNode, true)` usa `setValueAtTime(value, t)` para o render offline, mantendo `linearRampToValueAtTime` apenas no uso interativo em tempo real.

---

## 3. Testes Automatizados de Áudio Web e Navegador

### 3.1 Suite Host Web (`node --test host/web/tests/*.test.mjs`)
- **Resultado:** **41/41 PASS** (`0` falhas, `0` pulados).
- **Novo teste adicionado:** [`host/web/tests/webClassicCAudioProcessing.test.mjs`](../../host/web/tests/webClassicCAudioProcessing.test.mjs)
  - Instancia `hydra_dsp.wasm` compilado com `PHASE72_CLASSIC_C`.
  - Injeta sinal determinístico em 48 kHz (128 frames/bloco), confirma saída finita (sem `NaN`/`Inf`), modificação real do sinal vs seco e permanência da cauda após o fim da entrada (`baselineTailRms = 0.129455`, `baselineHashL = f047707776c5019c`, `baselineHashR = 1fd53bdbe2d3f12e`).
  - Valida que `reset` limpa a cauda (`postResetTailRms = 0`).
  - **Performance web (48 kHz, orçamento de 2.667 ms por bloco de 128 frames):**
    - Média: `0.1209 ms` (~4.5% do orçamento)
    - p95: `0.1729 ms`

### 3.2 Resultado dos Controles Verificados no DSP Classic C
Cada controle abaixo foi exercitado via `AudioParam` ou `MessagePort` no `HydraProcessor` e comparado contra a baseline:

| Controle | Canal | Diff RMS vs Baseline | Hash L (16 hex) | Status |
|---|---|---|---|---|
| `feedback` (`p_13 = 85`) | AudioParam | `0.035028` | `ba8d4c506c2b3347` | PASS |
| `tapeAge` (`p_6 = 95`) | AudioParam | `0.046747` | `da488126c4d2098e` | PASS |
| `tone` (`p_20 = 10`) | AudioParam | `0.012926` | `89e92fb49b4b2587` | PASS |
| `activeHeads` (`7`) | MessagePort | `0.113921` | `b26b900c8bcda98a` | PASS |
| `reverse` (`1`) | MessagePort | `0.155833` | `ff06f46f882bb71e` | PASS |
| `spring` (`1`) | MessagePort | `0.117392` | `72d413c2ec96071f` | PASS |
| `guitarFocus` (`1`) | MessagePort | `0.132537` | `63d605e582b80555` | PASS |

### 3.3 Testes Reais no Navegador (Chrome 157 + AudioWorklet + OfflineAudioContext)
1. **Reset AudioWorklet (`tools/phase6-listening-lab/tests/resetBrowser.mjs`):**
   - **PASS** em todos os 5 cenários (`running`, `stopped`, `error`, `repeated`, `timeout`).
2. **Audição E2E & Exportação Offline (`tools/phase6-listening-lab/tests/classicCBrowserAudition.mjs`):**
   - **PASS** (`consoleErrors = []`, `pageErrors = []`).
   - Verificou badge `DSP: Classic C (WASM)`, upload de WAV 48 kHz (`dryTailRms = 0`), inicialização de `AudioContext` + `AudioWorklet`, playback, exercício dos controles na UI e exportação offline (`OfflineAudioContext` 48 kHz stereo, `72000` frames, `offlineWetTailRms = 0.059469`, `offlineWetTailPeak = 0.459503`).

---

## 4. Comparação Controlada A × C (48 kHz, Mesmo Commit)

Pacote gerado por [`scripts/generate_phase73_web_listening.mjs`](../../scripts/generate_phase73_web_listening.mjs) em `artifacts/phase73-web-listening/` (sem normalização independente; ganho unitário):

| Cenário | Estímulo | Classic C RMS / Peak | Legacy A RMS / Peak | Diff (A − C) RMS |
|---|---|---|---|---|
| Delay convencional (`01_conventional_delay`) | transient | `0.0546 / 0.4876` | `0.0495 / 0.4876` | `0.0129` |
| Multi-Head + Ping-Pong (`02_multihead_pingpong`) | transient | `0.0594 / 0.5313` | `0.0544 / 0.5179` | `0.0165` |
| Tape Age elevado (`03_high_tape_age`) | harmonic | `0.1900 / 0.8735` | `0.1923 / 0.8115` | `0.0652` |
| Reverse com feedback (`04_reverse_feedback`) | transient | `0.0603 / 0.5132` | `0.0589 / 0.5319` | `0.0089` |
| Feedback elevado (`05_high_feedback`) | harmonic | `0.1998 / 0.9013` | `0.2771 / 0.8512` | `0.1860` |

As diferenças entre A e C confirmam o comportamento esperado da arquitetura C (saturação e atenuação progressiva no loop de feedback clássico, especialmente visível em `05_high_feedback` e `03_high_tape_age`), sem modificações no algoritmo DSP aprovado.

---

## 5. Firmware ESP32-S3 (Padrão C e Rollback A)

- `pio run -e esp32-s3-devkitc-1` (`-DHYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING=1`): **SUCCESS** (`85.36 s`).
- `pio run -e esp32-s3-legacy-a` (`-DHYDRA_PHASE72_CLASSIC_FEEDBACK_ROUTING=0`): **SUCCESS** (`118.41 s`).
- **Hardware físico:** porta `COM15` (`USB VID:PID=303A:1001`) detectada via `pio device list`, porém **não gravada/testada** nesta sessão (`PHYSICAL_PROMOTION_NOT_EXECUTED`) para evitar flash sem consentimento explícito do operador.

---

## 6. Status dos Workflows (PR #58)

No HEAD anterior da PR #58 (`ca42230d481d9632ad45ca948bfe1c26fbe21dbd`), os checks reportaram **success**:
- `test_native_vs_wasm (ubuntu-24.04 / ubuntu-26.04)`
- `test_native_core`, `build_native_core`, `build_wasm_core`
- `listening_review`, `listening_lab`, `audition_package`
- `magnetic_bias_review`, `validate_bias`, `validate_warp`
- `build_esp32_s3`, `firmware_classic_c`
- `build` / `validate` (Web Demo)

Após o push das alterações desta etapa (`.github/workflows/web-pages.yml`, `CMakeLists.txt`, `host/web/main.js`, `host/web/tests/webClassicCAudioProcessing.test.mjs`, `tools/phase6-listening-lab/tests/classicCBrowserAudition.mjs`), todos os workflows deverão ser reavaliados no novo HEAD da PR #58 antes do merge.

---

## 7. Como Testar a Demo Imediatamente (Windows)

```powershell
python -m http.server 8000 --directory build-web/web
```

Abra `http://localhost:8000/` no Chrome/Edge, confirme `DSP: Classic C (WASM)` no cabeçalho, carregue um arquivo de áudio, clique em **Start Audio Context** e **Play** (ou **Execute Offline BWF Render**). Consulte também [`docs/PHASE7_3_WEB_CLASSIC_C_QUICKSTART.md`](../PHASE7_3_WEB_CLASSIC_C_QUICKSTART.md).
