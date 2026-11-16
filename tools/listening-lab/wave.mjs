export function envelope(buffers, mode, channel, start, end, width) {
  const rate = buffers[0].sampleRate;
  const channels = channel === "Stereo" ? Math.max(...buffers.map(buffer => buffer.numberOfChannels)) : 1;
  const lanes = buffers.map(buffer => Array.from({ length: channels }, (_, i) => {
    const index = channel === "R" ? 1 : i;
    return buffer.getChannelData(Math.min(index, buffer.numberOfChannels - 1));
  }));
  return Array.from({ length: width }, (_, x) => {
    const left = Math.floor((start + (end - start) * x / width) * rate);
    const right = Math.max(left + 1, Math.floor((start + (end - start) * (x + 1) / width) * rate));
    let min = 0;
    let max = 0;
    for (let c = 0; c < channels; c++) {
      for (let i = left; i < right; i++) {
        const a = lanes[0][c][i] ?? 0;
        const b = lanes[1][c][i] ?? 0;
        const value = mode === "Difference" ? a - b : mode === "A" ? a : b;
        min = Math.min(min, value);
        max = Math.max(max, value);
      }
    }
    return [min, max];
  });
}

export const clock = time => {
  const milliseconds = Math.round(time * 1000);
  return `${String(Math.floor(milliseconds / 60000)).padStart(2, "0")}:${(milliseconds % 60000 / 1000).toFixed(3).padStart(6, "0")}`;
};

export function draw(canvas, buffers, view, channel, range, region, markers) {
  const width = Math.max(1, Math.round(canvas.clientWidth * (window.devicePixelRatio || 1)));
  const height = Math.max(1, Math.round(canvas.clientHeight * (window.devicePixelRatio || 1)));
  canvas.width = width;
  canvas.height = height;
  const ctx = canvas.getContext("2d");
  const scale = window.devicePixelRatio || 1;
  const [start, end] = range;
  const xAt = seconds => (seconds - start) / (end - start) * width;
  const graphHeight = height - 20 * scale;
  const modes = view === "Both" ? [["A"], ["B"]] : view === "Overlay" ? [["A", "B"]] : [[view]];
  const colors = { A: "#ff9862", B: "#8ddcd2", Difference: "#f1c57a" };
  ctx.fillStyle = "#181c1d";
  ctx.fillRect(0, 0, width, height);
  if (region) {
    ctx.fillStyle = "#ff98622c";
    ctx.fillRect(xAt(region[0]), 0, xAt(region[1]) - xAt(region[0]), graphHeight);
  }
  for (let tick = 0; tick <= 4; tick++) {
    const x = (width - 1) * tick / 4;
    ctx.fillStyle = "#ffffff27";
    ctx.fillRect(x, 0, scale, graphHeight);
    ctx.fillStyle = "#bebeba";
    ctx.font = `${10 * scale}px monospace`;
    ctx.textAlign = tick === 4 ? "right" : tick === 0 ? "left" : "center";
    ctx.fillText(clock(start + (end - start) * tick / 4), x, height - 5 * scale);
  }
  for (const marker of markers) {
    ctx.fillStyle = "#f1c57a99";
    const x = xAt(marker.time);
    ctx.fillRect(x, 0, scale, graphHeight);
    ctx.fillRect(x - 2 * scale, 0, 5 * scale, 5 * scale);
  }
  modes.forEach((lanes, lane) => {
    const half = graphHeight / modes.length / 2;
    const middle = half * (2 * lane + 1);
    ctx.fillStyle = "#ffffff22";
    ctx.fillRect(0, middle, width, scale);
    for (const mode of lanes) {
      ctx.strokeStyle = colors[mode];
      ctx.globalAlpha = lanes.length > 1 ? 0.65 : 1;
      ctx.beginPath();
      envelope(buffers, mode, channel, start, end, width).forEach(([min, max], i) => {
        const amplitude = mode === "Difference" ? 2 : 1;
        const y = value => middle - Math.max(-1, Math.min(value / amplitude, 1)) * (half - 3 * scale);
        ctx.moveTo(i + 0.5, y(max));
        ctx.lineTo(i + 0.5, y(min));
      });
      ctx.stroke();
      ctx.globalAlpha = 1;
    }
  });
}
