"""Instrument copies of the short trace and TapeCore for a native/WASM audit."""
import argparse
import collections
import hashlib
import json
import math
from pathlib import Path
import re
import shlex
import subprocess
import tempfile


def run(cmd, **kwargs):
    return subprocess.run(cmd, check=True, text=True, capture_output=True, **kwargs)


def source(revision, name):
    return run(["git", "show", f"{revision}:{name}"]).stdout


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise ValueError(f"probe anchor changed: {old}")
    return text.replace(old, new)


def instrument_core(core):
    header = """
#include <cstdio>
static int audit_frame = -1;
#define AUDIT(key, value) std::fprintf(stderr, "%s,%d,%a\\n", key, audit_frame, double(value))
"""
    # Report the libm return values and normalized coefficients for each setter.
    for name in ("Lowpass", "Highpass", "Peak", "HighShelf"):
        match = re.search(rf"(?m)^  void set{name}\(.*?\}}$", core)
        if not match:
            raise ValueError(f"missing filter setter: {name}")
        values = ["w0", "s", "c", "A"] if name == "Peak" else ["w0", "sinw0", "cosw0"]
        if name == "HighShelf":
            values.append("A")
        values += ["b0", "b1", "b2", "a1", "a2"]
        probes = "".join(f' AUDIT("{name}.{value}",{value});' for value in values)
        core = replace_once(core, match[0], match[0][:-1] + probes + " }")
    for anchor, probe in [
        ("  impl_->flutterPhase += impl_->flutterInc;", "  ++audit_frame;"),
        ("      table[i] = std::tanh(x);", '      AUDIT("LUT.tanh",std::tanh(x));'),
        ("    float y = lut->process(h);", '    AUDIT("magLutInput",h);'),
        ("    m = remanence * m + (1.0f - remanence) * y;", '    AUDIT("magLutOutput",y);'),
        ("    frame.conditionedInput=iLP.process(iHP.process(input));",
         '    const float highpass=iHP.process(input); AUDIT("inputHPF",highpass);'),
        ("    // --- delay read ---",
         '    AUDIT("conditionedInput",frame.conditionedInput); AUDIT("readModulation",mod);'),
        ("    return impl_->outputLimiter", '    AUDIT("recordedSample",buffer[impl_->writeHead]);'),
        ("    // --- dropout/gap loss ---", '    AUDIT("texture",textureSig); AUDIT("delay",delaySig);'),
        ("    frame.tapeOutput=outputGapLossFilter.process(", '    AUDIT("prePlayback",tapeSig);'),
        ("    return frame;", '    AUDIT("tapeOutput",frame.tapeOutput);'),
        ("    // --- magnetic saturation/write ---", '    AUDIT("recordInput",recSig);'),
    ]:
        core = replace_once(core, anchor, probe + "\n" + anchor)
    core = core.replace("frame.conditionedInput=iLP.process(iHP.process(input))",
                        "frame.conditionedInput=iLP.process(highpass)")
    return header + core


def instrument_trace(trace, shared_input):
    if shared_input:
        trace = replace_once(
            trace,
            "inL[i] = 0.45f * std::sin(2.0f * 3.1415926535f * 220.0f * t) + "
            "0.2f * std::sin(2.0f * 3.1415926535f * 330.0f * t);",
            "inL[i] = audit_input[i];",
        )
        values = [0.45 * math.sin(2 * math.pi * 220 * i / 48000)
                  + 0.2 * math.sin(2 * math.pi * 330 * i / 48000) for i in range(2048)]
        trace = ("static constexpr float audit_input[] = {"
                 + ",\n".join(v.hex() + "f" for v in values) + "};\n" + trace)
    else:
        trace = replace_once(
            trace, "    inR[i] = 0.9f * inL[i];",
            '    const float angle220 = 2.0f * 3.1415926535f * 220.0f * t;\n'
            '    const float angle330 = 2.0f * 3.1415926535f * 330.0f * t;\n'
            '    std::fprintf(stderr, "angle220,%u,%a\\n", i, double(angle220));\n'
            '    std::fprintf(stderr, "angle330,%u,%a\\n", i, double(angle330));\n'
            '    std::fprintf(stderr, "sinf220,%u,%a\\n", i, double(std::sin(angle220)));\n'
            '    std::fprintf(stderr, "sinf330,%u,%a\\n", i, double(std::sin(angle330)));\n'
            '    inR[i] = 0.9f * inL[i];')
    return '#include <cstdio>\n' + replace_once(trace, "    inR[i] = 0.9f * inL[i];",
        '    inR[i] = 0.9f * inL[i];\n'
        '    std::fprintf(stderr, "stimulus,%u,%a\\n", i, double(inL[i]));')


def read_probes(output):
    stages = collections.defaultdict(list)
    for line in output.splitlines():
        stage, frame, value = line.split(",")
        stages[stage].append((int(frame), float.fromhex(value), value))
    return stages


def compare(native, wasm):
    if native.keys() != wasm.keys():
        raise ValueError("probe keys differ")
    summary = {}
    for stage, samples in native.items():
        other = wasm[stage]
        if len(samples) != len(other):
            raise ValueError(f"probe lengths differ: {stage}")
        differences = []
        for index, (n, w) in enumerate(zip(samples, other)):
            if n[0] != w[0]:
                raise ValueError(f"probe frames differ: {stage}")
            error = abs(n[1] - w[1])
            if error:
                differences.append(dict(frame=n[0], occurrence=index, native=n[2],
                                        wasm=w[2], abs_error=error))
        if differences:
            summary[stage] = dict(first=differences[0], count=len(differences),
                                  peak=max(differences, key=lambda d: d["abs_error"]))
    return summary


def output_stages(stdout):
    values = collections.defaultdict(list)
    for frame, row in enumerate(stdout.splitlines()):
        for channel, field in zip(("outL", "outR"), row.split(","), strict=True):
            value = float(field)
            values[channel].append((frame, value, field))
    if any(len(values[channel]) != 2048 for channel in ("outL", "outR")):
        raise ValueError("expected 2048 frames")
    return values



def find_current_filter_setter(core, name):
    """Match one filter setter, with or without its ESP32 IRAM annotation.

    A missing or duplicated setter remains a hard error; probes are never
    silently omitted from the native/WASM diagnostic.
    """
    pattern = rf"(?m)^  (?:HYDRA_DSP_IRAM[ \t]+)?void set{name}\(.*?\}}$"
    matches = list(re.finditer(pattern, core))
    if len(matches) != 1:
        raise ValueError(
            f"missing or ambiguous current filter setter: {name} (found {len(matches)})"
        )
    return matches[0]


def instrument_current_core(core):
    """Diagnostic-only probes; fail closed if a current-core anchor changes."""
    header = """
#include <cstdio>
static int audit_frame = -1;
static int audit_channel = -1;
#define AUDIT_CURRENT(key, value) std::fprintf(stderr, "%s.%d,%d,%a\\n", key, audit_channel, audit_frame, double(value))
"""
    for name in ("LowShelf", "Lowpass", "Highpass", "Peak", "HighShelf"):
        match = find_current_filter_setter(core, name)
        values = ["fs", "freq", "Q", "w0"]
        if name in ("LowShelf", "HighShelf", "Peak"):
            values += ["gainDB", "A"]
        values += ["s", "c"] if name == "Peak" else ["sinw0", "cosw0"]
        values += ["alpha", "a0", "b0", "b1", "b2", "a1", "a2"]
        probes = "".join(f' AUDIT_CURRENT("{name}.{value}",{value});' for value in values)
        core = replace_once(core, match[0], match[0][:-1] + probes + " }")
    replacements = [
        ("  impl_->flutterPhase += impl_->flutterInc;", "  ++audit_frame; audit_channel=-1;\n  impl_->flutterPhase += impl_->flutterInc;"),
        ("    frame.conditionedInput=iLP.process(iHP.process(input));",
         '    audit_channel=(buffer==impl_->delayLine)?0:1; AUDIT_CURRENT("input",input);\n'
         '    const float audit_hp=iHP.process(input); AUDIT_CURRENT("inputHPF",audit_hp);\n'
         '    frame.conditionedInput=iLP.process(audit_hp); AUDIT_CURRENT("conditionedInput",frame.conditionedInput);'),
        ("    frame.tapeOutput=outputGapLossFilter.process(",
         '    AUDIT_CURRENT("prePlayback",tapeSig); AUDIT_CURRENT("readModulation",mod);\n    frame.tapeOutput=outputGapLossFilter.process('),
        ("    return frame;", '    AUDIT_CURRENT("tapeOutput",frame.tapeOutput);\n    return frame;'),
        ("    const float freshInput = frame.conditionedInput * impl_->driveGain;",
         '    audit_channel=(buffer==impl_->delayLine)?0:1;\n    const float freshInput = frame.conditionedInput * impl_->driveGain;'),
        ("    // --- magnetic saturation/write ---",
         '    AUDIT_CURRENT("recordInput",recSig); AUDIT_CURRENT("feedbackInput",feedbackInput);\n    // --- magnetic saturation/write ---'),
        ("    HYDRA_PROFILE_END(MagneticWrite, tMagWrite);",
         '    HYDRA_PROFILE_END(MagneticWrite, tMagWrite);\n    AUDIT_CURRENT("recordedSample",buffer[impl_->writeHead]);'),
        ("    return limited;", '    AUDIT_CURRENT("limitedOutput",limited);\n    return limited;'),
        ("    float y = 0.5f * (lut->process(h - b) + lut->process(h + b));",
         '    AUDIT_CURRENT("magLutInput",h); AUDIT_CURRENT("magBias",b); AUDIT_CURRENT("magPriorState",m);\n'
         '    float y = 0.5f * (lut->process(h - b) + lut->process(h + b));\n    AUDIT_CURRENT("magLutOutput",y);'),
        ("    m = remanence * m + (1.0f - remanence) * y;",
         '    m = remanence * m + (1.0f - remanence) * y;\n    AUDIT_CURRENT("magState",m);'),
    ]
    for anchor, replacement in replacements:
        core = replace_once(core, anchor, replacement)
    return header + core


def current_diagnostic(args):
    """Compile clean copies of the working tree, never modifying production DSP."""
    import platform
    import shutil
    root = Path(__file__).resolve().parents[2]
    output = args.output.resolve()
    work = output.parent / (output.stem + "-probe-build")
    # Unique directories avoid reusing any previous compiler output/cache.
    work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="clean-", dir=work))
    hashes = {}
    for directory, suffixes in (("include", (".h", ".hpp")), ("dsp/core", (".h", ".hpp", ".cpp", ".inc"))):
        for path in (root / directory).iterdir():
            if path.suffix in suffixes:
                shutil.copyfile(path, work / path.name)
                hashes[str(path.relative_to(root))] = hashlib.sha256(path.read_bytes()).hexdigest()
    for filename in ("render_trace.cpp", "check.h", "canonical_trace_input.inc"):
        path = root / "tests/core" / filename
        if path.exists():
            shutil.copyfile(path, work / filename)
            hashes[str(path.relative_to(root))] = hashlib.sha256(path.read_bytes()).hexdigest()
    compilers = [shlex.split(args.native_cxx), shlex.split(args.wasm_cxx)]
    flags = ["-std=c++17", "-O3", "-DNDEBUG", "-I."] + shlex.split(args.extra_flags)
    result = {"mode": "current-working-tree-diagnostic-only", "checkout_sha": run(["git", "rev-parse", "HEAD"], cwd=root).stdout.strip(),
              "platform": platform.platform(), "libc": platform.libc_ver(), "source_sha256": hashes,
              "compilers": [run(c + ["--version"]).stdout for c in compilers], "flags": flags,
              "build_directory": str(work), "scenarios": {}, "commands": []}
    core = (work / "tape_core.cpp").read_text(encoding="utf-8")
    binaries = {}
    for variant in ("release", "instrumented"):
        (work / "tape_core.cpp").write_text(instrument_current_core(core) if variant == "instrumented" else core, encoding="utf-8")
        binaries[variant] = []
        for label, compiler, wasm in zip(("native", "wasm"), compilers, (False, True), strict=True):
            binary = work / f"{variant}-{label}{'.js' if wasm else '.exe'}"
            cmd = compiler + flags + ["tape_core.cpp", "hydra_dsp_c_api.cpp", "render_trace.cpp", "-o", str(binary)]
            if wasm:
                cmd += ["-sENVIRONMENT=node"]
            result["commands"].append(cmd)
            compiled = run(cmd, cwd=work)
            (work / f"{variant}-{label}-compiler.log").write_text(compiled.stdout + compiled.stderr, encoding="utf-8")
            result.setdefault("binary_sha256", {})[binary.name] = hashlib.sha256(binary.read_bytes()).hexdigest()
            if wasm:
                module = binary.with_suffix(".wasm")
                result["binary_sha256"][module.name] = hashlib.sha256(module.read_bytes()).hexdigest()
            binaries[variant].append((["node"] if wasm else []) + [str(binary)])
    scenarios = run(binaries["release"][0] + ["--list-scenarios"]).stdout.splitlines()
    for scenario in scenarios:
        variants = {}
        for variant, commands in binaries.items():
            samples = []
            for label, command in zip(("native", "wasm"), commands, strict=True):
                sample = run(command + ["--scenario", scenario])
                for stream in ("stdout", "stderr"):
                    (work / f"{scenario}-{variant}-{label}.{stream}.csv").write_text(getattr(sample, stream), encoding="utf-8")
                samples.append(sample)
            n, w = samples
            native, wasm = output_stages(n.stdout), output_stages(w.stdout)
            errors = [abs(a[1] - b[1]) for channel in native for a, b in zip(native[channel], wasm[channel], strict=True)]
            metrics = {"max_abs": max(errors), "rmse": math.sqrt(math.fsum(e*e for e in errors)/len(errors)),
                       "channels": compare(native, wasm), "trace_sha256": [hashlib.sha256(s.stdout.encode("ascii")).hexdigest() for s in samples]}
            if variant == "instrumented":
                probes = [read_probes(s.stderr) for s in samples]
                metrics["operations"] = compare(*probes)
                metrics["probe_counts"] = {stage: len(values) for stage, values in probes[0].items()}
            variants[variant] = metrics
        variants["instrumentation_preserves_output_per_backend"] = variants["release"]["trace_sha256"] == variants["instrumented"]["trace_sha256"]
        result["scenarios"][scenario] = variants
        print(f'{scenario}: release max_abs={variants["release"]["max_abs"]:.12g} rmse={variants["release"]["rmse"]:.12g}; instrumented output unchanged={variants["instrumentation_preserves_output_per_backend"]}')
        for stage, difference in variants["instrumented"]["operations"].items():
            print(f'  {stage}: first_frame={difference["first"]["frame"]} occurrence={difference["first"]["occurrence"]} abs={difference["first"]["abs_error"]:.9g} count={difference["count"]}')
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(f"Diagnostic evidence: {output}; raw probes and clean binaries: {work}")

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base")
    parser.add_argument("--current", action="store_true", help="Audit clean copies of current sources; no historical LUT anchors")
    parser.add_argument("--extra-flags", default="", help="Additional flags for both diagnostic compilers")
    parser.add_argument("--head", default="HEAD")
    parser.add_argument("--native-cxx", default="c++")
    parser.add_argument("--wasm-cxx", default="em++")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.current:
        current_diagnostic(args)
        return
    if not args.base:
        parser.error("--base is required unless --current is selected")
    compilers = [shlex.split(args.native_cxx), shlex.split(args.wasm_cxx)]
    result = {"compilers": [run(c + ["--version"]).stdout for c in compilers]}
    macros = subprocess.run(compilers[0] + ["-dM", "-E", "-x", "c++", "-"],
                            input="", text=True, capture_output=True, check=True).stdout
    result["native_fma_macro"] = any(line.startswith("#define __FMA__ ") for line in macros.splitlines())
    with tempfile.TemporaryDirectory() as directory:
        work = Path(directory)
        for name, revision in (("base", args.base), ("head", args.head)):
            for filename in ("include/hydra_dsp.h", "include/hydra_mech_noise.hpp",
                             "dsp/core/tape_core.hpp", "dsp/core/tape_core.cpp",
                             "dsp/core/hydra_dsp_c_api.cpp", "tests/core/render_trace.cpp"):
                (work / Path(filename).name).write_text(source(revision, filename), encoding="utf-8")
            core = (work / "tape_core.cpp").read_text(encoding="utf-8")
            trace = (work / "render_trace.cpp").read_text(encoding="utf-8")
            if '#include "check.h"' in trace:
                (work / "check.h").write_text(source(revision, "tests/core/check.h"), encoding="utf-8")
            configurations = [("release", False, False, False)]
            if name == "head":
                configurations += [("instrumented", True, False, False),
                                   ("shared_input", True, True, False),
                                   ("contraction_off", True, False, True),
                                   ("shared_input_contraction_off", True, True, True),
                                   ("native_tanh_table", True, True, True)]
            result[name] = {}
            for variant, instrument, shared, no_contract in configurations:
                (work / "tape_core.cpp").write_text(instrument_core(core) if instrument else core, encoding="utf-8")
                (work / "render_trace.cpp").write_text(instrument_trace(trace, shared) if instrument else trace,
                                                      encoding="utf-8")
                flags = ["-std=c++17", "-O3", "-I."]
                if name == "head":
                    flags.append("-DNDEBUG")
                if no_contract:
                    flags.append("-ffp-contract=off")
                samples = []
                for compiler, wasm in zip(compilers, (False, True), strict=True):
                    if wasm and variant == "native_tanh_table":
                        table = read_probes(samples[0].stderr)["LUT.tanh"]
                        if len(table) != 1025:
                            raise ValueError("expected one complete tanh lookup table")
                        traced_core = instrument_core(core)
                        traced_core = replace_once(traced_core, "table[i] = std::tanh(x);",
                                                   "table[i] = audit_tanh[i];")
                        table_definition = "static constexpr float audit_tanh[] = {" + ",\n".join(
                            value.hex() + "f" for _, value, _ in table) + "};\n"
                        (work / "tape_core.cpp").write_text(table_definition + traced_core, encoding="utf-8")
                    binary = work / ("trace.js" if wasm else "trace.exe")
                    cmd = compiler + flags + ["tape_core.cpp", "hydra_dsp_c_api.cpp", "render_trace.cpp",
                                              "-o", str(binary)]
                    if wasm:
                        cmd += ["-sENVIRONMENT=node"]
                    subprocess.run(cmd, cwd=work, check=True)
                    samples.append(run((["node"] if wasm else []) + [str(binary)]))
                n, w = samples
                native, wasm = output_stages(n.stdout), output_stages(w.stdout)
                channels = compare(native, wasm)
                errors = [abs(a[1] - b[1]) for channel in native
                          for a, b in zip(native[channel], wasm[channel], strict=True)]
                metrics = dict(max_abs=max(errors), rmse=math.sqrt(math.fsum(e * e for e in errors) / len(errors)),
                               channels=channels, flags=flags,
                               trace_sha256=[hashlib.sha256(sample.stdout.encode("ascii")).hexdigest()
                                             for sample in samples])
                if instrument:
                    metrics["operations"] = compare(read_probes(n.stderr), read_probes(w.stderr))
                result[name][variant] = metrics
    result["release_trace_unchanged_per_backend"] = (
        result["base"]["release"]["trace_sha256"] == result["head"]["release"]["trace_sha256"]
    )
    print(f'SHA base and PR have identical Release traces per backend: '
          f'{result["release_trace_unchanged_per_backend"]}')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(f'native target defines __FMA__: {result["native_fma_macro"]}')
    for name in ("base", "head"):
        for variant, data in result[name].items():
            print(f'{name}/{variant}: max_abs={data["max_abs"]:.12g} rmse={data["rmse"]:.12g}')
            for stage in ("angle220", "angle330", "sinf220", "sinf330", "stimulus", "inputHPF",
                          "conditionedInput", "HighShelf.a2", "readModulation",
                          "LUT.tanh", "magLutInput", "magLutOutput", "recordedSample", "texture", "tapeOutput"):
                difference = data.get("operations", {}).get(stage)
                if difference:
                    print(f'  {stage}: first={difference["first"]["frame"]} '
                          f'peak={difference["peak"]["abs_error"]:.9g}')


if __name__ == "__main__":
    main()
