export const WEB_BUILD_HELP = 'Execute o build web (emcmake/cmake), sirva build-web/web via HTTP e tente novamente.';

export function inferRuntimeHint(error, { protocol } = {}) {
  const message = String(error?.message || error || '').toLowerCase();
  const currentProtocol = protocol ?? globalThis?.location?.protocol ?? '';

  if (currentProtocol === 'file:') {
    return 'Página aberta via file://. Use servidor HTTP local (ex.: python3 -m http.server --directory build-web/web).';
  }

  if (
    message.includes('hydra_dsp') ||
    message.includes('worklet') ||
    message.includes('fetch') ||
    message.includes('import')
  ) {
    return `Falha ao carregar runtime web (hydra_dsp.js/wasm ou worklet). ${WEB_BUILD_HELP}`;
  }

  return null;
}

export function formatRuntimeError(contextLabel, error, options) {
  const detail = error?.message || String(error);
  const hint = inferRuntimeHint(error, options);
  const message = hint
    ? `${contextLabel}: ${detail}. Dica: ${hint}`
    : `${contextLabel}: ${detail}`;
  return { detail, hint, message };
}