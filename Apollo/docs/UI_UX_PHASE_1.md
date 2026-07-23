# Apollo — Especificação de UI/UX: Fase 1

## 1. Objetivo e limites da Fase 1

Esta fase define a linguagem, a hierarquia e os estados de uma futura interface do Apollo (plate reverb com ramificação de octave e ações de performance). Ela organiza os controles pelo caminho sonoro e pelas tarefas do músico, sem alterar processamento, estado persistido, automação ou apresentação gráfica final.

O repositório atualmente disponibilizado não contém `Apollo/Source/PluginProcessor.cpp`, `Apollo/Source/PluginEditor.cpp`, `Apollo/Source/PluginEditor.h`, `Apollo/docs/UI_UX_PHASE_0.md` nem `Apollo/PORTING_NOTES.md`. Portanto, esta especificação usa exclusivamente o contrato de IDs e os comportamentos DSP fornecidos no pedido; cada ponto que exigiria confirmação no código é marcado como pendente. Antes de implementar, a equipe deve reconciliar esta especificação com esses arquivos-fonte.

Ficam fora de escopo: redesign de `ApolloLookAndFeel`, mudanças em `PluginEditor.cpp`, novos componentes JUCE, medidores, presets, layout responsivo, resize, DSP, parâmetros, automação e assets gráficos.

## 2. Arquitetura de informação proposta

1. **REVERB** vem primeiro porque concentra a criação do espaço: Size, Pre-delay, Decay, Tone, Mod Rate, Mod Depth e Input Diffusion. A sequência vai da escala/tempo percebido à cauda, cor e movimento.
2. **OCTAVE** vem em seguida porque é uma ramificação adicional e opcional do sinal. O usuário primeiro escolhe **Octave Mode**, depois ajusta seus dois ganhos tonais e, por último, consulta o roteamento dry/octave ainda ambíguo.
3. **PERFORMANCE** separa a escolha persistente da ação (**Perform Action**) do gesto temporário (**Perform**). Isso evita confundir o seletor `footswitch_mode` com o booleano automatizável `momentary_effect`.
4. **OUTPUT** encerra o fluxo com a proporção final (**Mix**) e o bypass interno. Uma área de monitoramento pode ocupar espaço futuro neste grupo, mas não é definida nem implementada agora.

Essa ordem acompanha a tarefa musical: construir o reverb, acrescentar/retirar octave, escolher um gesto expressivo e balancear/contornar o efeito. A ordem visual não muda o roteamento DSP nem a ordem técnica dos parâmetros.

## 3. Wireframe textual

```text
APOLLO
├── Header
│   ├── Nome/identidade do plugin
│   ├── Estado global: Bypassed | Freeze Active | Drive Active |
│   │   Octave Perform Active | Octave Perform: selecione um modo
│   └── Ajuda contextual futura
├── REVERB
│   ├── Size                         [time_scale]
│   ├── Pre-delay                    [predelay]
│   ├── Decay                        [decay]
│   ├── Tone                         [damp]
│   ├── Mod Rate                     [modspeed]
│   ├── Mod Depth                    [moddepth]
│   └── Input Diffusion              [input_diffusion]
├── OCTAVE
│   ├── Octave Mode                  [effect_mode: None/Up/Down/Both]
│   ├── Upper Tone                   [eq1_gain]
│   ├── Lower Tone                   [eq2_gain]
│   └── Dry/Octave routing — pending [octave_dry_mix]
├── PERFORMANCE
│   ├── Perform Action               [footswitch_mode: Freeze/Overdrive/Effect]
│   └── Perform                      [momentary_effect]
└── OUTPUT
    ├── Mix                          [mix]
    └── Bypass                       [bypass]
```

Os controles OCTAVE continuam desenhados e legíveis quando o modo é None, porém atenuados; não devem desaparecer nem ser destruídos. Os IDs são referências técnicas, não texto que precisa aparecer ao músico.

## 4. Dicionário de controles e microcopy

Os nomes atuais no host abaixo são os candidatos literais derivados dos IDs do contrato e precisam ser confrontados com o APVTS antes da Fase 2; a coluna não autoriza uma mudança no host. Percentuais são apresentação provisória para controles normalizados.

| ID interno | Nome atual no host (a confirmar) | Rótulo visível proposto | Grupo visual | Tipo sugerido | Formato de valor/unidade | Tooltip | Comportamento padrão | Dependências | Compatibilidade |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `predelay` | Pre-delay | Pre-delay | REVERB | knob | valor normalizado/percentual até validar curva; **não** ms ainda | “Define o atraso antes do início do reverb.” | valor default do APVTS | nenhum | preservar ID, range/default e conversão atual |
| `mix` | Mix | Mix | OUTPUT | knob | percentual wet/dry se a curva confirmá-lo; senão percentual normalizado | “Equilibra o sinal processado e o sinal direto na saída.” | valor default do APVTS | bypass interno não muda seu valor | preservar contrato |
| `decay` | Decay | Decay | REVERB | knob | escala atual/percentual provisório | “Define a duração da cauda do reverb.” | valor default do APVTS; Freeze pode sobrepô-lo temporariamente | Freeze ativo | nunca gravar a sobreposição como novo valor |
| `moddepth` | Mod Depth | Mod Depth | REVERB | knob | percentual provisório | “Define a intensidade da modulação da cauda.” | valor default do APVTS | usa Mod Rate em conjunto | preservar contrato |
| `modspeed` | Mod Speed | Mod Rate | REVERB | knob | valor normalizado/percentual até confirmar Hz | “Define a velocidade da modulação da cauda.” | valor default do APVTS | usa Mod Depth em conjunto | rótulo visível apenas; não afirmar Hz |
| `damp` | Damp | Tone | REVERB | knob | escala atual/percentual provisório | “Ajusta o amortecimento tonal do reverb.” | valor default do APVTS | nenhum | `Tone` é linguagem proposta, não ID |
| `eq1_gain` | EQ 1 Gain | Upper Tone (pendente) | OCTAVE | knob | dB | “Ajusta o ganho tonal associado à voz acima; confirmar a associação DSP.” | valor default do APVTS | mais relevante em Up | manter dB, ID/range/default |
| `eq2_gain` | EQ 2 Gain | Lower Tone (pendente) | OCTAVE | knob | dB | “Ajusta o ganho tonal associado à voz abaixo; confirmar a associação DSP.” | valor default do APVTS | mais relevante em Down | manter dB, ID/range/default |
| `time_scale` | Time Scale | Size | REVERB | knob | escala atual/percentual provisório | “Ajusta a escala temporal percebida do reverb.” | valor default do APVTS | nenhum | divergência justificada: Size é tarefa musical, não unidade física |
| `effect_mode` | Effect Mode | Octave Mode | OCTAVE | seletor segmentado | None / Up / Down / Both | “Escolhe quais vozes de octave ficam ativas.” | valor default do APVTS | determina atenuação do OCTAVE | não recriar enum/ID |
| `footswitch_mode` | Footswitch Mode | Perform Action | PERFORMANCE | seletor | Freeze / Overdrive / Effect | “Escolhe a ação acionada por Perform.” | valor default do APVTS | define o significado de Perform | seleção não aciona a ação |
| `input_diffusion` | Input Diffusion | Input Diffusion | REVERB | knob | percentual provisório | “Ajusta a difusão aplicada à entrada do reverb.” | valor default do APVTS | nenhum | preservar contrato |
| `octave_dry_mix` | Octave Dry Mix | Dry/Octave routing (pendente) | OCTAVE | toggle, somente após validação | On / Off; sem alegação de mistura contínua | “Controla uma condição de roteamento dry na ramificação octave; consulte a ajuda.” | valor default do APVTS | Down adiciona dry mesmo ligado | não prometer semântica que o DSP não cumpre |
| `bypass` | Bypass | Bypass | OUTPUT | toggle | On / Off | “Contorna o processamento interno do Apollo. Não é o bypass do host/DAW.” | valor default do APVTS | prevalece no feedback global | manter o bypass interno separado do host |
| `momentary_effect` | Momentary Effect | Perform | PERFORMANCE | botão momentâneo/toggle automatizável | Pressed / Released; host pode automatizar On / Off | “Mantém ativa a Perform Action escolhida enquanto estiver acionado.” | valor default do APVTS | depende de Perform Action e Octave Mode em Effect | booleano e automação obrigatoriamente preservados |

## 5. Matriz de estados dependentes

| Condição técnica | Estado visual e feedback | Ênfase / atenuação | Automação e acessibilidade |
| --- | --- | --- | --- |
| `effect_mode = None` | OCTAVE atenuado; “Octave desativado — escolha Up, Down ou Both para ativar.” | enfatizar Octave Mode; atenuar os dois controles tonais e roteamento, sem ocultá-los | valores continuam visíveis, foco por teclado e recuperáveis por preset/automação; anunciar o estado ao leitor de tela |
| `effect_mode = Up` | indicador “Up active”. | enfatizar Upper Tone; Lower Tone pode permanecer disponível sem ser sugerido como inativo até confirmar DSP | não modificar valores/automação; anunciar seleção |
| `effect_mode = Down` | indicador “Down active” e nota “A ramificação inclui sinal seco neste modo.” | enfatizar Lower Tone; mostrar aviso de exceção no roteamento | manter todos os valores automatizáveis; aviso deve estar disponível por tooltip/leitor de tela |
| `effect_mode = Both` | indicador “Up + Down active”. | mostrar bloco OCTAVE completo, com ambos os tons de igual peso | nenhuma alteração de automação ou foco |
| `footswitch_mode = Freeze` e `momentary_effect = true` | header e botão pressionado: “Freeze Active”. | enfatizar Decay com indicador “temporariamente sobreposto”; não alterar sua posição | valor Decay armazenado permanece recuperável; expor botão e estado por teclado/leitor de tela |
| `footswitch_mode = Overdrive` e `momentary_effect = true` | “Drive Active”. | enfatizar Perform; ajuda: “Drive atua no reverb, não necessariamente na entrada.” | booleano continua receber automação On/Off |
| `footswitch_mode = Effect` e `momentary_effect = true` | se houver modo: “Octave Perform Active”; se None: “Octave Perform: selecione um modo.” | enfatizar Perform e Octave Mode; em None, OCTAVE segue atenuado | não impedir o acionamento automatizado; comunicar ausência de modo sem mudar parâmetro |
| `bypass = true` | “Bypassed” persistente e controle selecionado. | atenuar grupos de processamento como feedback, mantendo-os inspecionáveis | não confundir com bypass do host; preservar foco, valores, automação e presets |

Estados ativos não podem depender só de cor: botão pressionado, texto, contraste e nome acessível devem comunicar a mesma informação. Quando várias condições coexistirem, **Bypassed** é o estado global prioritário; estados de Performance podem continuar legíveis como condição configurada, sem sugerir que áudio esteja sendo processado.

## 6. Decisões pendentes e validações necessárias

1. **`octave_dry_mix`:** opções seguras de rótulo são “Dry/Octave routing” ou “Dry path in octave branch”; nenhum é final. Validar auditivamente e no DSP a polaridade do toggle e a consequência por modo. O fato já conhecido é: desligado acrescenta dry à ramificação octave; em Down, dry também é acrescentado quando ligado. Até haver descrição completa, não usar “Dry Mix”, “Dry On” nem texto que implique exclusão do dry em Down.
2. **`predelay`:** verificar a curva e a unidade de `Dattorro::setPreDelay`. Só após isso apresentar ms; até lá usar percentual/valor normalizado.
3. **`modspeed`:** confirmar que a unidade configurada no DSP é Hz. Até confirmação, o rótulo é Mod Rate, mas o valor não recebe “Hz”.
4. **`eq1_gain` / `eq2_gain`:** confirmar se cada ganho corresponde, de fato, a Up e Down e se “Tone” descreve melhor a ação do que “Gain”. dB é permitido pelas faixas conhecidas; os rótulos Upper/Lower Tone permanecem pendentes.
5. **QA:** definir hosts e SO prioritários (ao menos um VST3 e um AU, versões e arquiteturas), incluindo restauração de sessão, gravação/leitura de automação e navegação por teclado.
6. **Perform:** definir implementação para mouse press/release, Space/Enter enquanto há foco, perda de foco, drag para fora, touch e automação host. A UI deve escrever/representar o mesmo booleano `momentary_effect`; mouse/teclado ativo significa true e release significa false, sem impedir eventos do host. Em conflito, o valor observado do parâmetro é a fonte de verdade e o estado acessível deve atualizar.

## 7. Critérios de aceite para a Fase 2

- Os 15 IDs do contrato estão mapeados para um controle ou estado visível, sem renomear, remover ou recriar IDs.
- Implementação não altera IDs, ranges, defaults, persistência, DSP, roteamento ou automação.
- Todo rótulo visível está aprovado, ou explicitamente marcado como pendente e apresentado de forma segura; em especial `octave_dry_mix`, unidades de Pre-delay/Mod Rate e linguagem dos EQs.
- REVERB, OCTAVE, PERFORMANCE e OUTPUT seguem a ordem e os grupos desta especificação.
- Freeze, Overdrive e Effect são compreensíveis pela seleção de Perform Action, pelo botão Perform e pelo feedback ativo.
- Bypass interno é claramente distinguível do bypass do host/DAW.
- Todos os estados da matriz têm texto, indicador não cromático, estado acessível e regras de ênfase/atenuação implementáveis.
- A ferramenta de automação do host preserva e restaura todos os parâmetros, inclusive `momentary_effect`; presets não fazem valores OCTAVE parecerem perdidos em None.
- Navegação por teclado, foco visível, acionamento de Perform e nomes/valores acessíveis são especificados e verificados nos hosts prioritários.

## 8. Riscos de compatibilidade de automação

Alterar um ID rompe a associação que hosts e sessões usam para automação e recuperação. Alterar range ou default pode reinterpretar dados já salvos, mudar som de sessões existentes e tornar presets incompatíveis. Por isso, rótulos visíveis e tooltips devem ser uma camada de apresentação sobre o APVTS existente, nunca substitutos do contrato técnico.

Ocultar controles OCTAVE em None cria o risco de o músico concluir que automação ou valores de preset foram apagados. Eles devem permanecer recuperáveis, identificáveis e acessíveis, apenas atenuados. Do mesmo modo, substituir `momentary_effect` por uma interação local de botão sem manter o parâmetro booleano quebraria gravação/reprodução de automação de host e estados restaurados. A futura interface pode oferecer pressão momentânea por mouse ou teclado, mas deve espelhar o booleano existente e aceitar mudanças provenientes do host.
