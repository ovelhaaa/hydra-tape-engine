# Multi-Head Tape Delay Emulator

This project implements a multi-head tape delay simulation on the ESP32-S3 microcontroller using the Arduino framework. It emulates the characteristics of vintage analog tape echo units, including wow, flutter, tape saturation, noise, and dropouts.

## Hardware Requirements

- **Microcontroller:** ESP32-S3 (Tested on Freenove ESP32-S3 WROOM with N16R8, i.e., 16MB Flash, 8MB PSRAM)
- **Audio Output:** PCM5102 I2S DAC (or compatible)
- **Audio Input:** None (Self-generating melody for testing) / _Note: The code is set up for I2S output, input is generated internally._

### Pinout (Default)

| Signal      | GPIO Pin          |
| :---------- | :---------------- |
| I2S BCLK    | 15                |
| I2S LRCK    | 16                |
| I2S DOUT    | 17                |
| I2S DIN     | 18                |
| RGB LED     | 48                |
| Boot Button | 0 (Toggle Bypass) |

## Features

- **Tape Physics Simulation:**
  - **Flutter & Wow:** Simulates motor speed irregularities.
  - **Saturation (Drive):** Soft clipping and tape saturation effects.
  - **Tape Noise:** Hiss generation with filtering.
  - **Dropouts:** Random volume drops simulating dirty or damaged tape.
- **Multi-Head Delay:**
  - Three virtual playback heads (A, B, C) with different delay times relative to the main delay time.
  - Head selection via serial commands.
- **Audio Generation:**
  - Built-in melody generator with selectable waveforms (Sine, Sawtooth, Triangle, Square).

## Usage

The device generates audio automatically. You can control parameters via the Serial Monitor (115200 baud).

### Serial Commands (CLI)

The device is controlled via a Serial Command Line Interface (CLI) at **115200 baud**.
Commands are generally 3 letters followed by a value. Example: `vol 80` (Sets volume to 80%).

**IMPORTANT:** Most parameters now accept values from **0 to 100**. This makes it easier to type quickly without floating points.

#### Mixer & System

| Command  | Argument   | Description                                   |
| :------- | :--------- | :-------------------------------------------- |
| **vol**  | `0 - 100`  | Master Volume (%)                             |
| **mix**  | `0 - 100`  | Dry/Wet Mix (0=Dry, 100=Wet)                  |
| **byp**  | `0` or `1` | Bypass Effect (1=Bypass, 0=Active)            |
| **src**  | `0 - 2`    | Audio Source (0=MP3, 1=Synth, 2=I2S Input)    |
| **bmp**  | `30 - 300` | Global BPM (affects melody & musical heads)   |
| **list** | -          | Show current status dashboard                 |
| **?**    | -          | Show help command list                        |
| **load** | `name`     | Load preset: `clean`, `lofi`, `dub`, `broken` |

#### Tape Engine

| Command | Argument    | Description                                                             |
| :------ | :---------- | :---------------------------------------------------------------------- |
| **dly** | `10 - 2000` | Delay Time in milliseconds                                              |
| **fbk** | `0 - 100`   | Feedback amount (Inputs >100 may self-oscillate)                        |
| **hds** | `1 - 7`     | Active Heads Bitmask (1=Head1, 2=Head2, 4=Head3). Sum for combinations. |
| **mus** | `0` or `1`  | Head Mode (0=Free, 1=Musical/Rhythmic spacing)                          |
| **mod** | `0` or `1`  | Engine Mode (0=Saturator/Preamp only, 1=Tape Delay)                     |
| **tps** | `0 - 100`   | Tape Speed / Varispeed (0=Stop, 50=Normal, 100=Double)                  |
| **tpa** | `0 - 100`   | Tape Age / Degradation (0=New, 100=Old/Dark)                            |
| **drv** | `0 - 100`   | Drive / Saturation (Mapped to internal gain 1x-10x)                     |
| **nlv** | `0 - 100`   | Noise Level / Hiss                                                      |
| **hbp** | `0 - 100`   | Head Bump (Low end resonance intensity)                                 |
| **azm** | `0 - 100`   | Azimuth alignment error (50 = Center/Perfect)                           |
| **ngt** | `0 - 100`   | Noise Gate Threshold (Sensitivity)                                      |
| **red** | `0 - 100`   | Gate Reduction Amount (0=None, 100=Full Silence)                        |

#### Modulation (Wow & Flutter)

| Command | Argument  | Description                           |
| :------ | :-------- | :------------------------------------ |
| **ftd** | `0 - 100` | Flutter Depth (Fast wobble intensity) |
| **ftr** | `0 - 100` | Flutter Rate (Mapped to 0.1Hz - 20Hz) |
| **wwd** | `0 - 100` | Wow Depth (Slow wobble intensity)     |
| **wwr** | `0 - 100` | Wow Rate (Mapped to 0.1Hz - 5Hz)      |
| **dps** | `0 - 100` | Dropout Severity                      |

#### Color & Tone

| Command | Argument   | Description                         |
| :------ | :--------- | :---------------------------------- |
| **gfc** | `0` or `1` | Guitar Focus Mode (Bandpass filter) |
| **ton** | `0 - 100`  | Tone Control (0=Dark, 100=Bright)   |

#### Melody Generator

| Command | Argument    | Description                               |
| :------ | :---------- | :---------------------------------------- |
| **wvf** | `0 - 3`     | Waveform (0=Sine, 1=Saw, 2=Tri, 3=Square) |
| **ptc** | `Midi Note` | Root Key / Pitch                          |
| **scl** | `0 - 4`     | Scale Type                                |
| **moo** | `0 - 100`   | Mood (Probabilistic factor)               |
| **rtm** | `0 - 100`   | Rhythm Density                            |
| **eno** | `0` or `1`  | Eno Mode (Ambient preset activation)      |

### Boot Button

Pressing the Boot button (GPIO 0) toggles the effect bypass.

## Building and Flashing

1.  Install [PlatformIO](https://platformio.org/).
2.  Open the project directory.
3.  Build: `pio run`
4.  Upload: `pio run --target upload`
5.  Monitor: `pio device monitor`

## Technical Details

- **Sampling Rate:** 48kHz
- **Buffer:** Uses PSRAM for large delay buffers (up to ~2.5 seconds).
- **Interpolation:** Hermite interpolation for variable speed playback.
- **Architecture:**
  - Core 0: Audio processing task (High priority).
  - Core 1: Control loop and Serial communication.

## Web build (Emscripten)

This repository now supports a browser host that reuses the same C++ core (`dsp/core`) via the C ABI (`include/hydra_dsp.h`) and WebAssembly.

### Reproducible commands

1. Configure and build (requires Emscripten SDK in PATH):

```bash
emcmake cmake -S . -B build-web -DBUILD_WEB=ON
cmake --build build-web --target hydra_dsp_web
```

Build outputs:

- `build-web/web/hydra_dsp.js` (Emscripten JS bindings/runtime)
- `build-web/web/hydra_dsp.wasm` (DSP core)
- `build-web/web/index.html`
- `build-web/web/main.js`
- `build-web/web/hydra-processor.js`

2. Serve locally:

```bash
python3 -m http.server 8080 --directory build-web/web
```

3. Run demo:

- Open `http://127.0.0.1:8080`.
- Click **Start Audio**.
- Load an audio file in the file picker and press play.
- Click **Connect FX** to route through the WASM worklet.
- Use bypass/reset and the main parameters.
- Use **Render Offline** for deterministic render through `OfflineAudioContext` and download WAV.


### CI/CD (GitHub Pages)

A workflow was added in `.github/workflows/web-pages.yml`.

- Trigger: push on `main` (and manual `workflow_dispatch`).
- Steps: setup Emscripten, build with `BUILD_WEB=ON`, publish `build-web/web` to GitHub Pages.
- For the first run, enable Pages in repo settings (Source: **GitHub Actions**).

## Shared DSP architecture (PR3)

The final structure for shared processing and hosts is:

- `dsp/core/`: canonical DSP engine (`TapeCore`) + C ABI bridge (`hydra_dsp_c_api.cpp`).
- `host/native/`: native adapters and embeddings (e.g. ESP32 glue in `host/native/esp32`).
- `host/web/`: browser host files (`index.html`, `main.js`, worklet wiring).
- `include/`: public cross-host C API (`hydra_dsp.h`).
- `tests/`: native regression and native-vs-wasm equivalence tooling.

### Determinism and fixed conditions

To maximize reproducibility of regression/equivalence runs:

- deterministic pseudo-random seeds are used for noise/dropout generators on reset;
- test signals and block sizes are fixed;
- parameter automation schedule uses fixed frame indices and fixed values.

### Regression/equivalence tolerances (float32)

The initial documented tolerance for native-vs-wasm output equivalence is:

- `max_abs <= 1e-5`
- `RMSE <= 1e-6`

### Exact build/test commands

#### Native (Linux/macOS)

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

This runs:

- `core_smoke`
- `core_regression` (deterministic buffer, reset/state, simple automation)

#### WebAssembly + equivalence

```bash
emcmake cmake -S . -B build-web -DBUILD_WEB=ON -DBUILD_TESTING=ON
cmake --build build-web -j
ctest --test-dir build-web --output-on-failure
```

This additionally runs:

- `native_vs_wasm_equivalence` via `tests/core/compare_native_wasm.py`
  using the same input, parameters, and call order for both backends.

### Expected platform divergences

Even with shared source and deterministic setup, tiny numerical differences are expected due to:

- compiler/backend math optimizations (native vs wasm code generation),
- floating-point contraction/FMA differences,
- library-level transcendental implementation details.

The equivalence thresholds above are chosen to absorb these inevitable low-level differences while still enforcing behavioral parity.
