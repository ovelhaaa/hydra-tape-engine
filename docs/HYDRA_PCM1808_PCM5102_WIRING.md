# Hydra Live v0.1 â€” PCM1808 / PCM5102

2026-11-25. Base Live/PR #71: `f3bd0cdae7ef662f25fa68876cdf06442d654724`.
Escopo: interface elÃ©trica, driver e diagnÃ³stico. Core, parÃ¢metros, ABI,
presets e goldens preservados. F4/F5 incorporadas na Live experimental;
PR #71 draft, sem promoÃ§Ã£o Ã  main ou aprovaÃ§Ã£o fÃ­sica implÃ­cita.

## Estado da evidÃªncia

`READY_FOR_HARDWARE_WIRING â€” PENDING_PHYSICAL_CLOCK_VERIFICATION` designa
o gate de firmware apÃ³s builds/testes. **Antes de soldar/alimentar**, ainda Ã©
obrigatÃ³rio identificar headers, reguladores e jumpers dos exemplares GY e
conferir IO14 no header da YD N16R8. NÃ£o foram fornecidos fotos/esquemas desses
exemplares: `PENDING_MODULE_INSPECTION`. NÃ£o hÃ¡ mediÃ§Ã£o fÃ­sica neste relatÃ³rio.
NÃ£o interpretar o nome do ambiente DevKitC como identificaÃ§Ã£o da placa YD.

## Fontes primÃ¡rias

- [TI PCM1808 SLES177B](https://www.ti.com/lit/ds/symlink/pcm1808.pdf): alimentaÃ§Ã£o, modos, entrada e formato.
- [TI PCM510xA SLAS859C](https://www.ti.com/lit/ds/symlink/pcm5102a.pdf): PLL, straps, alimentaÃ§Ã£o e saÃ­da.
- [Driver IDF 5.5.2](https://github.com/espressif/esp-idf/blob/v5.5.2/components/driver/deprecated/i2s_legacy.c): `i2s_check_set_mclk`, `i2s_set_pin`, `i2s_set_slot_legacy`, configuraÃ§Ã£o Philips e clock.
- [HAL](https://github.com/espressif/esp-idf/blob/v5.5.2/components/hal/i2s_hal.c), [divisÃ£o fracionÃ¡ria](https://github.com/espressif/esp-idf/blob/v5.5.2/components/hal/hal_utils.c), [LL S3](https://github.com/espressif/esp-idf/blob/v5.5.2/components/hal/esp32s3/include/hal/i2s_ll.h).
- [ESP32-S3-WROOM-1](https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf): GPIO/straps/OPI; nÃ£o Ã© esquema da YD.

## LigaÃ§Ãµes digitais propostas

Os nomes abaixo identificam **funÃ§Ãµes do chip**, nÃ£o headers GY comprovados.

| ESP32-S3 | MÃ³dulo | FunÃ§Ã£o a identificar | DireÃ§Ã£o |
|---|---|---|---|
| GPIO14 | PCM1808 | SCK/SCKI (chip 6) | ESP â†’ ADC, MCLK |
| GPIO15 | PCM1808 | BCK (chip 8) | ESP â†’ ADC |
| GPIO15 | PCM5102A | BCK (chip 13) | ESP â†’ DAC |
| GPIO16 | PCM1808 | LRC/LRCK (chip 7) | ESP â†’ ADC |
| GPIO16 | PCM5102A | LCK/LRCK (chip 15) | ESP â†’ DAC |
| GPIO17 | PCM5102A | DIN (chip 14) | ESP â†’ DAC |
| GPIO18 | PCM1808 | DOUT (chip 9) | ADC â†’ ESP |
| GND | Ambos | AGND/DGND conforme circuito | terra comum |

Conferir orientaÃ§Ã£o do chip, part number, headers e continuidade desligado.
ADC e DAC sÃ£o slaves: sÃ³ o ESP dirige BCLK/LRCK; ADC master causaria contenÃ§Ã£o.
NÃ£o ligar MCLK ao DAC nem DOUT do ADC diretamente ao DIN do DAC.

## AlimentaÃ§Ã£o, straps e sinais analÃ³gicos

| Item | Requisito do chip / verificaÃ§Ã£o do mÃ³dulo |
|---|---|
| PCM1808 VCC (3) | analÃ³gico 5 V nominal, 4,5â€“5,5 V; conferir trilhas/regulador |
| PCM1808 VDD (4) | digital 3,3 V, 3,0â€“3,6 V; nunca 5 V |
| PCM1808 MD0 (10), MD1 (11) | ambos LOW/GND â†’ slave; medir resistores/jumpers reais |
| PCM1808 FMT (12) | LOW â†’ IÂ²S 24 bits |
| PCM1808 VINL (13), VINR (14) | linha, full-scale tÃ­pico 3 Vpp no chip; conferir acoplamento/atenuaÃ§Ã£o |
| PCM1808 VREF (1) | referÃªncia interna, nÃ£o entrada nem alimentaÃ§Ã£o externa |
| PCM5102A AVDD/CPVDD | 3,3 V no chip; VIN/5V no header sÃ³ se regulador identificado |
| PCM5102A DVDD | 1,8 ou 3,3 V conforme circuito; verificar domÃ­nio digital |
| PCM5102A FMT (16) | LOW â†’ IÂ²S |
| PCM5102A SCK (12) | LOW/GND para PLL de BCK; conferir pulldown/jumper, nÃ£o flutuar |
| PCM5102A XSMT (17) | HIGH no domÃ­nio digital vÃ¡lido para desmutar, nunca 5 V |
| PCM5102A FLT/DEMP | registrar straps existentes, nÃ£o deixar controles flutuantes |
| PCM5102A OUTL/OUTR (6/7) | linha atÃ© 2,1 Vrms nominal; entrada de linha, nÃ£o falante/headphone direto |
| GND | terras compartilhados; evitar corrente USB/LED no retorno analÃ³gico |

Placas GY podem conter reguladores, unir rails ou alterar semÃ¢ntica dos jumpers.
Fotografar ambos os lados, identificar reguladores e rastrear headerâ†’chip com
multÃ­metro **desligado**. Medir rails isoladamente com fonte limitada antes de
conectar ESP. NÃ£o unir duas saÃ­das de regulador (3V3) sem esquema que permita.
GPIOs ESP nÃ£o toleram 5 V; conferir tambÃ©m pullups nas redes digitais.

## Clock e formato efetivos

Ambiente exclusivo `esp32-s3-hydra-live-hwtest`: pioarduino 55.03.36,
Arduino-ESP32 3.3.6 / headers IDF 5.5.2. A plataforma local chamada `espressif32`
tambÃ©m resolve para essa versÃ£o; nÃ£o inferir versÃ£o sÃ³ pelo nome.
Legacy `driver/i2s.h` suporta `mck_io_num`; nÃ£o misturar novo driver no perifÃ©rico.

IÂ²S0 MASTER TX|RX, Philips, estÃ©reo, slots 32 bits, WS LOW=L/HIGH=R.
`mck_io_num=14`, `mclk_multiple=256`, `bits_per_chan=32`, `use_apll=false`,
`fixed_mclk=0` (nÃ£o seleciona clock no S3). Fonte default Ã© PLL_160M;
S3 nÃ£o possui APLL IÂ²S como o ESP32 clÃ¡ssico. Driver compartilha BCK/WS
e coloca RX interno em slave do TX. DMA configurado em 4Ã—512 frames;
driver pode limitar frames por descritor. Erro de init/pin/clock/zero-DMA
impede a criaÃ§Ã£o da audio task no hwtest.

HAL procura fraÃ§Ã£o com denominador de 9 bits:

`160000000 / (14 + 76/441) = 11289600 Hz` nominal exato (441 < 512).

BCLK=MCLK/4=2822400 Hz; LRCK=BCLK/64=44100 Hz; MCLK/LRCK=256.
NÃ£o hÃ¡ conversÃ£o silenciosa para 48 kHz. ExatidÃ£o absoluta segue cristal/PLL;
hÃ¡ jitter fracionÃ¡rio. `i2s_get_clk` e compilaÃ§Ã£o nÃ£o medem os pinos.

PCM1808 emite 24 bits signed MSB-first um BCLK apÃ³s WS. Payload RX em bits 31:8;
os oito bits finais sÃ£o descartados. Sign extension explÃ­cita por subtraÃ§Ã£o
de 2^24, float=PCM24/8388608, sem ganho compensatÃ³rio. Word 0=L, 1=R na
configuraÃ§Ã£o S3; estÃ­mulo fÃ­sico por canal confirma. RX parcial conserva sÃ³
frames completos e zera o restante. TX normal mantÃ©m saturaÃ§Ã£o e escala
`masterVolumeÃ—0,90` existentes. Testes determinÃ­sticos validam conversÃ£o em
memÃ³ria, nÃ£o o alinhamento elÃ©trico no fio.

## GPIO e montagem removÃ­vel

GPIO14 estÃ¡ livre na Live: OLED1/2, encoder4/5/6, botÃ£o7, IÂ²S15â€“18,
RGB48, UART43/44, USB19/20. Evitados straps0/3/45/46 e flash/OPI26â€“37.
GPIO1 jÃ¡ Ã© SDA. RestriÃ§Ã£o MCLK0/1/3 do ESP32 clÃ¡ssico nÃ£o vale para S3.
Confirmar **IO14** exposto e livre na placa YD real. AlteraÃ§Ãµes locais ST7789
nÃ£o commitadas pertencem a outra tarefa, fora deste firmware; futura integraÃ§Ã£o
exige nova auditoria de GPIO.

Veroboard: sockets removÃ­veis, headers polarizados, cortes de trilha conferidos
com lupa e continuidade. Clocks curtos, GND prÃ³ximo, stubs BCLK/WS mÃ­nimos;
separar clocks de LIN/RIN e cabo de guitarra. Desacoplamento conforme datasheet
e componentes jÃ¡ existentes, sem presumir valores de referÃªncia/charge pump.
Evitar loops de terra e corrente pulsada no retorno analÃ³gico, preservando
terra comum digital. Medir ringing antes de dimensionar terminaÃ§Ã£o.

## Checklist elÃ©trico

1. Identificar YD/IO14, ICs GY, headers, reguladores, polaridades e straps.
2. Desligado: continuidade de cada sinal e ausÃªncia de curtos rails/GND,
   cortes da stripboard e 5 V em rede digital. Pulls podem falsear buzzer:
   conferir resistÃªncia e circuito.
3. Confirmar MD0=MD1=FMT=0 ADC; FMT=0, SCK=0 e XSMT vÃ¡lido DAC.
4. MÃ³dulos isolados em fonte limitada: medir rails e nÃ­veis digitais reais.
5. Desligar para interligar, GND primeiro; evitar USB/5V de duas fontes em
   paralelo/backfeed. Energizar domÃ­nios digitais antes de dirigir sinais a
   mÃ³dulos; evitar hot-plug e GPIO alimentando mÃ³dulo desligado.
6. Amplificador mutado/baixo volume, fonte de linha controlada, depois clocks.
   Mutar amplificador antes de desligar alimentaÃ§Ã£o/clocks.

## Teste fÃ­sico

Build sem flash: `python -m platformio run -e esp32-s3-hydra-live-hwtest`.
Usar artifact com bootloader/partiÃ§Ãµes correspondentes, sem adivinhar offsets
ou apagar presets. Upload Ã© posterior/manual. Rollback: artifact da PR #71 e
ambientes Live/Vintage Reference inalterados; rollback antigo nÃ£o gera novo
MCLK, portanto desconectar ADC quando necessÃ¡rio.

CLI 115200, um comando por linha:

```text
identity
hw silence
hw left
hw right
hw tone
hw silence
adc
hw monitor
adc
audiohealth
hw normal
src 2
bench start 4096 128
bench result
perf
```

Esperar tÃ©rmino de bench antes de result. Consultar help existente.
Rotas hw forÃ§am ADC na mesma tarefa/driver. Tom 440 Hz polinomial, pico0,1
(-20 dBFS), erro de aproximaÃ§Ã£o <0,00016 antes do ganho. Monitor ganho fixo0,25
(-12,04 dB), sem Tape/tanh/gate/Freeverb/masterVolume. `hw normal` restaura src
anterior e DSP; cauda DSP fica pausada durante diagnÃ³stico. Bypass preservado.
Baixar volume analÃ³gico antes de trocar rota.

`adc`: janela128 blocos (~0,372s), frames/samples recebidos, read-errors,
short-reads, pico/RMS/DC e contagem abs>=0,99 por canal. NÃ£o Ã© detector contÃ­nuo
de overflow DMA; sucesso de read nÃ£o prova ausÃªncia de perdas. Timeout mantÃ©m
mailbox pendente: repetir adc para recolher. SÃ³ CLI imprime/calcula sqrt;
callback faz somas/multiplicaÃ§Ãµes limitadas durante captura, sem alocaÃ§Ã£o/libm
por amostra. Estados adicionais <128 bytes alÃ©m dos buffers existentes;
custo de CPU nÃ£o medido. Captura pode afetar benchmark: medir separadamente.

OsciloscÃ³pio, pontaÃ—10/terra curto: medir em IO14/SCKI, IO15/BCK e IO16/LRCK
no ESP e nos chips. Registrar screenshot, escala, amplitude, duty/ringing,
frequÃªncia e identity. Esperado 11,2896MHz / 2,8224MHz / 44,1kHz; razÃµes4 e64,
256 MCLK por frame, 64 BCLK por frame. Capturar simultaneamente coerÃªncia,
WS e atraso de um BCLK atÃ© MSB. Analisador lÃ³gico preferÃ­vel >=100MS/s para
MCLK, decoder IÂ²S32 estÃ©reo MSB-first; registrar precisÃ£o do instrumento.
Captura lenta pode aliasar MCLK; nÃ£o confiar sÃ³ em autodetecÃ§Ã£o.

Injetar linha baixa sÃ³ LIN, depois RIN: confirmar ordem, sinal, RMS/DC,
incluindo amplitudes pequenas; subir gradualmente sem exceder limites do mÃ³dulo.
Comparar words decodificados com PCM24; terminar entrada silenciosa conforme
circuito para medir ruÃ­do. Ouvir DAC/monitor/Live e A/B com rollback na mesma
fonte/configuraÃ§Ã£o. Registrar P50/P95/P99/peak, fullWorkUs, overruns/IÂ²S,
heap/PSRAM/stack e atualizaÃ§Ãµes; deadline2902,49us. Hot path exige esse teste
fÃ­sico e escuta documentada antes de promoÃ§Ã£o.

## Guitarra e pendÃªncias

PCM1808 recebe linha. Guitarra passiva requer buffer Hi-Z (ordem de1Mohm,
dimensionado ao instrumento), ganho/headroom, proteÃ§Ã£o, acoplamento e bias
compatÃ­veis. Conferir impedÃ¢ncia e capacitores existentes; nÃ£o aterrar bias
interno por engano. Sem buffer, comeÃ§ar com gerador/interface de linha e
amplitude baixa medida no chip. NÃ£o conectar potÃªncia, phantom ou excesso
de tensÃ£o. Firmware nÃ£o implementa prÃ©-amplificador.

Pendentes: fotos/mapa headerâ†’chip, rails/straps e IO14 na YD (antes de soldar);
clocks/nÃ­veis/coerÃªncia/ringing; alinhamento real/LR/clipping/ruÃ­do;
escuta DAC/monitor/Live e deadline fÃ­sico. Nenhuma dessas verificaÃ§Ãµes foi
inferida de build ou simulada como mediÃ§Ã£o.
