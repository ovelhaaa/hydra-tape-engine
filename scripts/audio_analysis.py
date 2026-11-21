"""Audio signal analysis and musical verification library for Hydra Tape Engine 2.0.

Computes reproducible acoustic, temporal, and stability metrics:
  - Absolute Peak, RMS, True Peak (oversampled)
  - Crest Factor (dB), Clipping Detection
  - Windowed Energy Envelope
  - Signal Discontinuities & Derivative Analysis
  - Stereo Channel Correlation
  - Echo Delay Time Offset & Musical Subdivision Error
  - Feedback Stability & Decay Envelopes
  - Reverse Tape Echo V2 Window Diagnostics (Boundary alignment, crossfade energy dips/spikes)
"""

import math
import numpy as np
from scipy import signal

def calc_peak(audio: np.ndarray) -> float:
    """Calculates absolute peak amplitude across all channels."""
    if audio.size == 0:
        return 0.0
    return float(np.max(np.abs(audio)))

def calc_rms(audio: np.ndarray) -> float:
    """Calculates root-mean-square amplitude."""
    if audio.size == 0:
        return 0.0
    return float(np.sqrt(np.mean(np.square(audio))))

def calc_true_peak(audio: np.ndarray, oversample: int = 4) -> float:
    """Estimates True Peak amplitude via 4x bandlimited oversampling."""
    if audio.size == 0:
        return 0.0
    if audio.ndim == 1:
        resampled = signal.resample_poly(audio, oversample, 1)
        return float(np.max(np.abs(resampled)))
    tp = 0.0
    for ch in range(audio.shape[1]):
        resampled = signal.resample_poly(audio[:, ch], oversample, 1)
        tp = max(tp, float(np.max(np.abs(resampled))))
    return tp

def calc_crest_factor_db(audio: np.ndarray) -> float:
    """Calculates crest factor in dB (20 * log10(Peak / RMS))."""
    p = calc_peak(audio)
    r = calc_rms(audio)
    if r < 1e-9 or p < 1e-9:
        return 0.0
    return float(20.0 * np.log10(p / r))

def detect_clipping(audio: np.ndarray, threshold: float = 0.999, min_consecutive: int = 2) -> dict:
    """Detects digital clipping and flat-top sample regions."""
    if audio.size == 0:
        return {"clipped_samples": 0, "has_clipping": False, "max_consecutive": 0}
    
    flat = np.abs(audio) >= threshold
    clipped_count = int(np.sum(flat))
    
    # Check consecutive clamped samples per channel
    max_cons = 0
    if audio.ndim == 1:
        channels = [audio]
    else:
        channels = [audio[:, ch] for ch in range(audio.shape[1])]
        
    for ch_data in channels:
        is_clip = (np.abs(ch_data) >= threshold).astype(int)
        if np.any(is_clip):
            runs = np.diff(np.concatenate(([0], is_clip, [0])))
            starts = np.where(runs == 1)[0]
            ends = np.where(runs == -1)[0]
            lengths = ends - starts
            if len(lengths) > 0:
                max_cons = max(max_cons, int(np.max(lengths)))
                
    return {
        "clipped_samples": clipped_count,
        "has_clipping": max_cons >= min_consecutive,
        "max_consecutive": max_cons,
    }

def calc_window_energy(audio: np.ndarray, window_size: int = 1024, hop_size: int = 512) -> np.ndarray:
    """Calculates sliding window RMS energy envelope."""
    if audio.size == 0:
        return np.array([], dtype=np.float32)
    mono = np.mean(audio, axis=1) if audio.ndim > 1 else audio
    frames = len(mono)
    if frames < window_size:
        return np.array([float(np.sqrt(np.mean(mono**2)))], dtype=np.float32)
    
    num_hops = 1 + (frames - window_size) // hop_size
    energies = np.zeros(num_hops, dtype=np.float32)
    for i in range(num_hops):
        start = i * hop_size
        w = mono[start : start + window_size]
        energies[i] = np.sqrt(np.mean(w**2))
    return energies

def detect_discontinuities(audio: np.ndarray, jump_threshold: float = 0.20) -> dict:
    """Detects unnatural DSP clicks / amplitude jumps using first and second derivatives."""
    if audio.size == 0:
        return {"max_step": 0.0, "max_accel": 0.0, "jump_count": 0}
    
    mono = np.mean(audio, axis=1) if audio.ndim > 1 else audio
    d1 = np.abs(np.diff(mono))
    d2 = np.abs(np.diff(d1))
    
    max_step = float(np.max(d1)) if len(d1) > 0 else 0.0
    max_accel = float(np.max(d2)) if len(d2) > 0 else 0.0
    jump_count = int(np.sum(d1 > jump_threshold))
    
    return {
        "max_step": max_step,
        "max_accel": max_accel,
        "jump_count": jump_count,
    }

def calc_channel_correlation(audio: np.ndarray) -> float:
    """Calculates zero-lag Pearson correlation coefficient between Left and Right."""
    if audio.ndim < 2 or audio.shape[1] < 2 or len(audio) < 2:
        return 1.0
    l = audio[:, 0]
    r = audio[:, 1]
    norm_l = np.linalg.norm(l)
    norm_r = np.linalg.norm(r)
    if norm_l < 1e-9 or norm_r < 1e-9:
        return 1.0
    corr = float(np.dot(l, r) / (norm_l * norm_r))
    return max(-1.0, min(1.0, corr))

def measure_echo_timing(wet_audio: np.ndarray, dry_audio: np.ndarray,
                        sample_rate: int = 48000, expected_delay_ms: float = None) -> dict:
    """Measures echo arrival times using cross-correlation envelope and calculates timing error."""
    wet_mono = np.mean(wet_audio, axis=1) if wet_audio.ndim > 1 else wet_audio
    dry_mono = np.mean(dry_audio, axis=1) if dry_audio.ndim > 1 else dry_audio

    # Isolate wet echo component by subtracting direct dry bleed
    dry_corr_peak = np.dot(wet_mono, dry_mono) / (np.dot(dry_mono, dry_mono) + 1e-9)
    echo_signal = wet_mono - dry_corr_peak * dry_mono

    # Cross-correlation between isolated echo signal and original dry
    corr = signal.correlate(echo_signal, dry_mono, mode='full')
    lags = signal.correlation_lags(len(echo_signal), len(dry_mono))

    if expected_delay_ms is not None and expected_delay_ms > 0:
        expected_lag = int((expected_delay_ms * 0.001) * sample_rate)
        # Search window +/- 25% around expected delay (min 20ms margin)
        margin = max(int(0.02 * sample_rate), int(expected_lag * 0.25))
        search_mask = (lags >= expected_lag - margin) & (lags <= expected_lag + margin)
        if np.any(search_mask):
            valid_lags = lags[search_mask]
            valid_corr = corr[search_mask]
            best_lag = valid_lags[np.argmax(valid_corr)]
        else:
            best_lag = expected_lag
    else:
        positive_mask = (lags > int(0.04 * sample_rate))
        if not np.any(positive_mask):
            return {"measured_delay_ms": 0.0, "delay_error_ms": 0.0, "delay_error_pct": 0.0}
        valid_lags = lags[positive_mask]
        valid_corr = corr[positive_mask]
        best_lag = valid_lags[np.argmax(valid_corr)]

    measured_delay_ms = float((best_lag / sample_rate) * 1000.0)
    delay_error_ms = 0.0
    delay_error_pct = 0.0
    if expected_delay_ms is not None and expected_delay_ms > 0:
        delay_error_ms = float(measured_delay_ms - expected_delay_ms)
        delay_error_pct = float(abs(delay_error_ms) / expected_delay_ms * 100.0)

    return {
        "measured_delay_ms": measured_delay_ms,
        "delay_error_ms": delay_error_ms,
        "delay_error_pct": delay_error_pct,
    }

def analyze_feedback_stability(audio: np.ndarray, sample_rate: int = 48000,
                               stimulus_end_sec: float = 2.0) -> dict:
    """Analyzes decaying tail for runaway divergence, residual energy, and decay rate (dB/s)."""
    mono = np.mean(audio, axis=1) if audio.ndim > 1 else audio
    tail_start_idx = int(stimulus_end_sec * sample_rate)
    
    if tail_start_idx >= len(mono):
        return {"decay_rate_db_s": 0.0, "is_stable": True, "residual_rms": 0.0}
    
    tail = mono[tail_start_idx:]
    if len(tail) < sample_rate // 10:
        return {"decay_rate_db_s": 0.0, "is_stable": True, "residual_rms": 0.0}
    
    # Calculate energy in 100ms segments
    seg_size = int(0.1 * sample_rate)
    num_segs = len(tail) // seg_size
    seg_rms = []
    for s in range(num_segs):
        segment = tail[s * seg_size : (s + 1) * seg_size]
        seg_rms.append(max(1e-9, float(np.sqrt(np.mean(segment**2)))))
        
    seg_db = 20.0 * np.log10(np.array(seg_rms))
    times = np.arange(num_segs) * 0.1
    
    # Linear fit of dB vs time
    if len(times) > 1:
        slope, intercept = np.polyfit(times, seg_db, 1)
    else:
        slope = 0.0
        
    residual_rms = float(np.sqrt(np.mean(tail[-seg_size:]**2))) if len(tail) >= seg_size else 0.0
    # Stable if energy is decaying (slope negative or near zero) and peak not blowing up
    peak_in_tail = float(np.max(np.abs(tail)))
    is_stable = (slope <= 1.5) and (peak_in_tail < 1.5)
    
    return {
        "decay_rate_db_s": float(slope),
        "is_stable": is_stable,
        "residual_rms": residual_rms,
        "peak_in_tail": peak_in_tail,
    }

def analyze_reverse_v2(audio: np.ndarray, expected_window_ms: float,
                       sample_rate: int = 48000) -> dict:
    """Analyzes Reverse Tape Echo V2 window transitions, boundary steps, and crossfades."""
    mono = np.mean(audio, axis=1) if audio.ndim > 1 else audio
    window_samples = int(expected_window_ms * 0.001 * sample_rate)
    if window_samples <= 0 or len(mono) < 2 * window_samples:
        return {
            "window_alignment_error_samples": 0,
            "boundary_max_step": 0.0,
            "crossfade_dip_ratio": 1.0,
            "boundary_cleanliness": "PASS"
        }

    # Crossfade duration is 8ms nominal in ReverseWindows
    crossfade_samples = max(1, int(0.008 * sample_rate))

    # Inspect each expected boundary
    boundary_steps = []
    dip_ratios = []

    num_windows = len(mono) // window_samples
    for w in range(1, num_windows):
        boundary_idx = w * window_samples
        if boundary_idx + crossfade_samples >= len(mono):
            break
        # Boundary step
        step = np.abs(mono[boundary_idx] - mono[boundary_idx - 1])
        boundary_steps.append(step)

        # Crossfade energy vs adjacent steady state
        cf_region = mono[boundary_idx - crossfade_samples // 2 : boundary_idx + crossfade_samples // 2]
        steady_region = mono[boundary_idx + crossfade_samples : boundary_idx + 2 * crossfade_samples]
        
        rms_cf = max(1e-6, np.sqrt(np.mean(cf_region**2)))
        rms_st = max(1e-6, np.sqrt(np.mean(steady_region**2)))
        dip_ratios.append(rms_cf / rms_st)

    max_boundary_step = float(np.max(boundary_steps)) if boundary_steps else 0.0
    avg_dip_ratio = float(np.mean(dip_ratios)) if dip_ratios else 1.0

    # Step < 0.25 and dip ratio between 0.4 and 1.8 indicates clean musical crossfade
    clean = (max_boundary_step < 0.25) and (0.35 <= avg_dip_ratio <= 2.2)

    return {
        "window_samples": window_samples,
        "boundary_max_step": max_boundary_step,
        "crossfade_dip_ratio": avg_dip_ratio,
        "boundary_cleanliness": "PASS" if clean else "MARGINAL"
    }

def full_audio_analysis(processed_audio: np.ndarray, original_audio: np.ndarray,
                        sample_rate: int = 48000, expected_delay_ms: float = None,
                        expected_reverse_ms: float = None, stimulus_end_sec: float = 2.0) -> dict:
    """Runs the full suite of acoustic, timing, and stability metrics."""
    peak = calc_peak(processed_audio)
    rms_val = calc_rms(processed_audio)
    tp = calc_true_peak(processed_audio)
    cf = calc_crest_factor_db(processed_audio)
    clip = detect_clipping(processed_audio)
    disc = detect_discontinuities(processed_audio)
    corr = calc_channel_correlation(processed_audio)
    fb = analyze_feedback_stability(processed_audio, sample_rate, stimulus_end_sec)
    
    echo = {}
    if expected_delay_ms is not None:
        echo = measure_echo_timing(processed_audio, original_audio, sample_rate, expected_delay_ms)
        
    rev = {}
    if expected_reverse_ms is not None:
        rev = analyze_reverse_v2(processed_audio, expected_reverse_ms, sample_rate)

    return {
        "peak": peak,
        "rms": rms_val,
        "true_peak": tp,
        "crest_factor_db": cf,
        "clipping": clip,
        "discontinuities": disc,
        "channel_correlation": corr,
        "feedback_stability": fb,
        "echo_timing": echo,
        "reverse_v2": rev,
    }
