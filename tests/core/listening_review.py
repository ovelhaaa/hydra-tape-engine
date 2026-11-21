"""Render shared stimuli for listening and characterize DSP transitions."""
import argparse
import array
from dataclasses import dataclass, field
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile
import wave

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import audio_provenance

RATE = 48_000
SECONDS = 12
BLOCK = 128
# IDs from include/hydra_dsp.h.
FLUTTER, WOW, DRIVE, DELAY, FEEDBACK, MIX, WET, HEADS, PING, FREEZE, REVERSE = (
    0, 1, 3, 12, 13, 14, 15, 16, 21, 22, 23
)
BASE = {0: 0, 1: 0, 2: 0, 3: 25, 4: 0, 5: 50, 6: 0, 7: 0, 8: 0,
        11: 1, DELAY: 400, FEEDBACK: 45, MIX: 80, WET: 100, HEADS: 4,
        20: 80, PING: 0, FREEZE: 0, REVERSE: 0, 24: 0, 25: 0}


@dataclass(frozen=True)
class Scene:
    name: str
    stimulus: str
    settings: dict = field(default_factory=dict)
    events: tuple = ()
    seconds: int = SECONDS


SCENES = [
    Scene("single_head_impulse", "impulse", {DELAY: 400, FEEDBACK: 60}),
    Scene("multi_head_transients", "transients", {DELAY: 650, HEADS: 7}),
    Scene("high_feedback_harmonic", "harmonic", {DELAY: 550, FEEDBACK: 88}),
    Scene("ping_pong", "impulse", {DELAY: 480, FEEDBACK: 78, PING: 1}),
    Scene("wow_flutter_excerpt", "excerpt", {FLUTTER: 32, WOW: 28, HEADS: 7}),
    Scene("drive_ramp_excerpt", "excerpt", {DRIVE: 5},
          tuple((1 + i * 0.5, DRIVE, 5 + i * 5) for i in range(1, 20))),
]
TRANSITIONS = [
    Scene("freeze", "tone", {MIX: 100}, ((3, FREEZE, 1), (6, FREEZE, 0)), 9),
    Scene("reverse_stereo", "reverse_markers", {MIX: 100, FEEDBACK: 0, REVERSE: 1}, (), 5),
    Scene("delay_step", "tone", {MIX: 100, FEEDBACK: 0},
          ((3, DELAY, 700), (6, DELAY, 220)), 9),
]


def little_endian(values):
    if sys.byteorder != "little":
        values.byteswap()
    return values


def load_excerpt(path):
    with wave.open(str(path), "rb") as wav:
        if (wav.getframerate(), wav.getsampwidth(), wav.getnchannels()) != (RATE, 2, 2):
            raise ValueError("excerpt must be stereo PCM16 WAV at 48 kHz")
        pcm = array.array("h", wav.readframes(8 * RATE))
    if not pcm:
        raise ValueError("empty excerpt")
    return array.array("f", (sample / 32768 for sample in little_endian(pcm)))


def synthesize(scene, excerpt):
    frames = scene.seconds * RATE
    values = array.array("f", [0.0]) * (2 * frames)
    if scene.stimulus == "excerpt" and excerpt is not None:
        values[:len(excerpt)] = excerpt
        fade = min(RATE // 20, len(excerpt) // 2)
        for n in range(fade):
            scale = (fade - n - 1) / fade
            index = len(excerpt) - 2 * fade + 2 * n
            values[index] *= scale
            values[index + 1] *= scale
        return values
    if scene.stimulus == "impulse":
        values[2 * RATE] = 0.8
        return values
    if scene.stimulus in ("reverse_markers", "reverse_left_only", "reverse_right_only"):
        for time in (1.25, 1.75, 2.4, 3.1):
            index = int(time * RATE) * 2
            if scene.stimulus != "reverse_right_only":
                values[index] = 0.75
            if scene.stimulus != "reverse_left_only":
                values[index + 1] = 0.75
        return values
    notes = (110, 130.81, 164.81, 146.83, 98, 123.47, 196, 164.81)
    active = frames if scene.stimulus == "tone" else min(frames, 8 * RATE)
    for i in range(active):
        t = i / RATE
        tau = math.tau
        if scene.stimulus == "tone":
            v = 0.34 * math.sin(tau * 440 * t) * min(t * 20, 1)
            left = right = v
        elif scene.stimulus == "harmonic":
            envelope = max(0, min(t * 20, (active / RATE - t) * 20, 1))
            left = envelope * (0.26 * math.sin(tau * 110 * t) + 0.11 * math.sin(tau * 330 * t))
            right = envelope * (0.24 * math.sin(tau * 110 * t) + 0.1 * math.sin(tau * 220 * t))
        else:
            beat = int(t * 2)
            phase = t - beat / 2
            f = notes[beat % len(notes)]
            attack = min(phase * 400, 1)
            envelope = attack * math.exp(-phase * (30 if scene.stimulus == "transients" else 7))
            left = envelope * (0.28 * math.sin(tau * f * phase) + 0.09 * math.sin(tau * 3 * f * phase))
            right = envelope * (0.24 * math.sin(tau * f * phase) + 0.12 * math.sin(tau * 2 * f * phase))
        values[2 * i] = left
        values[2 * i + 1] = right
    return values


def run_scene(command, scene, input_path, work, count):
    events = [(0, key, value) for key, value in (BASE | scene.settings).items()]
    events += [(round(t * RATE / BLOCK) * BLOCK, key, value) for t, key, value in scene.events]
    schedule = work / "automation.txt"
    schedule.write_text("".join(f"{frame} {key} {value}\n" for frame, key, value in sorted(events)),
                        encoding="ascii")
    output = work / "output.f32"
    subprocess.run([*command, input_path, schedule, output], check=True)
    values = array.array("f")
    raw = output.read_bytes()
    values.frombytes(raw)
    if len(values) != count:
        raise ValueError(f"{scene.name}: expected {count} samples, got {len(values)}")
    values = little_endian(values)
    if not all(map(math.isfinite, values)):
        raise ValueError(f"{scene.name}: non-finite sample")
    return values


def write_wav(path, values):
    # Fixed 24-bit scale for every file. No normalization.
    pcm = bytearray()
    for value in values:
        integer = max(-8388608, min(8388607, round(value * 8388607)))
        pcm.extend(integer.to_bytes(3, "little", signed=True))
    with wave.open(str(path), "wb") as wav:
        wav.setparams((2, 3, RATE, 0, "NONE", "not compressed"))
        wav.writeframes(pcm)


def peak_step(values, start, stop):
    return max(abs(values[i] - values[i - 1]) for i in range(max(1, start), stop))


def frequency(values, start, stop):
    crossings = []
    for i in range(start + 1, stop):
        a, b = values[i - 1], values[i]
        if a <= 0 < b:
            crossings.append(i - 1 - a / (b - a))
    return RATE * (len(crossings) - 1) / (crossings[-1] - crossings[0]) if len(crossings) >= 2 else None


def delta(a, b):
    return array.array("f", (x - y for x, y in zip(a, b, strict=True)))


def echo_peaks(scene, audio):
    delay_seconds = scene.settings[DELAY] / 1000
    windows = []
    for echo in range(1, 6):
        center = 1 + echo * delay_seconds
        first = int((center - 0.07) * RATE)
        last = int((center + 0.07) * RATE)
        windows.append([max(map(abs, audio[2 * first + channel:2 * last:2]))
                        for channel in (0, 1)])
    if windows[0][0] <= 1e-4 or max(windows[1]) <= 1e-4:
        raise ValueError(f"{scene.name}: first two echoes missing")
    if scene.name == "ping_pong" and windows[1][1] <= 1e-4:
        raise ValueError("ping-pong produced no right-channel echo in the second delay window")
    return windows


def characterize(scene, values, command, input_path, work):
    left = values[::2]
    if scene.name == "reverse_stereo":
        texture_only = Scene(scene.name, scene.stimulus, scene.settings | {WET: 0},
                             seconds=scene.seconds)
        delayed = delta(values, run_scene(command, texture_only, input_path, work, len(values)))
        # The scene uses the runner defaults: 120 BPM and quarter-note Reverse
        # (division 0), so the reverse window hop is 0.5 s. Reverse V2 reads a
        # captured segment backwards: a marker written at t0 reappears at
        # 2*T - t0, where T is the next window boundary after t0. Searching at
        # the stimulus times only finds the pre-reverse dry signal.
        hop = 60.0 / 120.0
        markers = (1.25, 1.75, 2.4, 3.1)
        offsets = {}
        for marker in markers:
            boundary = math.ceil(marker / hop) * hop
            expected = 2 * boundary - marker
            first = int((expected - 0.02) * RATE)
            last = int((expected + 0.03) * RATE)
            l = max(range(first, last), key=lambda i: abs(delayed[2 * i]))
            r = max(range(first, last), key=lambda i: abs(delayed[2 * i + 1]))
            if min(abs(delayed[2 * l]), abs(delayed[2 * r + 1])) < 1e-4:
                raise ValueError(f"{scene.name}: reverse marker missing at {expected:.6f}s")
            if abs(l / RATE - expected) > 0.005 or abs(r / RATE - expected) > 0.005:
                raise ValueError(f"{scene.name}: reverse marker timing error at {expected:.6f}s")
            if abs(r - l) > 2:
                raise ValueError(f"{scene.name}: reverse marker stereo misalignment at {expected:.6f}s")
            onset_l = next(i for i in range(first, last) if abs(delayed[2 * i]) >= 1e-4)
            onset_r = next(i for i in range(first, last) if abs(delayed[2 * i + 1]) >= 1e-4)
            offsets[str(marker)] = {"left_peak_frame": l, "right_peak_frame": r,
                                    "peak_offset_frames": r - l,
                                    "left_onset_frame": onset_l, "right_onset_frame": onset_r,
                                    "onset_offset_frames": onset_r - onset_l,
                                    "expected_frame": int(round(expected * RATE))}

        # Negative control: without Reverse the same markers must not appear at
        # the reverse-predicted times. This keeps the detector from accepting
        # ordinary delay taps or a residual carrier as reversed markers.
        control = run_scene(command, Scene(scene.name, scene.stimulus,
                                           scene.settings | {REVERSE: 0}, seconds=scene.seconds),
                            input_path, work, len(values))
        control_texture = run_scene(command, Scene(scene.name, scene.stimulus,
                                                   scene.settings | {REVERSE: 0, WET: 0},
                                                   seconds=scene.seconds),
                                    input_path, work, len(values))
        control_delayed = delta(control, control_texture)
        for marker in markers:
            boundary = math.ceil(marker / hop) * hop
            expected = 2 * boundary - marker
            first = int((expected - 0.02) * RATE)
            last = int((expected + 0.03) * RATE)
            peak = max(max(map(abs, control_delayed[2 * first:2 * last:2])),
                       max(map(abs, control_delayed[2 * first + 1:2 * last:2])))
            if peak >= 1e-4:
                raise ValueError(f"{scene.name}: reverse marker detected without Reverse")

        isolation = {}
        for stimulus, silent_channel in (("reverse_left_only", 1), ("reverse_right_only", 0)):
            control = Scene(scene.name, stimulus, scene.settings, seconds=scene.seconds)
            input_path.write_bytes(little_endian(synthesize(control, None)).tobytes())
            output = run_scene(command, control, input_path, work, len(values))
            isolation[stimulus] = max(map(abs, output[silent_channel::2]))
            if isolation[stimulus] >= 1e-5:
                raise ValueError(f"{stimulus}: reverse crosstalk")
        return {"marker_peaks_without_texture": offsets, "silent_channel_peaks": isolation}

    unchanged = Scene(scene.name, scene.stimulus, scene.settings, seconds=scene.seconds)
    control = run_scene(command, unchanged, input_path, work, len(values))[::2]
    pitch_signal = left
    if scene.name == "delay_step":
        texture_only = Scene(scene.name, scene.stimulus, scene.settings | {WET: 0},
                             scene.events, scene.seconds)
        texture = run_scene(command, texture_only, input_path, work, len(values))
        pitch_signal = delta(values, texture)[::2]
    results = []
    for t, _, value in scene.events:
        i = round(t * RATE / BLOCK) * BLOCK
        post = i + RATE // 50
        results.append({
            "frame": i, "parameter_value": value,
            "boundary_step": abs(left[i] - left[i - 1]),
            "pre_peak_step_20ms": peak_step(left, i - RATE // 50, i),
            "post_peak_step_20ms": peak_step(left, i, post),
            "control_peak_step_20ms": peak_step(control, i, post),
            "max_delta_vs_control_20ms": max(abs(left[j] - control[j]) for j in range(i, post)),
            "zero_crossing_hz_before": frequency(pitch_signal, i - RATE // 10, i),
            "zero_crossing_hz_after": frequency(pitch_signal, i, i + RATE // 20),
            "zero_crossing_hz_after_1s": frequency(pitch_signal, i + RATE, i + RATE + RATE // 10),
        })
    if max(map(abs, left)) <= 1e-4:
        raise ValueError(f"{scene.name}: output is silent")
    if max(row["max_delta_vs_control_20ms"] for row in results) <= 1e-5:
        raise ValueError(f"{scene.name}: change had no effect")
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native", type=Path, required=True)
    parser.add_argument("--wasm-js", type=Path)
    parser.add_argument("--excerpt-wav", type=Path)
    parser.add_argument("--characterize-only", action="store_true")
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--dsp-commit", help="Verified source revision used to build both runners")
    parser.add_argument("--build-profile", help="Verified build profile for both runners")
    args = parser.parse_args()
    excerpt = load_excerpt(args.excerpt_wav) if args.excerpt_wav else None
    commands = {"native": [args.native.resolve()]}
    if args.wasm_js:
        commands["wasm"] = ["node", args.wasm_js.resolve()]
    report = {"rate": RATE, "block": BLOCK, "scenes": [],
              "excerpt": args.excerpt_wav.name if excerpt is not None else "generated stereo performance"}
    args.output_dir.mkdir(parents=True, exist_ok=True)
    renders = {}
    with tempfile.TemporaryDirectory() as directory:
        work = Path(directory)
        input_path = work / "input.f32"
        scenes = TRANSITIONS if args.characterize_only else SCENES + TRANSITIONS
        for scene in scenes:
            stimulus = synthesize(scene, excerpt)
            input_path.write_bytes(little_endian(stimulus[:]).tobytes())
            record = {"scene": scene.name, "seconds": scene.seconds,
                      "settings": BASE | scene.settings, "automation_seconds": scene.events}
            audio = {name: run_scene(cmd, scene, input_path, work, len(stimulus))
                     for name, cmd in commands.items()}
            if scene.stimulus == "impulse":
                record["echo_peak_windows_lr"] = {name: echo_peaks(scene, samples)
                                                  for name, samples in audio.items()}
            if scene in TRANSITIONS:
                record["characterization"] = {}
                for name, cmd in commands.items():
                    input_path.write_bytes(little_endian(stimulus[:]).tobytes())
                    record["characterization"][name] = characterize(scene, audio[name], cmd, input_path, work)
            if not args.characterize_only:
                folder = args.output_dir / scene.name
                folder.mkdir(exist_ok=True)
                write_wav(folder / "dry.wav", stimulus)
                for name, samples in audio.items():
                    path = f"{scene.name}/{name}.wav"
                    write_wav(args.output_dir / path, samples)
                    renders[path] = audio_provenance.render(
                        args.output_dir, path, scene.name, name, name, RATE, f"{scene.name}/dry.wav"
                    )
                    record[f"{name}_peak"] = max(map(abs, samples))
            report["scenes"].append(record)
    report["provenance"] = audio_provenance.manifest(
        __file__, renders, dsp_commit=args.dsp_commit, build_profile=args.build_profile
    )
    (args.output_dir / "review.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Rendered {len(report['scenes'])} cases to {args.output_dir}")


if __name__ == "__main__":
    main()
