# Hydra 2.0 — Validação de áudio do Accidental Warp V1.3

## Reprodução

Compile `hydra_listen_native` em Release e instale NumPy e SciPy. A bancada requer Node.js e Git. Ela detecta a extensão do executável, aceita caminhos explícitos e cria um manifesto novo a cada execução.

```sh
cmake -S . -B build-native -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-native --config Release --target hydra_listen_native
python -m pip install numpy scipy
python scripts/test_validate_warp_prototype.py
python scripts/validate_warp_prototype.py --output-dir artifacts/warp-ab
```

Use `--listen` para informar o executável, inclusive se o gerador CMake colocar o binário em `Release/`. Use `--node` para selecionar o Node. A execução verifica o SHA-256 do HTML original com quebras de linha normalizadas para LF e registra também o hash dos bytes no disco. O manifesto identifica o commit, alterações locais, versões dos motores, arquivos, parâmetros e critérios. A CI publica `accidental-warp-ab` com o manifesto e os WAVs para escuta. Os WAVs usam PCM estéreo de 24 bits e as métricas usam os dados float32 anteriores à conversão.

## Correção DSP

A bancada envia a mesma entrada float32 a 48 kHz e os mesmos eventos nos limites de 128 frames. O Worklet recebe mensagens de parâmetros. O modelo Python mantém uma instância por render e o TapeCore usa sua suavização interna de Grain. A comparação usa TapeCore sem hiss ou modulações estocásticas para medir estéreo e decaimento. O circuito magnético permanece ativo.

| Cenário | Critério e evidência |
| --- | --- |
| 1. Oitava | Alvo 880 Hz, pico dominante de 890 Hz nos três motores. |
| 2. Quinta | Alvo 660 Hz, pico de 665 Hz nos três motores. |
| 3–6. Quatro modos | Saída não silenciosa, pico monitorado sobre o render completo e zero NaN/Inf. O leitor single-tap do TapeCore usa período de fase baseado em Grain e alcance de leitura Grain + 1000 amostras. O protótipo usa Grain + 1000 para ambos. |
| 7. Estéreo | Entrada mono a 0, 0,5 e 1. Diferença L/R abaixo de `1e-5` no primeiro caso e acima de −60 dBFS nos demais. |
| 8. Feedback | Impulso a 0%, 50%, 80% e 95%. A cauda perde energia com o fim do estímulo e 0% produz a menor energia de repetição entre 200 e 500 ms. |
| 9. Grain | 40 → 120 → 180 → 240 ms. A saída coincide com o controle de Grain estático até o primeiro evento e diverge depois dele. |
| 10. Speed | 0 → 0,5 → 1 → 1,5. A frequência é verificada após a acomodação em cada segmento. |
| 11. Toggle | Ativo → inativo → ativo. Só o nativo tem a transição de volta para o modo de delay convencional. Worklet e Python ficam como `NOT COMPARABLE`. |
| 12. Cascata | Frase de cinco notas, feedback de 65% e render de 15 s. Valores finitos, pico e decaimento são medidos em toda a gravação. |

Para sinais estacionários, o erro de pitch permitido acomoda uma banda lateral à taxa de Grain mais um bin da FFT. O limite de 2 FS detecta crescimento acima da margem de 6 dB sobre PCM normalizado. Os testes do core cobrem as janelas complementares e transições. O teste Python verifica a evolução e o wrap da fase, persistência da memória dos filtros, retenção do impulso em buffer e a leitura esperada no frame 16 de uma janela de 64 frames.

O teste de impulso revelou um tom residual de aproximadamente 3 kHz que permaneceu por 15 s no TapeCore a Speed 0,5. Com 95% de feedback, a janela de 0,5 a 1,5 s tinha RMS de `6,97e-3`. Permitir que só a entrada nova abra o gate de bias no modo Warp reduziu esse RMS para `2,77e-8`. A realimentação continua passando pela saturação magnética. O cenário 8 detecta a regressão com o critério de decaimento.

## Similaridade entre motores

O par Worklet/Python usa limites de RMS de `1e-6`, desvio máximo `1e-5` e alinhamento de zero frames. Ambos implementam o mesmo caminho de referência, com buffer float32. Esses limites acomodam arredondamento de ponto flutuante. A bancada registra, para todos os pares renderizados, erros RMS e máximos por canal, lag da correlação, diferença espectral, energia a cada 100 ms e diferença L/R.

O TapeCore inclui saturação magnética, EQ, interpolação sobre outro anel, fase de leitura inicial e suavização próprias. Os pares com TapeCore têm métricas descritivas e status `NOT COMPARABLE` para equivalência de forma de onda. Na oitava, os três picos foram 890 Hz, mas o erro Worklet/TapeCore ficou em 0,106 RMS por canal nesta configuração. Nos modos single-tap, a diferença é também de temporização do leitor. Esta bancada mede a cadeia completa e não quantifica a contribuição isolada de cada estágio.

## Avaliação perceptual

A escuta A/B está pendente. Use os WAVs do mesmo cenário, variante e entrada, com níveis ajustados para comparação. Registre diferenças de ritmo, textura granular, brilho, saturação, degradação das repetições e musicalidade da cascata. A verificação de pico, decaimento e NaN/Inf cobre propriedades numéricas.

## Web e ESP32-S3

O teste funcional web executa o AudioWorklet de produção em Node com o binário WASM gerado. Ele envia ativação e modo pelo MessagePort e Speed e Feedback por AudioParams, lê os valores da API C e mede mudança de pitch, forma de onda e energia da cauda. Os testes estáticos cobrem a ligação dos controles ao host. O teste funcional usa uma simulação de host de Worklet e não mede o agendamento de um navegador.

Para testar o firmware, execute `pio run -e esp32-s3-devkitc-1`. Em serial a 115200, `perf` mede geração, processamento e formatação PCM do bloco por `esp_timer_get_time()`, fora das chamadas de I2S. Com 128 frames a 44,1 kHz, o orçamento é `128 / 44100 ≈ 2.902,5 µs`. `DSP Budget Overruns` conta blocos acima desse limite. `I2S TX Write Failures` conta escritas que retornam erro ou quantidade incompleta de bytes. O contador de TX não mede continuidade física do sinal no conversor.

Em uma placa ESP32-S3 rev. 0.2 com 16 MB de flash e 8 MB de PSRAM, o teste usou o sintetizador interno e 44,1 kHz. No primeiro firmware de bancada, os blocos de delay sem Warp e com Warp a 50% e 95% ultrapassaram o orçamento em todas as janelas de medição, com zero escritas curtas ou com erro. O pico acumulado chegou a 5.428 µs. O comando serial passou a rodar no Core 0 para continuar respondendo enquanto o áudio ocupa o Core 1. A build que forçou `-Ofast` elevou o pico de Warp para 7.547 µs, por isso a configuração padrão de otimização do framework foi restaurada. A [PR de validação](https://github.com/ovelhaaa/hydra-tape-engine/pull/47) registra o ensaio com o firmware final.

## Native ↔ WASM

O teste de equivalência usa `max_abs <= 1e-5` e `RMSE <= 1e-6`. A execução local em Release mediu `max_abs = 7,436e-6` e `RMSE = 2,49805e-6` tanto na branch quanto no baseline `main` (`e4cae43`), com o mesmo toolchain. Os resultados do GitHub Actions devem ser verificados no HEAD da PR separadamente.
