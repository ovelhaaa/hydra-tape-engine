# Relatório Técnico: Biblioteca "Frippertronics_Core" e Plano de Implementação

**Data:** 03/01/2026
**Status:** Arquivado para Futura Implementação ("Na Gaveta")

## 1. Análise da Biblioteca `Frippertronics_Core`

A biblioteca consiste em dois módulos principais de DSP (Digital Signal Processing) otimizados para o ESP32, focados em eficiência e coloração sonora ambiental.

### 1.1 Conteúdo e Detalhamento Funcional

#### 1.1.1. Módulo `Diffuser` (Difusor Espectral/Temporal)

Este componente é o responsável por criar a sonoridade "Ambient" (estilo Brian Eno), atuando como um "borrador" de transientes que suaviza ataques percussivos e cria texturas densas ("nuvens" sonoras). Diferente de um reverb tradicional, ele foca na densidade de curto prazo.

- **Arquitetura:**
  Implementa uma cadeia em série de **3 Filtros Passa-Tudo (Schroeder All-Pass Filters)**.
- **Fluxo de Sinal (Algoritmo):**
  Para cada amostra de áudio, o sinal passa sequencialmente pelos 3 estágios do difusor. Em cada estágio:

  1.  **Leitura do Atraso:** O sistema lê o valor armazenado no buffer circular na posição atual ($y = buffer[idx]$).
  2.  **Cálculo da Saída:** Combina-se a entrada atual ($x$) com o valor lido do atraso:
      $$Output = -gain \cdot x + y$$
  3.  **Realimentação (Feedback):** Atualiza-se o buffer na posição atual com uma mistura da entrada e da saída recém-calculada:
      $$buffer[idx] = x + gain \cdot Output$$
  4.  **Propagação:** O $Output$ deste estágio torna-se a entrada ($x$) do próximo estágio da cadeia.

- **Fundamentação Matemática (Design Math):**
  - **Números Primos:** Os tamanhos dos buffers de atraso são estritamente primos (**113, 181, 233** samples). Isso é fundamental para evitar que as frequências de ressonância dos filtros se alinhem ou sejam harmônicas entre si. O resultado é uma densidade de eco inarmônica, suave e sem tonalidade metálica ("ringing") característica de delays digitais simples.
  - **Phase Smearing (Borramento de Fase):** Por ser uma cadeia de filtros "All-Pass", a magnitude da resposta de frequência é plana (não atenua graves nem agudos teoricamente), mas a **fase** do sinal é drasticamente alterada e espalhada no tempo. Isso converte impulsos (cliques) em curtos "swooshes", reduzindo a definição rítmica em favor da textura.

#### 1.1.2. Módulo `FastSine` (Oscilador de Baixa Frequência Otimizado)

Uma implementação de função seno de alta performance para modulações em tempo real (LFOs), onde a velocidade de execução é prioritária sobre a pureza espectral absoluta.

- **Tabela de Consulta (LUT):** Utiliza um array pré-calculado de 1024 floats (`table[1024]`), ocupando apenas 4KB de RAM.
- **Otimização Bitwise:**
  - Substitui operações de módulo (`%`) e comparações condicionais (`if x > 2PI`) por uma simples máscara AND (`& 1023`).
  - Utiliza um fator de conversão pré-calculado ($1024 / 2\pi \approx 162.97$) para transformar radianos diretamente em índices da tabela via cast para inteiro.
- **Aplicação:** Ideal para gerar os sinais de controle de **Wow** e **Flutter** do tape delay, permitindo que a CPU se dedique ao processamento pesado de interpolação de áudio sem gargalos matemáticos nas funções trigonométricas.

#### 1.1.3. O Loop de Fita (Sound-on-Sound)

Embora a biblioteca `Frippertronics_Core` foque na coloração, o "motor" do Frippertronics reside na arquitetura do próprio `TapeDelay` (classe `TapeModel`), que já implementa a física fundamental do sistema:

- **Sound-on-Sound (SOS):** O sistema utiliza uma arquitetura de _feedback_ realimentado onde a saída do cabeçote de reprodução é somada à nova entrada antes de ser regravada (arquitetura de delay digital clássico com saturação no loop).
- **Capacidade de Loop Longo:**
  - O ESP32-S3 (versão N16R8) possui **8MB de PSRAM**.
  - O `TapeModel` já aloca seus buffers na PSRAM (`heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`).
  - **Potencial:** Com 48kHz e processamento estéreo (float 32-bit), cada segundo de áudio consome ~384KB. Isso permite loops de **até ~20 segundos** (teórico), embora o padrão atual esteja limitado a 2s.
  - **Implementação:** Para obter o loop Frippertronics clássico (3 a 6 segundos), basta aumentar o parâmetro `maxDelayTimeMs` na inicialização do `TapeModel`. A estrutura de memória e ponteiros de leitura/escrita (`writeHead`) já suporta offset arbitrário, permitindo varispeed em loops longos sem glitches.

### 1.2 Compatibilidade e Riscos

- **PlatformIO/ESP32:** Totalmente compatível. O uso da flag `IRAM_ATTR` nos métodos `process` garante que o código execute a partir da RAM interna rápida, evitando latência de acesso à memória Flash.
- **Consumo de Memória:** Extremamente baixo (~2KB totais). Seguro para rodar simultaneamente com o `TapeModel`.
- **Independência:** A biblioteca foi refatorada para não ter dependências além das bibliotecas padrão (`Arduino.h`, `math.h`), eliminando riscos de conflito com versões futuras do código base.

---

## 2. Plano de Implementação ("Na Gaveta")

Este plano descreve como reintroduzir as funcionalidades "Frippertronics" (Morphing, Freeze e Difusão) no projeto principal (`main.cpp`) sem quebrar a arquitetura atual.

### Fase 1: Integração do Difusor (Tonalidade "Eno")

**Objetivo:** Adicionar o controle de "borrão" (Diffusion) ao final da cadeia de Delay.

1.  **Incluir Biblioteca:**
    Adicionar `#include "Frippertronics_Core.h"` no `main.cpp`.
2.  **Instanciar Objetos:**
    Criar `Diffuser diffuserL;` e `Diffuser diffuserR;` globais.
3.  **Inserir no Loop de Áudio (`audioTask`):**
    Logo após `tape->processStereo(...)` e antes da saída:
    ```cpp
    // Código Sugerido
    if (p_diffusionAmt > 0.0f) {
        float diffGain = 0.5f + (p_diffusionAmt * 0.4f); // Mapeia 0-1 para ganho 0.5-0.9
        diffuserL.gain = diffGain;
        diffuserR.gain = diffGain;
        // Processa In-Place ou Out-Of-Place
        outL = diffuserL.process(outL);
        outR = diffuserR.process(outR);
    }
    ```
4.  **CLI:** Adicionar comando `dif <0-100>`.

### Fase 2: Implementação do "Freeze" (Congelamento)

**Objetivo:** Permitir loops infinitos (Sound-on-Sound estático).

1.  **Lógica de Controle:**
    No `executeCommand`, ao ativar Freeze (`frz`):
    - Forçar `feedback` para `1.0` (ou `1.05` para saturação leve gradual).
    - Mudar entrada para silêncio (Input Mute) para não gravar novas camadas por cima.
    - Reduzir ligeiramente a velocidade (`tapeSpeed`) para criar um efeito de "drift" e degradação lenta.

### Fase 3: O "Macro" Morph

**Objetivo:** Criar um único controle que transita entre o estilo "Robert Fripp" e "Brian Eno".

1.  **Variável `morph` (0.0 a 1.0):**
    - **0.0 (Fripp):** Diffusion = 0, TapeAge = Baixo, Feedback = Médio, Heads = Rítmicas.
    - **1.0 (Eno):** Diffusion = High, TapeAge = Alto (Dark), Feedback = Alto, Heads = 3 (Longa).
2.  **Implementação:**
    Criar função `updateMorph(float m)` que interpola todos esses parâmetros proporcionalmente.
