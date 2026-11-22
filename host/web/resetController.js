// A failed/late reset must never be allowed to run after audio resumes.
export function createResetController({ getContext, getNode, timeoutMs = 2000 }) {
  let pending;
  let nextRequestId = 0;
  return function reset() {
    if (pending) return pending;
    pending = (async () => {
      const context = getContext();
      const node = getNode();
      if (!context || !node) throw new Error('Audio engine is not connected');
      const wasRunning = context.state === 'running';
      if (wasRunning) await context.suspend();
      const requestId = ++nextRequestId;
      await new Promise((resolve, reject) => {
        const finish = (error) => {
          clearTimeout(timer);
          node.port.removeEventListener('message', onMessage);
          error ? reject(error) : resolve();
        };
        const onMessage = ({ data }) => {
          if (data?.key !== 'reset' || data.requestId !== requestId) return;
          if (data.type === 'stateAck') finish();
          if (data.type === 'resetError') finish(new Error(data.message));
        };
        const timer = setTimeout(() => finish(new Error('DSP reset acknowledgement timed out; audio remains suspended')), timeoutMs);
        node.port.addEventListener('message', onMessage);
        node.port.start();
        try { node.port.postMessage({ type: 'reset', requestId }); }
        catch (error) { finish(error); }
      });
      if (wasRunning) await context.resume();
      return true;
    })().finally(() => { pending = undefined; });
    return pending;
  };
}
