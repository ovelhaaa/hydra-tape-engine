# Hydra Tape Engine — Fase UI-01: ST7789 240×240 + Encoder Único

## 1. Perfis de Build

- **`esp32-s3-st7789`** (novo perfil MVP):
  - Define `-DHYDRA_UI_BACKEND_ST7789=1`.
  - Ignora `DisplayUI`, `Adafruit SSD1306` e `Adafruit GFX Library`.
  - Usa exclusivamente o display SPI ST7789 240×240 e um encoder rotativo com botão (sem botão AUX, sem touchscreen, sem pots/footswitches obrigatórios).
- **`esp32-s3-devkitc-1`** (perfil legado preservado):
  - Mantém o driver SSD1306 I2C (128×64) e `AUX_BUTTON_PIN` no GPIO7 sem alterações.

---

## 2. Manual de Ligação Elétrica (ESP32-S3 N16R8)

### Display ST7789 1,54" 240×240 SPI (Validado em `amy/smk-s3`)

| Sinal ST7789 | GPIO ESP32-S3 | Observação |
| --- | --- | --- |
| `VCC` | `3V3` | Alimentação 3.3V |
| `GND` | `GND` | Terra comum |
| `MOSI` (SDA / DIN) | **GPIO 11** | SPI2 MOSI (40 MHz) |
| `SCLK` (SCL / CLK) | **GPIO 12** | SPI2 SCLK |
| `CS` | **GPIO 10** | Chip Select |
| `DC` (RS) | **GPIO 9** | Data / Command |
| `RST` (RES) | **GPIO 8** | Reset |
| `BL` (BLK / LED) | **GPIO 7** | Backlight PWM (ativo em alto, apagado até 1º frame válido) |

### Encoder Rotativo com Clique

| Sinal Encoder | GPIO ESP32-S3 | Observação |
| --- | --- | --- |
| `A` (CLK) | **GPIO 4** | Quadratura hardware PCNT |
| `B` (DT) | **GPIO 5** | Quadratura hardware PCNT |
| `SW` (Botão) | **GPIO 6** | `INPUT_PULLUP` (clique curto + pressão longa >= 800 ms) |
| `GND` | `GND` | Terra comum |

### Pinos Reservados / Proibidos na Placa

- **Áudio I2S (`PCM5102`):** BCLK=GPIO 15, LRCK=GPIO 16, DOUT=GPIO 17, DIN=GPIO 18.
- **RGB LED onboard:** GPIO 48.
- **Octal PSRAM (OPI N16R8):** GPIO 33–37 (não usar).
- **USB Nativo:** GPIO 19 (D-), GPIO 20 (D+).
- **UART0:** GPIO 43 (TX), GPIO 44 (RX).
- **Strapping / Boot:** GPIO 0, GPIO 45, GPIO 46.

> [!WARNING]
> **Conflito no GPIO 7:** No perfil legado OLED (`lib/DisplayUI/include/hardware_pins.h`), o GPIO 7 era usado pelo `AUX_BUTTON_PIN`. No perfil `esp32-s3-st7789`, o GPIO 7 é **exclusivo do Backlight (`ST7789_BL_PIN`)** e o botão AUX é desabilitado em tempo de compilação (`HYDRA_HAS_AUX_BUTTON 0`), com `static_assert` verificando colisões em `lib/HydraUI/include/hydra_ui_pins.h`.

---

## 3. Mapa de Navegação (Apenas Encoder + Clique)

- **Giro:** move foco/cursor ou altera valor em edição.
- **Clique curto (< 800 ms):** entra no item/editor, confirma preset ou emite evento Tap na tela Tap Tempo.
- **Pressão longa (>= 800 ms):** volta um nível (`PARAM_EDIT` → `PARAM_LIST` → `MENU` → `HOME`). Na `HOME` (fora de edição), alterna **SW BYPASS** com banner ostensivo.

### Grupos do MENU (10 grupos)

1. **Modo:** `Tape Delay`, `Tape Saturator`, `Reverse`, `Warp`, `Reverse + Warp`, `Fripp (Host)`, `Bubbles (Host)` (comandos semânticos que preservam `spring`, `freeze`, `dryWet`, `feedback`, `delayTimeMs`).
2. **Delay:** `Delay Active`, `Delay Time` (máximo em runtime da API), `Feedback`, `Mix (Dry/Wet)`, `Active Heads` (máscara `A B C`), `Ping Pong`, `Freeze`.
3. **Fita:** `Tape Speed`, `Tape Age`, `Drive`, `Tape Noise`, `Head Bump`, `Azimuth Err`, `Dropouts`, `Tone`, `Guitar Focus`.
4. **Movimento:** `Flutter Depth`, `Flutter Rate`, `Wow Depth`, `Wow Rate`.
5. **Reverse:** `Reverse`, `Rev Mode` (`RHYTHM`/`FREE`/`LEGACY`), `Rev Division` (`1/4`..`1/8T`), `Rev Smear`.
6. **Warp:** `Warp Active`, `Warp Speed`, `Warp Grain`, `Warp Stereo`, `Warp Feedback`, `Warp Mode`.
7. **Espaco:** `Spring Reverb`, `Spring Mix`, `Spring Decay`, `Spring Damp`, `FreeVerb (Host)` (`Enable`, `Room`, `Damping`, `Wet`, `Width`).
8. **Tempo:** `BPM`, `Tap Tempo` (tela dedicada com status `IDLE`/`ACQUIRING`/`LOCKED`/`REJECTED` da API), `Heads Musical`, `Tempo Source` (`INT`/`TAP`/`MIDI`), `Delay Display` (`ms`/`BPM`).
9. **Presets:** `Clean`, `Lo-Fi`, `Dub`, `Broken`, `Warp Oct`, `Warp Casc` (seleção + confirmação explícita; gravação exibida como indisponível sem backend de persistência).
10. **Sistema:** `SW Bypass`, `Master Vol`, `Audio Source`, `Synth Gen` (`Enable`, `Waveform`, `Scale`, `Key`, `Mood`, `Rhythm`, `ENO Mode`), `Reset DSP Buffer`.

---

## 4. Arquitetura Implementada

- **`lib/HydraUI/` (independente de display):**
  - `hydra_ui_pins.h`: pinagem centralizada + `static_assert` anti-colisão.
  - `hydra_ui_types.h`, `hydra_ui_catalog.h/.cpp`: catálogo sincronizado via `hydra_dsp_get_param_specs()` + `runtimeMaxDelayMs`.
  - `hydra_ui_snapshot.h`: snapshot somente leitura para renderização sem travar o áudio.
  - `hydra_ui_model.h/.cpp`: máquina de estados pura (inclui eventos futuros `POT_CHANGED` com soft-takeover e `FOOTSWITCH`).
  - `hydra_ui_renderer.h/.cpp`: renderizador RGB565 240×240 com fonte 5×7 escalável.
  - `st7789_driver_esp32.h/.cpp`: driver `esp_lcd` SPI 40 MHz, framebuffer 115.200 B em PSRAM, janela DMA 7.680 B (16 linhas) em RAM interna, ~15 FPS.
  - `encoder_pcnt_esp32.h/.cpp`: quadratura PCNT + debounce 35 ms + detecção longa 800 ms sem duplicação de eventos.
  - `hydra_ui_esp32_bridge.h/.cpp`: ponte segura entre `UiModel`, `paramMutex`/`syncGlobalParams()` e filas SPSC `HydraDspEsp32Adapter`.

---

## 5. Validação na Placa (COM15 — ESP32-S3 N16R8)

Gravação executada via `pio run -e esp32-s3-st7789 -t upload --upload-port COM15` (`631.936` bytes gravados, hash verificado).

### Boot e Inicialização Capturados na Serial

- `PSRAM Size: 8388608 bytes`
- `ST7789 Display initialized OK (240x240 PSRAM+DMA)`
- `ST7789 UI Task started on Core 0`
- `Audio Task: Hydra DSP adapter allocated` (+ `FrippEngine`, `BubblesEngine`, `FreeverbEngine`, `MelodyGen` alocados em PSRAM/RAM sem falhas)

### Telemetria DSP (`perf`) com ST7789 Ativo em Repouso

| Métrica | Valor Medido (`COM15`) |
| --- | --- |
| `Block Size` / `Sample Rate` | `128 frames` / `44100 Hz` |
| `Block Time Budget` | `2902.5 us` |
| `Blocks Processed` | `1174` |
| `Avg DSP Block Time` | `1606 us` |
| `Peak DSP Block Time` | `1969 us` |
| `DSP Budget Use (EMA)` | `53.8 %` |
| `DSP Budget Overruns` | `0` |
| `I2S TX Write Failures` | `0` |
| `Full Work Overruns` | `0` |
| `Internal Free Heap` | `112168 bytes` |
| `SPIRAM (PSRAM) Free` | `1543468 bytes` |
| `Audio Stack Min Free` | `27260 bytes` |

### Itens Pendentes de Inspeção Física Humana

- Confirmação visual no painel ST7789 (orientação, cores, ausência de tearing e brilho do backlight).
- Teste manual prolongado (5 min) com giro intenso do encoder físico, cliques de Tap Tempo, troca de modos, presets e SW Bypass.
