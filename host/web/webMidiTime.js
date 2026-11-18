/**
 * Web MIDI Timestamp to Audio Frame Conversion Utilities.
 * Preserves high-resolution hardware timestamps from MIDIMessageEvent across the Web AudioWorklet boundary.
 */

/**
 * Checks whether a Web MIDI event timestamp is valid and within acceptable latency bounds.
 *
 * @param {number|null|undefined} midiTimestamp - Event timestamp from MIDIMessageEvent.timeStamp (ms)
 * @param {number} hostNow - Current monotonic host time from performance.now() (ms)
 * @returns {boolean} True if timestamp is valid
 */
export function isMidiTimestampValid(midiTimestamp, hostNow) {
  if (typeof midiTimestamp !== 'number' || !Number.isFinite(midiTimestamp) || midiTimestamp <= 0) {
    return false;
  }
  if (typeof hostNow !== 'number' || !Number.isFinite(hostNow) || hostNow <= 0) {
    return false;
  }
  const dtMs = hostNow - midiTimestamp;
  if (dtMs < -50.0 || dtMs > 3000.0) {
    return false;
  }
  return true;
}

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
  if (!isMidiTimestampValid(midiTimestamp, hostNow)) {
    return fallbackFrame;
  }

  const dtMs = hostNow - midiTimestamp;
  const safeDtMs = Math.max(0, dtMs);
  const dtSec = safeDtMs / 1000.0;
  const eventAudioTime = Math.max(0, validAudioTime - dtSec);
  return Math.max(0, Math.round(eventAudioTime * validRate));
}

/**
 * Process a batch of MIDI events, preserving original inter-pulse timing and ensuring monotonic frame ordering.
 * Explicitly rejects duplicate and truly out-of-order/regressive events.
 *
 * @param {Array<{byte: number, timeStamp: number}>} events - Batch of MIDI messages
 * @param {number} hostNow - Current performance.now() (ms)
 * @param {number} audioCurrentTime - AudioContext.currentTime (seconds)
 * @param {number} sampleRate - AudioContext.sampleRate (Hz)
 * @param {number} [lastFrame=0] - Last processed frame index
 * @returns {Array<{byte: number, timestampFrames: number, status: string, isFallback: boolean}>} Processed messages
 */
export function processBatchMidiEvents(events, hostNow, audioCurrentTime, sampleRate, lastFrame = 0) {
  if (!Array.isArray(events) || events.length === 0) return [];
  const results = [];
  let prevFrame = lastFrame;
  let prevTimestamp = -1;

  for (const ev of events) {
    const isValid = isMidiTimestampValid(ev.timeStamp, hostNow);
    let frame = convertMidiTimestampToAudioFrames(ev.timeStamp, hostNow, audioCurrentTime, sampleRate);
    let status = isValid ? 'accepted' : 'fallback';

    if (prevTimestamp >= 0 && typeof ev.timeStamp === 'number' && Math.abs(ev.timeStamp - prevTimestamp) < 1.0) {
      status = 'rejected_duplicate';
    } else if (prevFrame > 0) {
      if (frame < prevFrame) {
        status = 'rejected_out_of_order';
      } else if (frame === prevFrame) {
        // Sub-sample rounding: preserve sequence with minimal separation
        frame = prevFrame + 1;
      }
    }

    if (status === 'accepted' || status === 'fallback') {
      prevFrame = frame;
      if (typeof ev.timeStamp === 'number') {
        prevTimestamp = ev.timeStamp;
      }
    }

    results.push({
      byte: ev.byte,
      timestampFrames: frame,
      status,
      isFallback: !isValid
    });
  }
  return results;
}
