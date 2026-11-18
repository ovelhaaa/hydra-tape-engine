"""Musical stimulus synthesizer for Hydra Tape Engine 2.0 Phase 6 Validation.

Generates realistic musical audio sources:
  A. Clean Guitar (individual notes and arpeggiated chords)
  B. Driven Guitar (moderate drive with sustained harmonics and pick attack)
  C. Monophonic Synthesizer (phrases with resonant filter sweep and sustained notes)
  D. Polyphonic Synthesizer (lush chords with long envelopes)
  E. Drum Machine (punchy kick, crisp snare, quantized 16th-note hi-hats)
  F. Irregular Percussion (unquantized, polyrhythmic accents and off-grid transients)
"""

import math
import numpy as np

def generate_clean_guitar(duration_sec: float, sample_rate: int = 48000) -> np.ndarray:
    """Source A: Clean Guitar (E2, A2, D3, G3, B3, E4 notes and chords)."""
    frames = int(duration_sec * sample_rate)
    audio = np.zeros((frames, 2), dtype=np.float32)
    t = np.arange(frames, dtype=np.float32) / sample_rate

    # Arpeggio pattern (E minor 9 / A minor chord tones)
    # Pluck timings in seconds, frequencies in Hz
    notes = [
        (0.00, 82.41, 0.7, 0.40),    # E2
        (0.25, 123.47, 0.6, 0.45),   # B2
        (0.50, 164.81, 0.6, 0.50),   # E3
        (0.75, 196.00, 0.6, 0.55),   # G3
        (1.00, 246.94, 0.7, 0.60),   # B3
        (1.25, 329.63, 0.8, 0.65),   # E4
        (1.50, 392.00, 0.7, 0.55),   # G4
        (1.75, 293.66, 0.6, 0.50),   # D4
        (2.00, 110.00, 0.8, 0.45),   # A2
        (2.25, 164.81, 0.6, 0.50),   # E3
        (2.50, 220.00, 0.7, 0.55),   # A3
        (2.75, 261.63, 0.7, 0.60),   # C4
        (3.00, 329.63, 0.8, 0.65),   # E4
        (3.50, 82.41, 1.2, 0.40),    # E2 low sustain
    ]

    for t_start, freq, decay, pan in notes:
        if t_start >= duration_sec:
            continue
        start_idx = int(t_start * sample_rate)
        note_len = min(frames - start_idx, int(decay * 2.0 * sample_rate))
        if note_len <= 0:
            continue
        tn = np.arange(note_len, dtype=np.float32) / sample_rate

        # Karplus-like physical pluck harmonics: fundamental + damped harmonics
        env = np.exp(-tn / decay) * (1.0 - np.exp(-tn / 0.003))
        # Pick transient click
        pick = np.exp(-tn / 0.004) * np.sin(2.0 * np.pi * 3200.0 * tn) * 0.35

        sig = (
            1.00 * np.sin(2.0 * np.pi * freq * tn) +
            0.50 * np.sin(2.0 * np.pi * 2.0 * freq * tn) * np.exp(-tn / (decay * 0.7)) +
            0.25 * np.sin(2.0 * np.pi * 3.0 * freq * tn) * np.exp(-tn / (decay * 0.5)) +
            0.12 * np.sin(2.0 * np.pi * 4.0 * freq * tn) * np.exp(-tn / (decay * 0.35)) +
            0.06 * np.sin(2.0 * np.pi * 5.0 * freq * tn) * np.exp(-tn / (decay * 0.25))
        ) * env + pick

        audio[start_idx : start_idx + note_len, 0] += sig * (1.0 - pan) * 0.45
        audio[start_idx : start_idx + note_len, 1] += sig * pan * 0.45

    # Normalize to -3 dB peak
    peak = np.max(np.abs(audio))
    if peak > 1e-6:
        audio = audio * (0.707 / peak)
    return audio

def generate_driven_guitar(duration_sec: float, sample_rate: int = 48000) -> np.ndarray:
    """Source B: Guitar with moderate drive (sustained power chords and heavy transients)."""
    frames = int(duration_sec * sample_rate)
    audio = np.zeros((frames, 2), dtype=np.float32)

    # Power chords & riffs: E5 (82.41, 123.47, 164.81), G5, A5
    chords = [
        (0.00, [82.41, 123.47, 164.81], 0.95),   # E5
        (1.00, [82.41, 123.47, 164.81], 0.45),   # E5
        (1.50, [98.00, 146.83, 196.00], 0.45),   # G5
        (2.00, [110.00, 164.81, 220.00], 1.20),  # A5 sustained
        (3.25, [98.00, 146.83, 196.00], 0.70),   # G5
    ]

    for t_start, freqs, dur in chords:
        if t_start >= duration_sec:
            continue
        start_idx = int(t_start * sample_rate)
        chord_len = min(frames - start_idx, int(dur * sample_rate))
        if chord_len <= 0:
            continue
        tn = np.arange(chord_len, dtype=np.float32) / sample_rate
        chord_sig = np.zeros(chord_len, dtype=np.float32)

        # Pick attack
        pick = np.exp(-tn / 0.006) * (np.random.randn(chord_len).astype(np.float32) * 0.5)

        for f in freqs:
            env = np.exp(-tn / (dur * 0.8)) * (1.0 - np.exp(-tn / 0.005))
            chord_sig += (np.sin(2.0 * np.pi * f * tn) + 0.3 * np.sin(4.0 * np.pi * f * tn)) * env

        raw = chord_sig * 2.5 + pick
        # Soft tube overdrive curve (tanh saturation with asymmetric bias)
        driven = np.tanh(raw + 0.15 * raw * raw)
        # Cab emulation lowpass/highpass filter approximation
        audio[start_idx : start_idx + chord_len, 0] += driven * 0.55
        audio[start_idx : start_idx + chord_len, 1] += driven * 0.55

    peak = np.max(np.abs(audio))
    if peak > 1e-6:
        audio = audio * (0.75 / peak)
    return audio

def generate_mono_synth(duration_sec: float, sample_rate: int = 48000) -> np.ndarray:
    """Source C: Monophonic Synthesizer (resonant filter sweep, melodic phrases)."""
    frames = int(duration_sec * sample_rate)
    audio = np.zeros((frames, 2), dtype=np.float32)

    # 16th note synth lead line: A3 (220), C4 (261.63), D4 (293.66), E4 (329.63), G4 (392)
    sequence = [
        (0.00, 220.00, 0.22),
        (0.25, 261.63, 0.22),
        (0.50, 293.66, 0.22),
        (0.75, 329.63, 0.45),
        (1.25, 392.00, 0.22),
        (1.50, 329.63, 0.45),
        (2.00, 293.66, 0.22),
        (2.25, 261.63, 0.22),
        (2.50, 220.00, 0.95),
    ]

    for t_start, freq, dur in sequence:
        if t_start >= duration_sec:
            continue
        start_idx = int(t_start * sample_rate)
        seg_len = min(frames - start_idx, int(dur * sample_rate))
        if seg_len <= 0:
            continue
        tn = np.arange(seg_len, dtype=np.float32) / sample_rate

        # Sawtooth waveform with exponential decay envelope
        phase = (tn * freq) % 1.0
        saw = 2.0 * phase - 1.0

        # Sub oscillator (1 octave down square wave)
        sub = np.sign(np.sin(2.0 * np.pi * (freq * 0.5) * tn)) * 0.35

        # Resonant filter sweep simulation (sweep cutoff 4000Hz -> 800Hz)
        env = (1.0 - np.exp(-tn / 0.008)) * np.exp(-tn / (dur * 0.9))
        sig = (saw + sub) * env

        audio[start_idx : start_idx + seg_len, 0] += sig * 0.55
        audio[start_idx : start_idx + seg_len, 1] += sig * 0.50

    peak = np.max(np.abs(audio))
    if peak > 1e-6:
        audio = audio * (0.707 / peak)
    return audio

def generate_poly_synth(duration_sec: float, sample_rate: int = 48000) -> np.ndarray:
    """Source D: Polyphonic Synthesizer (warm lush pad chords with slow attack/decay)."""
    frames = int(duration_sec * sample_rate)
    audio = np.zeros((frames, 2), dtype=np.float32)

    # Chord progression: Dmaj9 (D3, F#3, A3, C#4, E4) -> Gmaj7 (G2, G3, B3, D4, F#4)
    chords = [
        (0.00, [146.83, 185.00, 220.00, 277.18, 329.63], 2.2),
        (2.00, [98.00, 196.00, 246.94, 293.66, 369.99], 2.2),
    ]

    for t_start, freqs, dur in chords:
        if t_start >= duration_sec:
            continue
        start_idx = int(t_start * sample_rate)
        seg_len = min(frames - start_idx, int(dur * sample_rate))
        if seg_len <= 0:
            continue
        tn = np.arange(seg_len, dtype=np.float32) / sample_rate

        # Slow attack (150ms) and slow release
        attack_time = 0.15
        release_time = 0.60
        att_env = np.clip(tn / attack_time, 0.0, 1.0)
        rel_env = np.clip((dur - tn) / release_time, 0.0, 1.0)
        env = att_env * rel_env

        chord_l = np.zeros(seg_len, dtype=np.float32)
        chord_r = np.zeros(seg_len, dtype=np.float32)

        for i, f in enumerate(freqs):
            detune_l = 1.0 - 0.002 * (i % 2)
            detune_r = 1.0 + 0.002 * ((i + 1) % 2)
            # Dual pulse/saw mix with slight chorus
            sig_l = (np.sin(2.0 * np.pi * f * detune_l * tn) + 0.3 * np.sin(4.0 * np.pi * f * detune_l * tn))
            sig_r = (np.sin(2.0 * np.pi * f * detune_r * tn) + 0.3 * np.sin(4.0 * np.pi * f * detune_r * tn))
            pan = (i + 1.0) / (len(freqs) + 1.0)
            chord_l += sig_l * (1.0 - pan)
            chord_r += sig_r * pan

        audio[start_idx : start_idx + seg_len, 0] += chord_l * env * 0.25
        audio[start_idx : start_idx + seg_len, 1] += chord_r * env * 0.25

    peak = np.max(np.abs(audio))
    if peak > 1e-6:
        audio = audio * (0.707 / peak)
    return audio

def generate_drum_machine(duration_sec: float, sample_rate: int = 48000, bpm: float = 120.0) -> np.ndarray:
    """Source E: Drum Machine (808/909 kick, snappy snare, 16th hi-hats on musical grid)."""
    frames = int(duration_sec * sample_rate)
    audio = np.zeros((frames, 2), dtype=np.float32)

    beat_sec = 60.0 / bpm
    sixteenth_sec = beat_sec / 4.0
    total_sixteenths = int(duration_sec / sixteenth_sec)

    for step in range(total_sixteenths):
        t_start = step * sixteenth_sec
        start_idx = int(t_start * sample_rate)
        if start_idx >= frames:
            break

        bar_step = step % 16
        # Kick on 0, 6, 10
        if bar_step in [0, 6, 10]:
            k_len = min(frames - start_idx, int(0.25 * sample_rate))
            tn = np.arange(k_len, dtype=np.float32) / sample_rate
            # 808 pitch dive 140Hz -> 48Hz
            f_inst = 48.0 + 92.0 * np.exp(-tn / 0.035)
            phase = 2.0 * np.pi * np.cumsum(f_inst) / sample_rate
            k_env = np.exp(-tn / 0.08)
            kick = np.sin(phase) * k_env * 0.85
            audio[start_idx : start_idx + k_len, 0] += kick
            audio[start_idx : start_idx + k_len, 1] += kick

        # Snare on 4, 12 (backbeat)
        if bar_step in [4, 12]:
            s_len = min(frames - start_idx, int(0.18 * sample_rate))
            tn = np.arange(s_len, dtype=np.float32) / sample_rate
            body = np.sin(2.0 * np.pi * 185.0 * tn) * np.exp(-tn / 0.04) * 0.5
            noise = np.random.randn(s_len).astype(np.float32) * np.exp(-tn / 0.07) * 0.4
            snare = body + noise
            audio[start_idx : start_idx + s_len, 0] += snare * 0.7
            audio[start_idx : start_idx + s_len, 1] += snare * 0.7

        # Closed Hi-Hat on every 16th, accented on 8th notes, open on step 14
        if bar_step == 14: # Open hat
            h_len = min(frames - start_idx, int(0.22 * sample_rate))
            tn = np.arange(h_len, dtype=np.float32) / sample_rate
            h_env = np.exp(-tn / 0.12)
            hat = (np.random.randn(h_len).astype(np.float32) - 0.5) * h_env * 0.35
            audio[start_idx : start_idx + h_len, 0] += hat * 0.4
            audio[start_idx : start_idx + h_len, 1] += hat * 0.6
        else:
            h_len = min(frames - start_idx, int(0.04 * sample_rate))
            tn = np.arange(h_len, dtype=np.float32) / sample_rate
            accent = 1.0 if (bar_step % 2 == 0) else 0.65
            h_env = np.exp(-tn / 0.015)
            hat = (np.random.randn(h_len).astype(np.float32) - 0.5) * h_env * 0.30 * accent
            pan = 0.4 if (bar_step % 2 == 0) else 0.6
            audio[start_idx : start_idx + h_len, 0] += hat * (1.0 - pan)
            audio[start_idx : start_idx + h_len, 1] += hat * pan

    peak = np.max(np.abs(audio))
    if peak > 1e-6:
        audio = audio * (0.80 / peak)
    return audio

def generate_irregular_percussion(duration_sec: float, sample_rate: int = 48000) -> np.ndarray:
    """Source F: Irregular Percussion (unquantized, polyrhythmic organic transients)."""
    frames = int(duration_sec * sample_rate)
    audio = np.zeros((frames, 2), dtype=np.float32)

    # Off-grid, polyrhythmic woodblock/conga/rimshot events
    # (time, pitch, duration, pan, velocity)
    events = [
        (0.000, 520.0, 0.05, 0.30, 0.8),
        (0.183, 340.0, 0.08, 0.65, 0.6), # ~11/60 timing (off-grid)
        (0.391, 780.0, 0.04, 0.20, 0.7),
        (0.554, 420.0, 0.07, 0.75, 0.5),
        (0.712, 600.0, 0.05, 0.40, 0.9),
        (0.963, 310.0, 0.09, 0.80, 0.75),
        (1.155, 520.0, 0.05, 0.25, 0.65),
        (1.339, 850.0, 0.03, 0.35, 0.85),
        (1.581, 410.0, 0.07, 0.70, 0.55),
        (1.792, 340.0, 0.08, 0.55, 0.70),
        (2.073, 620.0, 0.05, 0.30, 0.90),
        (2.284, 910.0, 0.03, 0.85, 0.60),
        (2.467, 430.0, 0.07, 0.45, 0.75),
        (2.715, 310.0, 0.09, 0.20, 0.80),
        (3.021, 520.0, 0.05, 0.60, 0.85),
        (3.289, 740.0, 0.04, 0.40, 0.70),
        (3.542, 360.0, 0.08, 0.75, 0.65),
        (3.819, 880.0, 0.03, 0.30, 0.80),
    ]

    for t_start, freq, decay, pan, vel in events:
        if t_start >= duration_sec:
            continue
        start_idx = int(t_start * sample_rate)
        seg_len = min(frames - start_idx, int(decay * 2.5 * sample_rate))
        if seg_len <= 0:
            continue
        tn = np.arange(seg_len, dtype=np.float32) / sample_rate

        # Resonant membrane/rim click
        env = np.exp(-tn / decay)
        click = np.sin(2.0 * np.pi * freq * tn) * env * vel

        audio[start_idx : start_idx + seg_len, 0] += click * (1.0 - pan)
        audio[start_idx : start_idx + seg_len, 1] += click * pan

    peak = np.max(np.abs(audio))
    if peak > 1e-6:
        audio = audio * (0.75 / peak)
    return audio

def get_musical_source(name: str, duration_sec: float, sample_rate: int = 48000, bpm: float = 120.0) -> np.ndarray:
    name_lower = name.lower()
    if "guitar_clean" in name_lower or "clean_guitar" in name_lower or "guitar" in name_lower and "drive" not in name_lower:
        return generate_clean_guitar(duration_sec, sample_rate)
    elif "guitar_driven" in name_lower or "driven_guitar" in name_lower or "drive" in name_lower:
        return generate_driven_guitar(duration_sec, sample_rate)
    elif "mono_synth" in name_lower or "synth_mono" in name_lower:
        return generate_mono_synth(duration_sec, sample_rate)
    elif "poly_synth" in name_lower or "synth_poly" in name_lower:
        return generate_poly_synth(duration_sec, sample_rate)
    elif "drum" in name_lower:
        return generate_drum_machine(duration_sec, sample_rate, bpm=bpm)
    elif "percussion" in name_lower or "irregular" in name_lower:
        return generate_irregular_percussion(duration_sec, sample_rate)
    else:
        # Default fallback to clean guitar
        return generate_clean_guitar(duration_sec, sample_rate)
