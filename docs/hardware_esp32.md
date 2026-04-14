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

A interface de controle usa comandos curtos no Serial Monitor.

### Mixer e sistema

- `vol 0..100`
- `mix 0..100`
- `byp 0|1`
- `src 0..2`
- `bmp 30..300`
- `list`
- `?`
- `load clean|lofi|dub|broken`

### Tape engine

- `dly 10..2000`
- `fbk 0..100`
- `hds 1..7`
- `mus 0|1`
- `mod 0|1`
- `tps 0..100`
- `tpa 0..100`
- `drv 0..100`
- `nlv 0..100`
- `hbp 0..100`
- `azm 0..100`
- `ngt 0..100`
- `red 0..100`

### Modulação (wow/flutter)

- `ftd 0..100`
- `ftr 0..100`
- `wwd 0..100`
- `wwr 0..100`
- `dps 0..100`

### Cor e timbre

- `gfc 0|1`
- `ton 0..100`

### Gerador de melodia

- `wvf 0..3`
- `ptc <midi note>`
- `scl 0..4`
- `moo 0..100`
- `rtm 0..100`
- `eno 0|1`

## Notas de operação

- Taxa de amostragem alvo: 48 kHz.
- Botão Boot (GPIO 0) alterna bypass.
