export const db = amplitude => 20 * Math.log10(amplitude);
export const dbText = value => Number.isFinite(value) ? `${value.toFixed(2)} dB` : "−∞ dB";

export function levels(buffer, channel = "Stereo", duration = buffer.duration) {
  const count = Math.min(buffer.length, Math.round(duration * buffer.sampleRate));
  const channels = channel === "Stereo" ? Array.from({ length: buffer.numberOfChannels }, (_, i) => i)
    : [Number(channel === "R" && buffer.numberOfChannels > 1)];
  let energy = 0;
  let peak = 0;
  for (const index of channels) {
    const data = buffer.getChannelData(index);
    for (let i = 0; i < count; i++) {
      const sample = data[i];
      energy += sample * sample;
      peak = Math.max(peak, Math.abs(sample));
    }
  }
  return { rms: Math.sqrt(energy / (channels.length * count)), peak };
}

export function match(stats, mode) {
  const quietest = Math.min(...stats.map(row => row.rms));
  const gains = stats.map(row => mode === "MATCHED" && quietest > 0 ? quietest / row.rms : 1);
  const maxPeak = Math.max(...stats.map((row, i) => row.peak * gains[i]));
  return { gains, protection: maxPeak > 0.98 ? 0.98 / maxPeak : 1,
    silent: quietest === 0, delta: db(stats[1].rms) - db(stats[0].rms) };
}

export async function wavFormat(file) {
  const header = new DataView(await file.slice(0, 12).arrayBuffer());
  if (header.byteLength < 12 || header.getUint32(0) !== 0x52494646 || header.getUint32(8) !== 0x57415645) return null;
  for (let offset = 12; offset + 8 <= file.size;) {
    const chunk = new DataView(await file.slice(offset, offset + 16).arrayBuffer());
    const size = chunk.getUint32(4, true);
    if (chunk.getUint32(0) === 0x666d7420 && size >= 16 && chunk.byteLength >= 16) {
      return { channels: chunk.getUint16(10, true), rate: chunk.getUint32(12, true) };
    }
    offset += 8 + size + (size % 2);
  }
  return null;
}
export async function decodePair(context, files) {
  const decode = async file => context.decodeAudioData(await file.arrayBuffer());
  const buffers = await Promise.all(files[0] === files[1] ? [decode(files[0])] : files.map(decode));
  if (buffers.length === 1) buffers.push(buffers[0]);
  if (buffers.some(buffer => !buffer.length || buffer.numberOfChannels > 2)) {
    throw new Error("O deck suporta WAV mono ou estéreo.");
  }
  return buffers;
}

function ramp(param, value, time, seconds) {
  param.cancelAndHoldAtTime(time);
  param.linearRampToValueAtTime(value, time + seconds);
}

export class Deck {
  constructor(context, buffers, destination = context.destination) {
    this.context = context;
    this.buffers = buffers;
    this.duration = Math.min(...buffers.map(buffer => buffer.duration));
    this.offset = 0;
    this.active = 0;
    this.loop = false;
    this.region = [0, this.duration];
    this.fade = 0.008;
    this.monitor = 0.75;
    this.master = context.createGain();
    this.master.gain.value = this.monitor;
    this.master.connect(destination);
    this.mix = context.createGain();
    this.mix.channelCount = 2;
    this.mix.channelCountMode = "explicit";
    this.split = context.createChannelSplitter(2);
    this.merge = context.createChannelMerger(2);
    this.mix.connect(this.split);
    this.merge.connect(this.master);
    this.route("Stereo");
    this.switches = buffers.map((_, i) => {
      const node = context.createGain();
      node.gain.setValueAtTime(Number(i === 0), context.currentTime);
      node.connect(this.mix);
      return node;
    });
    this.trim = [1, 1];
    this.crossfade = { from: 0, to: 0, start: 0, end: 0 };
  }

  route(channel) {
    this.split.disconnect();
    this.split.connect(this.merge, Number(channel === "R"), 0);
    this.split.connect(this.merge, Number(channel !== "L"), 1);
  }

  configure(trim, protection, volume) {
    this.trim = trim;
    const time = this.context.currentTime;
    // Lower the ceiling before increasing any individual trim.
    const target = protection * volume;
    this.master.gain.cancelScheduledValues(time);
    this.master.gain.setValueAtTime(target, time);
    this.run?.nodes.forEach(({ gain }, i) => gain.gain.setValueAtTime(trim[i], time));
  }

  get position() {
    if (!this.run) return this.offset;
    const time = this.run.offset + Math.max(0, this.context.currentTime - this.run.when);
    const [start, end] = this.region;
    return this.loop && time >= end ? start + (time - start) % (end - start) : Math.min(time, this.duration);
  }

  start() {
    if (this.run) return;
    const [start, end] = this.region;
    if (this.loop && (this.offset < start || this.offset >= end)) this.offset = start;
    if (this.offset >= this.duration) this.offset = 0;
    const when = this.context.currentTime + 0.012;
    const offset = this.offset;
    const nodes = this.buffers.map((buffer, index) => {
      const source = this.context.createBufferSource();
      source.buffer = buffer;
      source.loop = this.loop;
      source.loopStart = start;
      source.loopEnd = end;
      const gain = this.context.createGain();
      gain.gain.value = this.trim[index];
      const edge = this.context.createGain();
      edge.gain.setValueAtTime(0, when);
      const remaining = this.duration - offset;
      const fade = this.loop ? 0.004 : Math.min(0.004, remaining / 2);
      edge.gain.linearRampToValueAtTime(1, when + fade);
      if (!this.loop) {
        edge.gain.setValueAtTime(1, when + remaining - fade);
        edge.gain.linearRampToValueAtTime(0, when + remaining);
      }
      source.connect(gain).connect(edge).connect(this.switches[index]);
      source.start(when, offset);
      if (!this.loop) source.stop(when + remaining);
      return { source, gain, edge };
    });
    const run = { when, offset, nodes };
    this.run = run;
    nodes.forEach(({ source, gain, edge }, index) => {
      source.onended = () => {
        source.disconnect();
        gain.disconnect();
        edge.disconnect();
        if (index === 0 && this.run === run) {
          this.run = null;
          this.offset = this.duration;
        }
      };
    });
  }

  select(index) {
    const time = this.context.currentTime;
    const previous = this.crossfade;
    const fraction = time >= previous.end ? 1 : Math.max(0, (time - previous.start) / (previous.end - previous.start));
    const from = previous.from + (previous.to - previous.from) * fraction;
    const end = time + this.fade;
    this.switches.forEach(({ gain }, i) => {
      gain.cancelScheduledValues(time);
      gain.setValueAtTime(i ? from : 1 - from, time);
      gain.linearRampToValueAtTime(Number(i === index), end);
    });
    this.crossfade = { from, to: index, start: time, end };
    this.active = index;
  }

  pause() {
    if (!this.run) return;
    this.offset = this.position;
    const time = this.context.currentTime;
    const end = time + 0.004;
    const nodes = this.run.nodes;
    this.run = null;
    nodes.forEach(({ source, edge }) => {
      ramp(edge.gain, 0, time, 0.004);
      source.stop(end);
    });
  }

  seek(time) {
    const playing = Boolean(this.run);
    this.pause();
    this.offset = Math.max(0, Math.min(time, this.duration));
    if (playing && (this.loop || this.offset < this.duration)) this.start();
  }

  stop() {
    this.pause();
    this.offset = 0;
  }

  setLoop(enabled, start, end) {
    const playing = Boolean(this.run);
    this.pause();
    this.loop = enabled;
    this.region = [start, end];
    if (playing) this.start();
  }

  close() {
    this.stop();
    this.master.disconnect();
  }
}
