# Handoff F3

FASE:F3 — READY_FOR_HUMAN_LISTENING; aprovação musical das candidatas PENDING.
BASE HEAD:2c15b88680ff65aff8bd5ac09543fab44b52c785.
FONTE DO PACOTE:1e454ebf5f02d7f39a4e0ea634bb107f32da4217.
PR:#68 Draft, baseada na F2; nenhum merge.

Hipótese, arquivos/linhas e leituras:SATURATION_ARCHITECTURE.md.
Diferença DSP:somente ganhos dos saturadores, flag independente default0.
Nenhuma mudança não relacionada/ABI/presets/LUT/bias/Reverse/Warp/UI.
Core/header têm os mesmos hashes do pacote após documentação final.
Native/WASM:max_abs0/RMSE0; input/LUT exatos, limites intactos.
A congelada10/10 exact; produçãoA/C0/Warm10/10 cada; Warp exact.
CTest:16/17 Native,14/15 WASM, assertion A conhecida preservada;
observação completa Native/WASM sem outra falha. Flake Tap inicial e timeout
de browser inicial preservados; repetições completas passaram, sem alterar DSP.
Motor27/27;154/154 estresses/candidata,60 caudas60s+4 compostas;
192 cenas dinâmicas/candidata com instrumentação bit-exact.
Clipping/headroom:FEEDBACK_STABILITY.md.
Sonic RAW/MATCHED:13cenas/78WAVs;sha256.json/gabarito separado/guia no pacote.
Browser local e ZIP publicado:PASS,26 avaliações exportadas/persistidas;
teste automático não é audição humana.
CPU/I2S/memória/stack físicos:PENDING_PHYSICAL; três builds ESP32 PASS,
zero flash. Não promover com evidência desktop ou histórica.
Rollback:F3=0/F2=2 para Vintage;F2=0 C0;F2=1 Warm; rotaA default intacta.
Próxima fase:F4 NÃO AUTORIZADA. Escuta das candidatas e campanha física
comparável necessárias antes de promoção/release; merge somente autorizado.
