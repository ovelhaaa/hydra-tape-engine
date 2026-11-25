# Hydra Vintage Live v0.1 — teste com instrumento

Firmware experimental: Classic C=1, F2=2, F3=0, F4=1, F5=1.
Controle Vintage: as mesmas flags, F4=0 e F5=0. Produção permanece inalterada.
Estado físico: **PENDING_PHYSICAL**. Build e testes desktop não aprovam deadline
nem ausência de artefatos audíveis. Não há flash automático.

## Compilar, gravar e recuperar

Use Python 3.13 e PlatformIO 6.2.0. No checkout do SHA indicado no manifesto:

```powershell
python -m pip install platformio==6.2.0
python -m platformio run -e esp32-s3-hydra-live
python -m platformio device list
python -m platformio run -e esp32-s3-hydra-live -t upload --upload-port COM_PORTA
python -m platformio device monitor --port COM_PORTA --baud 115200
```

Substitua `COM_PORTA` pela porta real. Feche o monitor antes do upload.
O artifact contém `firmware.bin`, ELF, mapa quando disponível, identidade,
SHA-256 e relatórios. Compare os hashes com `sha256.json` antes de utilizar.
O comando PlatformIO grava também bootloader e partições corretos; não tente
adivinhar offsets para gravar somente o `.bin` em uma placa virgem.
Não execute erase-flash: preserve o armazenamento de presets.

Rollback musical imediato:

```powershell
python -m platformio run -e esp32-s3-hydra-vintage-reference
python -m platformio run -e esp32-s3-hydra-vintage-reference -t upload --upload-port COM_PORTA
```

Para recuperar a produção original, use um checkout separado de
`7d8c40a3a925082995f919c0d7c12aa8978d5d21` e compile/grave
`esp32-s3-devkitc-1`. Preserve o checkout Live e seu manifesto para comparação.

Placa: ESP32-S3 N16R8, 240 MHz, PCM1808/PCM5102, estéreo, 44.100 Hz,
128 frames. Pinagem e circuito são os existentes. Conecte a fonte à entrada
de áudio já validada e monitore a saída com volume inicialmente baixo.

## Preparação por CLI

Os comandos abaixo existem em `src/main.cpp`. Envie um por linha:

```text
identity
src 2
gen 0
byp 0
mod 1
rev 0
wrp 0
frz 0
frv 0
mus 0
mix 35
drv 25
fbk 45
hds 4
dly 350
perf
```

`identity` deve mostrar o SHA esperado, nome do ambiente, flags, 44100/128,
PSRAM/flash e I2S inicializado. `src 2` seleciona a entrada I2S.

## Sete testes

1. **Áudio básico:** toque notas/acordes; varie `mix 0`, `mix 50`, `mix 100`,
   `drv 10`, `drv 70`, `fbk 35`, `fbk 75`. Procure clipping inesperado,
   ruído digital, falhas e mudanças abruptas. Volte a `mix 35` e `drv 25`.
2. **Tape Echo:** em `mus 0`, use `dly 120`, `dly 350`, `dly 900`.
   Toque e deixe a cauda soar; verifique continuidade e estabilidade.
3. **Cabeçotes:** alterne `hds 1`, `hds 7`, `hds 4`, `hds 3`, `hds 4`
   durante notas e caudas. Verifique cliques/saltos de ganho. Repita durante
   o glide do teste seguinte. Os intervalos exatos 2/4/6 ms são automatizados
   no contrato; a CLI manual serve à avaliação no instrumento.
4. **Capstan:** use `mus 1`, `bpm 120`, depois `bpm 85`, `bpm 155`,
   `bpm 120` com caudas audíveis; interrompa um glide com outro comando.
   Confirme aceleração/desaceleração musical e chegada ao tempo solicitado.
   Use `transport status` para conferir o clock, que não deve acompanhar a rampa.
5. **Freeze:** `fbk 95`, `frz 1`, `bpm 85`, `hds 7`, `bpm 155`,
   `frz 0`. Verifique conteúdo preservado enquanto congelado e liberação
   contínua, sem estouro. Repita em outras combinações de cabeçotes.
6. **Reverse:** `frz 0`, `fbk 65`, `rev 1`, `revmode 0`, `revdiv 0`;
   mude `bpm 90` e `bpm 140`. Confira sincronismo e estabilidade por ouvido
   e `transport status`. Depois `rev 0`. Experimente também `ppg 1` e `wrp 1`,
   retornando ambos a zero após o controle de compatibilidade.
7. **Estresse:** toque com `fbk 95` e, brevemente, `fbk 100`; alterne BPM,
   cabeçotes, Mix/Drive e Freeze. Deixe a cauda por 60 segundos. Compare
   `perf` antes/depois. Use `bench 4096 256` para obter P50/P95/P99/peak,
   overruns, I2S, memória e custo de parâmetros. Para medir transições no mesmo
   ensaio, use `bench start 16384 256`, envie as mudanças de BPM/cabeçotes/
   Mix/Drive/Freeze durante os próximos 48 segundos e consulte `bench result`.
   Se ainda coletando, o comando informa NOT READY; repita após completar.
   Não reinicie uma captura ativa. Esses subcomandos reutilizam os contadores
   e liberam a CLI durante a medição, sem trabalho extra na audio task.

## Registrar e comparar

Deadline por bloco: **2.902,49 µs**. Exija zero overrun DSP/full-work e zero
falha I2S; registre P50/P95/P99/peak, fullWorkPeak, MaxParamUpdUs, heap/PSRAM
e stack mínima via `perf`, `bench 4096 256` e `audiohealth`.
O scan de stack ocorre fora da audio task. Se P99/peak se aproximarem do prazo,
registre a margem; não deduza custo físico dos resultados Native.

Repita com Vintage Reference na mesma placa, fonte, níveis e parâmetros.
Documente a escuta A/B das transições e equivalência em repouso. Não é exigida
nova sessão cega para esta build experimental de bancada.

| Teste | PASS/FAIL | Observação |
|---|---|---|
| Identidade/boot/I2S | PENDING | |
| Áudio básico | PENDING | |
| Tape Echo | PENDING | |
| Cabeçotes durante glide | PENDING | |
| Capstan durante crossfade | PENDING | |
| Freeze e liberação | PENDING | |
| Reverse/Transport | PENDING | |
| Estresse/cauda 60 s | PENDING | |
| A/B Live/Vintage e custo | PENDING | |

Anexe SHA, placa, data, logs `identity`/`perf`/`bench`/`audiohealth`, valores dos
contadores, preferência/artefatos ouvidos e gravação curta quando possível.
