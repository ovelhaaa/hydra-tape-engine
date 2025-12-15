# Multi-Head Tape Delay Emulator

This project implements a multi-head tape delay simulation on the ESP32-S3 microcontroller using the Arduino framework. It emulates the characteristics of vintage analog tape echo units, including wow, flutter, tape saturation, noise, and dropouts.

## Hardware Requirements

*   **Microcontroller:** ESP32-S3 (Tested on Freenove ESP32-S3 WROOM with N16R8, i.e., 16MB Flash, 8MB PSRAM)
*   **Audio Output:** PCM5102 I2S DAC (or compatible)
*   **Audio Input:** None (Self-generating melody for testing) / *Note: The code is set up for I2S output, input is generated internally.*

### Pinout (Default)

| Signal | GPIO Pin |
| :--- | :--- |
| I2S BCLK | 15 |
| I2S LRCK | 16 |
| I2S DOUT | 17 |
| I2S DIN | 18 |
| RGB LED | 48 |
| Boot Button | 0 (Toggle Bypass) |

## Features

*   **Tape Physics Simulation:**
    *   **Flutter & Wow:** Simulates motor speed irregularities.
    *   **Saturation (Drive):** Soft clipping and tape saturation effects.
    *   **Tape Noise:** Hiss generation with filtering.
    *   **Dropouts:** Random volume drops simulating dirty or damaged tape.
*   **Multi-Head Delay:**
    *   Three virtual playback heads (A, B, C) with different delay times relative to the main delay time.
    *   Head selection via serial commands.
*   **Audio Generation:**
    *   Built-in melody generator with selectable waveforms (Sine, Sawtooth, Triangle, Square).

## Usage

The device generates audio automatically. You can control parameters via the Serial Monitor (115200 baud).

### Serial Commands

Type the command followed by a value (if applicable) and press Enter.

*   `list`: Shows current status and parameter values.
*   `heads <1-7>`: Select active playback heads (Bitmask).
    *   1: Head A (Short)
    *   2: Head B (Medium)
    *   4: Head C (Long)
    *   Examples: `3` (A+B), `7` (A+B+C)
*   `mode <delay|saturation>`: Toggle between Delay mode and Saturation (Preamp) only mode.
*   `time <10-2000>`: Delay time in milliseconds.
*   `feedback <0-110>`: Feedback amount (0-110%).
*   `mix <0-100>`: Dry/Wet mix.
*   `volume <0-100>`: Master volume.
*   `waveform <sine|sawtooth|triangle|square>`: Change oscillator waveform.
*   `drive <0-100>`: Tape saturation amount.
*   `flutterdepth <0-100>`: Intensity of high-frequency speed wobble.
*   `flutterrate <0-100>`: Speed of flutter.
*   `wowdepth <0-100>`: Intensity of low-frequency speed wobble.
*   `wowrate <0-100>`: Speed of wow.
*   `dropout <0-100>`: Severity of tape dropouts.
*   `noise <0-100>`: Tape hiss level.
*   `lpf <0-100>`: Output Low Pass Filter cutoff.
*   `noiselpf <0-100>`: Noise Low Pass Filter cutoff.

### Boot Button

Pressing the Boot button (GPIO 0) toggles the effect bypass.

## Building and Flashing

1.  Install [PlatformIO](https://platformio.org/).
2.  Open the project directory.
3.  Build: `pio run`
4.  Upload: `pio run --target upload`
5.  Monitor: `pio device monitor`

## Technical Details

*   **Sampling Rate:** 48kHz
*   **Buffer:** Uses PSRAM for large delay buffers (up to ~2.5 seconds).
*   **Interpolation:** Hermite interpolation for variable speed playback.
*   **Architecture:**
    *   Core 0: Audio processing task (High priority).
    *   Core 1: Control loop and Serial communication.
