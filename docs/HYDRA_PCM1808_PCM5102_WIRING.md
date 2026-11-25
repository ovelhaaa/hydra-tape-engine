# Hydra Live v0.1 — PCM1808 / PCM5102

2026-11-25. Base Live/PR #71: `f3bd0cdae7ef662f25fa68876cdf06442d654724`.
Escopo: interface elétrica, driver e diagnóstico. Core, parâmetros, ABI,
presets e goldens preservados. F4/F5 incorporadas na Live experimental;
PR #71 draft, sem promoção à main ou aprovação física implícita.

## Estado da evidência

`READY_FOR_HARDWARE_WIRING — PENDING_PHYSICAL_CLOCK_VERIFICATION` designa
o gate de firmware após builds/testes. **Antes de soldar/alimentar**, ainda é
obrigatório identificar headers, reguladores e jumpers dos exemplares GY e
conferir IO14 no header da YD N16R8. Não foram fornecidos fotos/esquemas desses
exemplares: `PENDING_MODULE_INSPECTION`. Não há medição física neste relatório.
Não interpretar o nome do ambiente DevKitC como identificação da placa YD.

## Fontes primárias

- [TI PCM1808 SLES177B](https://www.ti.com/lit/ds/symlink/pcm1808.pdf): alimentação, modos, entrada e formato.
- [TI PCM510xA SLAS859C](https://www.ti.com/lit/ds/symlink/pcm5102a.pdf): PLL, straps, alimentação e saída.
- [Driver IDF 5.5.2](https://github.com/espressif/esp-idf/blob/v5.5.2/components/driver/deprecated/i2s_legacy.c): `i2s_check_set_mclk`, `i2s_set_pin`, `i2s_set_slot_legacy`, configuração Philips e clock.
- [HAL](https://github.com/espressif/esp-idf/blob/v5.5.2/components/hal/i2s_hal.c), [divisão fracionária](https://github.com/espressif/esp-idf/blob/v5.5.2/components/hal/hal_utils.c), [LL S3](https://github.com/espressif/esp-idf/blob/v5.5.2/components/hal/esp32s3/include/hal/i2s_ll.h).
- [ESP32-S3-WROOM-1](https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf): GPIO/straps/OPI; não é esquema da YD.

## Ligações digitais propostas

Os nomes abaixo identificam **funções do chip**, não headers GY comprovados.

| ESP32-S3 | Módulo | Função a identificar | Direção |
|---|---|---|---|
| GPIO14 | PCM1808 | SCK/SCKI (chip 6) | ESP → ADC, MCLK |
| GPIO15 | PCM1808 | BCK (chip 8) | ESP → ADC |
| GPIO15 | PCM5102A | BCK (chip 13) | ESP → DAC |
| GPIO16 | PCM1808 | LRC/LRCK (chip 7) | ESP → ADC |
| GPIO16 | PCM5102A | LCK/LRCK (chip 15) | ESP → DAC |
| GPIO17 | PCM5102A | DIN (chip 14) | ESP → DAC |
| GPIO18 | PCM1808 | DOUT (chip 9) | ADC → ESP |
| GND | Ambos | AGND/DGND conforme circuito | terra comum |

Conferir orientação do chip, part number, headers e continuidade desligado.
ADC e DAC são slaves: só o ESP dirige BCLK/LRCK; ADC master causaria contenção.
Não ligar MCLK ao DAC nem DOUT do ADC diretamente ao DIN do DAC.

## Alimentação, straps e sinais analógicos

| Item | Requisito do chip / verificação do módulo |
|---|---|
| PCM1808 VCC (3) | analógico 5 V nominal, 4,5–5,5 V; conferir trilhas/regulador |
| PCM1808 VDD (4) | digital 3,3 V, 3,0–3,6 V; nunca 5 V |
| PCM1808 MD0 (10), MD1 (11) | ambos LOW/GND → slave; medir resistores/jumpers reais |
| PCM1808 FMT (12) | LOW → I²S 24 bits |
| PCM1808 VINL (13), VINR (14) | linha, full-scale típico 3 Vpp no chip; conferir acoplamento/atenuação |
| PCM1808 VREF (1) | referência interna, não entrada nem alimentação externa |
| PCM5102A AVDD/CPVDD | 3,3 V no chip; VIN/5V no header só se regulador identificado |
| PCM5102A DVDD | 1,8 ou 3,3 V conforme circuito; verificar domínio digital |
| PCM5102A FMT (16) | LOW → I²S |
| PCM5102A SCK (12) | LOW/GND para PLL de BCK; conferir pulldown/jumper, não flutuar |
| PCM5102A XSMT (17) | HIGH no domínio digital válido para desmutar, nunca 5 V |
| PCM5102A FLT/DEMP | registrar straps existentes, não deixar controles flutuantes |
| PCM5102A OUTL/OUTR (6/7) | linha até 2,1 Vrms nominal; entrada de linha, não falante/headphone direto |
| GND | terras compartilhados; evitar corrente USB/LED no retorno analógico |

Placas GY podem conter reguladores, unir rails ou alterar semântica dos jumpers.
Fotografar ambos os lados, identificar reguladores e rastrear header→chip com
multímetro **desligado**. Medir rails isoladamente com fonte limitada antes de
conectar ESP. Não unir duas saídas de regulador (3V3) sem esquema que permita.
GPIOs ESP não toleram 5 V; conferir também pullups nas redes digitais.

## Clock e formato efetivos

Ambiente exclusivo `esp32-s3-hydra-live-hwtest`: pioarduino 55.03.36,
Arduino-ESP32 3.3.6 / headers IDF 5.5.2. A plataforma local chamada `espressif32`
também resolve para essa versão; não inferir versão só pelo nome.
Legacy `driver/i2s.h` suporta `mck_io_num`; não misturar novo driver no periférico.

I²S0 MASTER TX|RX, Philips, estéreo, slots 32 bits, WS LOW=L/HIGH=R.
`mck_io_num=14`, `mclk_multiple=256`, `bits_per_chan=32`, `use_apll=false`,
`fixed_mclk=0` (não seleciona clock no S3). Fonte default é PLL_160M;
S3 não possui APLL I²S como o ESP32 clássico. Driver compartilha BCK/WS
e coloca RX interno em slave do TX. DMA configurado em 4×512 frames;
driver pode limitar frames por descritor. Erro de init/pin/clock/zero-DMA
impede a criação da audio task no hwtest.

HAL procura fração com denominador de 9 bits:

`160000000 / (14 + 76/441) = 11289600 Hz` nominal exato (441 < 512).

BCLK=MCLK/4=2822400 Hz; LRCK=BCLK/64=44100 Hz; MCLK/LRCK=256.
Não há conversão silenciosa para 48 kHz. Exatidão absoluta segue cristal/PLL;
há jitter fracionário. `i2s_get_clk` e compilação não medem os pinos.

PCM1808 emite 24 bits signed MSB-first um BCLK após WS. Payload RX em bits 31:8;
os oito bits finais são descartados. Sign extension explícita por subtração
de 2^24, float=PCM24/8388608, sem ganho compensatório. Word 0=L, 1=R na
configuração S3; estímulo físico por canal confirma. RX parcial conserva só
frames completos e zera o restante. TX normal mantém saturação e escala
`masterVolume×0,90` existentes. Testes determinísticos validam conversão em
memória, não o alinhamento elétrico no fio.

## GPIO e montagem removível

GPIO14 está livre na Live: OLED1/2, encoder4/5/6, botão7, I²S15–18,
RGB48, UART43/44, USB19/20. Evitados straps0/3/45/46 e flash/OPI26–37.
GPIO1 já é SDA. Restrição MCLK0/1/3 do ESP32 clássico não vale para S3.
Confirmar **IO14** exposto e livre na placa YD real. Alterações locais ST7789
não commitadas pertencem a outra tarefa, fora deste firmware; futura integração
exige nova auditoria de GPIO.

Veroboard: sockets removíveis, headers polarizados, cortes de trilha conferidos
com lupa e continuidade. Clocks curtos, GND próximo, stubs BCLK/WS mínimos;
separar clocks de LIN/RIN e cabo de guitarra. Desacoplamento conforme datasheet
e componentes já existentes, sem presumir valores de referência/charge pump.
Evitar loops de terra e corrente pulsada no retorno analógico, preservando
terra comum digital. Medir ringing antes de dimensionar terminação.

## Checklist elétrico

1. Identificar YD/IO14, ICs GY, headers, reguladores, polaridades e straps.
2. Desligado: continuidade de cada sinal e ausência de curtos rails/GND,
   cortes da stripboard e 5 V em rede digital. Pulls podem falsear buzzer:
   conferir resistência e circuito.
3. Confirmar MD0=MD1=FMT=0 ADC; FMT=0, SCK=0 e XSMT válido DAC.
4. Módulos isolados em fonte limitada: medir rails e níveis digitais reais.
5. Desligar para interligar, GND primeiro; evitar USB/5V de duas fontes em
   paralelo/backfeed. Energizar domínios digitais antes de dirigir sinais a
   módulos; evitar hot-plug e GPIO alimentando módulo desligado.
6. Amplificador mutado/baixo volume, fonte de linha controlada, depois clocks.
   Mutar amplificador antes de desligar alimentação/clocks.

## Teste físico

Build sem flash: `python -m platformio run -e esp32-s3-hydra-live-hwtest`.
Usar artifact com bootloader/partições correspondentes, sem adivinhar offsets
ou apagar presets. Upload é posterior/manual. Rollback: artifact da PR #71 e
ambientes Live/Vintage Reference inalterados; rollback antigo não gera novo
MCLK, portanto desconectar ADC quando necessário.

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

Esperar término de bench antes de result. Consultar help existente.
Rotas hw forçam ADC na mesma tarefa/driver. Tom 440 Hz polinomial, pico0,1
(-20 dBFS), erro de aproximação <0,00016 antes do ganho. Monitor ganho fixo0,25
(-12,04 dB), sem Tape/tanh/gate/Freeverb/masterVolume. `hw normal` restaura src
anterior e DSP; cauda DSP fica pausada durante diagnóstico. Bypass preservado.
Baixar volume analógico antes de trocar rota.

`adc`: janela128 blocos (~0,372s), frames/samples recebidos, read-errors,
short-reads, pico/RMS/DC e contagem abs>=0,99 por canal. Não é detector contínuo
de overflow DMA; sucesso de read não prova ausência de perdas. Timeout mantém
mailbox pendente: repetir adc para recolher. Só CLI imprime/calcula sqrt;
callback faz somas/multiplicações limitadas durante captura, sem alocação/libm
por amostra. Estados adicionais <128 bytes além dos buffers existentes;
custo de CPU não medido. Captura pode afetar benchmark: medir separadamente.

Osciloscópio, ponta×10/terra curto: medir em IO14/SCKI, IO15/BCK e IO16/LRCK
no ESP e nos chips. Registrar screenshot, escala, amplitude, duty/ringing,
frequência e identity. Esperado 11,2896MHz / 2,8224MHz / 44,1kHz; razões4 e64,
256 MCLK por frame, 64 BCLK por frame. Capturar simultaneamente coerência,
WS e atraso de um BCLK até MSB. Analisador lógico preferível >=100MS/s para
MCLK, decoder I²S32 estéreo MSB-first; registrar precisão do instrumento.
Captura lenta pode aliasar MCLK; não confiar só em autodetecção.

Injetar linha baixa só LIN, depois RIN: confirmar ordem, sinal, RMS/DC,
incluindo amplitudes pequenas; subir gradualmente sem exceder limites do módulo.
Comparar words decodificados com PCM24; terminar entrada silenciosa conforme
circuito para medir ruído. Ouvir DAC/monitor/Live e A/B com rollback na mesma
fonte/configuração. Registrar P50/P95/P99/peak, fullWorkUs, overruns/I²S,
heap/PSRAM/stack e atualizações; deadline2902,49us. Hot path exige esse teste
físico e escuta documentada antes de promoção.

## Guitarra e pendências

PCM1808 recebe linha. Guitarra passiva requer buffer Hi-Z (ordem de1Mohm,
dimensionado ao instrumento), ganho/headroom, proteção, acoplamento e bias
compatíveis. Conferir impedância e capacitores existentes; não aterrar bias
interno por engano. Sem buffer, começar com gerador/interface de linha e
amplitude baixa medida no chip. Não conectar potência, phantom ou excesso
de tensão. Firmware não implementa pré-amplificador.

Pendentes: fotos/mapa header→chip, rails/straps e IO14 na YD (antes de soldar);
clocks/níveis/coerência/ringing; alinhamento real/LR/clipping/ruído;
escuta DAC/monitor/Live e deadline físico. Nenhuma dessas verificações foi
inferida de build ou simulada como medição.
