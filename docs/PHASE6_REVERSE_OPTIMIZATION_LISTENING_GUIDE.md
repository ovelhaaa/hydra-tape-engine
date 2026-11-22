# Phase 6.3 — escuta A/B da otimização do Reverse

Este pacote complementar compara o **Reverse V2 da Fase 6.2** (lado A / Original) com a
**implementação otimizada da Fase 6.3** (lado B / Processed), no Listening Lab existente.
Não há nova interface: abra o player em `player/`, sirva a pasta por localhost e carregue
`phase6-reverse-optimization`.

## O que mudou no lado B

- A detecção de fronteiras de subdivisão do Reverse passou de duas avaliações em precisão
  dupla por amostra para uma coleta por bloco, reconciliada exatamente com o predicado
  amostral. A sequência de janelas é idêntica.
- A ativação do delay deixou de limpar ~3 MB de PSRAM dentro do callback. A primeira
  ativação continua idêntica (buffers zerados na alocação); reativações usam uma máscara
  lógica de época, sem apagar o histórico gravado.
- Nenhuma alteração no modelo de bias, no miolo magnético, no feedback ou no algoritmo
  musical do Reverse.

## Comparação

Cada par usa a mesma fonte, seed, BPM, eventos, sample rate, duração e ganho. Não há
normalização nem compensação de volume. Os dez cenários são: Reverse 1/4, 1/8, 1/16,
feedback alto, mudança de BPM, MIDI Clock, Multi-Head, delay convencional, Warp e stress
combinado. Os dez pares foram medidos como **bit-exact** (max_abs = 0, RMSE = 0); ainda
assim, cada item começa em `NOT REVIEWED`.

## Como escutar

Volume moderado e constante; mantenha loop e posição ao alternar A/B. Comece por Reverse
1/4 e 1/16 para julgar janelas e transientes, depois feedback alto e mudança de BPM.
Compare Multi-Head e delay convencional como controles sem Reverse. Em Warp e stress, o
roteamento real faz o Warp mascarar o Reverse; isso é intencional.

Avalie apito residual, aspereza, naturalidade dos agudos, compressão, transientes,
sensação de fita, comportamento do feedback, musicalidade do Reverse e coerência estéreo.
Registre divergências de volume como observação, sem compensar cada versão.

Todos os itens começam em `NOT REVIEWED`. Use `PASS`, `NEEDS ATTENTION` ou `FAIL` somente
após a escuta e exporte o JSON por **Export reviews**. Métricas ou equivalência bit-exact
não substituem o registro humano. O pacote da Fase 6.3 no commit `7431a6d` foi revisado
pelo responsável com **10/10 PASS** e hashes verificados; novos renders e commits
posteriores continuam começando sem avaliação. O pacote de escuta da Fase 6.2 permanece
preservado em `artifacts/listening-lab/phase6-bias-ab/`.

Reprodução: `python scripts/package_reverse_ab.py`.
