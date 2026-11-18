/**
 * Web MIDI Timestamp to Audio Frame Conversion Utilities.
 * Preserves high-resolution hardware timestamps from MIDIMessageEvent across the Web AudioWorklet boundary.
 */

/**
 * Converts a Web MIDI event timestamp (DOMHighResTimeStamp relative to performance.timeOrigin)
 * into an absolute audio context frame index, compensating for main-thread event scheduling latency.
 *
 * @param {number|null|undefined} midiTimestamp - Event timestamp from MIDIMessageEvent.timeStamp (ms)
 * @param {number} hostNow - Current monotonic host time from performance.now() (ms)
 * @param {number} audioCurrentTime - AudioContext.currentTime (seconds)
 * @param {number} sampleRate - AudioContext.sampleRate (Hz)
 * @returns {number} Converted audio frame index (integer >= 0)
 */
export function convertMidiTimestampToAudioFrames(midiTimestamp, hostNow, audioCurrentTime, sampleRate) {
  const validRate = (typeof sampleRate === 'number' && Number.isFinite(sampleRate) && sampleRate > 0)
    ? sampleRate
    : 48000.0;
  const validAudioTime = (typeof audioCurrentTime === 'number' && Number.isFinite(audioCurrentTime) && audioCurrentTime >= 0)
    ? audioCurrentTime
    : 0.0;
  const fallbackFrame = Math.max(0, Math.round(validAudioTime * validRate));

  // Invalid or missing timestamp fallback policy
  if (typeof midiTimestamp !== 'number' || !Number.isFinite(midiTimestamp) || midiTimestamp <= 0) {
    return fallbackFrame;
  }
  if (typeof hostNow !== 'number' || !Number.isFinite(hostNow) || hostNow <= 0) {
    return fallbackFrame;
  }

  const dtMs = hostNow - midiTimestamp;

  // Reject future timestamps exceeding 50ms tolerance margin
  if (dtMs < -50.0) {
    return fallbackFrame;
  }

  // Reject stale events delayed by more than 3.0 seconds
  if (dtMs > 3000.0) {
    return fallbackFrame;
  }

  // Calculate audio frame corresponding to the instant the MIDI event was received by driver
  const safeDtMs = Math.max(0, dtMs);
  const dtSec = safeDtMs / 1000.0;
  const eventAudioTime = Math.max(0, validAudioTime - dtSec);
  return Math.max(0, Math.round(eventAudioTime * validRate));
}

/**
 * Process a batch of MIDI events, preserving original inter-pulse timing and ensuring monotonic frame ordering.
 *
 * @param {Array<{byte: number, timeStamp: number}>} events - Batch of MIDI messages
 * @param {number} hostNow - Current performance.now() (ms)
 * @param {number} audioCurrentTime - AudioContext.currentTime (seconds)
 * @param {number} sampleRate - AudioContext.sampleRate (Hz)
 * @param {number} [lastFrame=0] - Last processed frame index
 * @returns {Array<{byte: number, timestampFrames: number}>} Processed messages with converted frame timestamps
 */
export function processBatchMidiEvents(events, hostNow, audioCurrentTime, sampleRate, lastFrame = 0) {
  if (!Array.isArray(events) || events.length === 0) return [];
  const results = [];
  let prevFrame = lastFrame;

  for (const ev of events) {
    let frame = convertMidiTimestampToAudioFrames(ev.timeStamp, hostNow, audioCurrentTime, sampleRate);
    if (frame <= prevFrame && prevFrame > 0) {
      // Ensure strict monotonicity for timing clock events
      frame = prevFrame + 1;
    }
    prevFrame = frame;
    results.push({
      byte: ev.byte,
      timestampFrames: frame
    });
  }
  return results;
}
