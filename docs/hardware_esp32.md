# Guia de integração de hardware (ESP32-S3)

Este guia preserva as instruções de uso em hardware físico (board, pinout e CLI serial).

## Requisitos de hardware

- **MCU:** ESP32-S3 (validado em Freenove ESP32-S3 WROOM N16R8)
- **DAC I2S:** PCM5102 (ou compatível)
- **Botão Boot:** usado para bypass

## Pinout padrão

| Sinal | GPIO |
|---|---:|
| I2S BCLK | 15 |
| I2S LRCK | 16 |
| I2S DOUT | 17 |
| I2S DIN | 18 |
| RGB LED | 48 |
| Boot Button | 0 |

## Build/flash no hardware

```bash
pio run
pio run --target upload
pio device monitor
```

## CLI serial (115200 baud)

A interface de controle usa comandos curtos no Serial Monitor. Os comandos de porcentagem aceitam `0..100`, são clampados na entrada e alimentam o core DSP na escala moderna. Profundidades de wow/flutter/dropout são amigáveis na CLI (`0..100%`) e mapeadas internamente para a faixa musical do core.

### Mixer e sistema

- `vol 0..100`: volume master do hardware.
- `mix 0..100`: dry/wet do path de textura/delay.
- `byp 0|1`: bypass.
- `src 0..2`: `0=MP3`, `1=Synth`, `2=I2S`.
- `bmp 30..300`: BPM.
- `list`: dashboard.
- `?`: ajuda.
- `load clean|lofi|dub|broken`: presets calibrados para a escala moderna.

### Tape engine

- `dly 10..2000`: delay em ms.
- `fbk 0..100`: feedback.
- `hds 1..7`: bitmask de heads.
- `mus 0|1`: heads livres ou musicais.
- `mod 0|1`: `0=Saturator/texture`, `1=Tape delay`.
- `tps 0..100`: tape speed.
- `tpa 0..100`: tape age.
- `drv 0..100`: drive/saturação.
- `nlv 0..100`: hiss/noise; `0` remove o hiss gerado.
- `hbp 0..100`: head bump.
- `azm 0..100`: erro de azimuth.
- `ngt 0..100`: threshold do gate.
- `red 0..100`: redução do gate.

### Modulação (wow/flutter/dropout)

- `ftd 0..100`: porcentagem de flutter depth; mapeia para `0..45` no core.
- `ftr 0.1..20`: flutter rate em Hz reais.
- `wwd 0..100`: porcentagem de wow depth; mapeia para `0..35` no core.
- `wwr 0.1..5`: wow rate em Hz reais.
- `dps 0..100`: porcentagem de dropout severity; mapeia para `0..40` no core.

### Cor, timbre e reverb

- `gfc 0|1`: guitar focus.
- `ton 0..100`: tone.
- `spr 0|1`: spring reverb.
- `spm 0..100`: spring mix.
- `spd 0..100`: spring decay.
- `spf 0..100`: spring damping.

### Gerador de melodia

- `wvf 0..3`
- `ptc <midi note>`
- `scl 0..4`
- `moo 0..100`
- `rtm 0..100`
- `eno 0|1`

## Teste rápido de noise/wow/flutter no ESP32-S3

No monitor serial:

```text
mod 1
mix 70
nlv 60
ftd 80
wwd 80
ftr 6
wwr 0.8
list
```

Resultado esperado: `nlv` aumenta o hiss sem clipar; `ftd/ftr` adicionam jitter/vibrato rápido; `wwd/wwr` adicionam drift lento, mais óbvio em modo delay (`mod 1`). Use `nlv 0`, `ftd 0` e `wwd 0` para conferir o contraste.

## Web/WASM e artefatos gerados

A fonte da UI web fica em `host/web`. A pasta `build-web/web` é saída de build e pode estar desatualizada em checkouts locais; regenere-a com:

```bash
emcmake cmake -S . -B build-web -DBUILD_WEB=ON
cmake --build build-web --target hydra_dsp_web
python3 -m http.server 8080 --directory build-web/web
```

Na web, teste noise/wow/flutter com os sliders `Hiss Floor`, `Wow Depth`, `Wow Rate`, `Flutter Depth` e `Flutter Rate`. Os mapas em `host/web/main.js` devem continuar alinhados com `include/hydra_dsp.h` (`delayWet=15`, `activeHeads=16`, `bpm=17`, `reset=29`).

## Notas de operação

- O caminho de hardware ESP32-S3 usa `SAMPLE_RATE 44100` em `src/main.cpp` para reduzir carga de CPU/DMA no firmware atual.
- A web normalmente roda a 48 kHz porque usa o `AudioContext` do navegador; os testes nativos também usam 48 kHz para equivalência com WASM.
- Ping-pong usa cross-feedback L/R no core DSP; no firmware ESP32, o comando serial `ppg 1` ativa o modo e `ppg 0` desativa.
