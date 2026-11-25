# Instruções para agentes — Hydra Tape Engine

## Leitura obrigatória antes de qualquer modificação no DSP

1. **Auditoria técnica e musical:** [docs/TAPE_DELAY_DSP_MUSICAL_AUDIT_2026-11-23.md](docs/TAPE_DELAY_DSP_MUSICAL_AUDIT_2026-11-23.md).
2. **Roteiro de implementação por fases:** [docs/DSP_REFORM_IMPLEMENTATION_PHASES.md](docs/DSP_REFORM_IMPLEMENTATION_PHASES.md).
3. Antes de **cada** fase, releia os contratos globais G0 e o preflight específico F0–F11 do roteiro; confirme a HEAD atual e quais fases anteriores foram realmente aprovadas e incorporadas.
4. Leia a [reconciliação F0.1](docs/DSP_REFORM_PR_RECONCILIATION.md) e a [consolidação da F0](docs/evidence/dsp-reform-f0/CONSOLIDATION.md). Diferencie main (API 1.7/38) das APIs experimentais #61 (1.8/47) e #62 (1.9/49); não recrie C, Repeat Aging ou Motion sem conferir os SHAs e gates registrados. O READY_FOR_F1 histórico não supera o bloqueio de regenerabilidade documentado na F0.1.

**Não confunda o plano com autorização para implementar todas as fases.** Trabalhe apenas na fase solicitada, preferencialmente uma PR por fase, sem merge automático. Antes de editar, apresente o mapeamento do código ativo, os caminhos `arquivo:linha`, a hipótese musical, o custo esperado no ESP32-S3 e o plano de validação.

**Contratos inegociáveis:** nenhum código de TapeModel legado deve substituir o TapeCore canônico; preserve a memória compartilhada Tape/Reverse/Warp, ABI C, presets, compatibilidade Native/WASM e testes em `tests/core/`. Não relaxe tolerâncias nem regenere goldens sem justificativa e aprovação. Não acrescente alocação, bloqueio ou processamento pesado à audio task sem prova de custo. Quando mudar o hot path, exija validação no hardware físico, além de escuta A/B documentada.

A variante de feedback C é experimental/opt-in e teve validação física própria; não presuma que esteja incorporada ao modo padrão. Nenhuma melhoria sonora deve ser promovida por superioridade teórica sem comparação auditiva com a baseline aprovada.

Em tarefas explicitamente **somente de auditoria ou documentação**, não edite código e não crie mudanças fora do escopo solicitado.
