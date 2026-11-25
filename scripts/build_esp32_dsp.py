Import("env")

# Stamp the actual firmware inputs, including dirty builds, without changing DSP.
import hashlib
import json
from pathlib import Path
import subprocess

root = Path(env.subst("$PROJECT_DIR"))
paths = subprocess.check_output(
    ["git", "ls-files", "src", "include", "lib", "dsp/core", "host/native/esp32",
     "platformio.ini", "scripts/build_esp32_dsp.py"], cwd=root, text=True).splitlines()
digest = hashlib.sha256()
for name in sorted(paths):
    digest.update(name.encode() + b"\0" + (root / name).read_bytes() + b"\0")
commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()
dirty = subprocess.run(["git", "diff", "--quiet", "HEAD", "--", *paths], cwd=root).returncode
identity = root / env.subst("$BUILD_DIR") / "hydra_firmware_identity.h"
identity.parent.mkdir(parents=True, exist_ok=True)
content = ('#pragma once\n#define HYDRA_FIRMWARE_COMMIT "' + commit
           + ('-dirty' if dirty else '') + '"\n#define HYDRA_FIRMWARE_SOURCE_SHA256 "'
           + digest.hexdigest() + '"\n'
           + '#define HYDRA_FIRMWARE_BUILD_NAME "' + env.subst('$PIOENV') + '"\n'
           + '#define HYDRA_FIRMWARE_FLAGS ' + json.dumps(str(env.get('BUILD_FLAGS', []))) + '\n')
if not identity.exists() or identity.read_text() != content:
    identity.write_text(content)
env.AppendUnique(CPPPATH=[env.subst("$BUILD_DIR")])

env.AppendUnique(CPPPATH=["$PROJECT_INCLUDE_DIR"])

# Compile the same translation units used by CMake, with ESP32 build flags.
env.BuildSources("$BUILD_DIR/hydra-core", "$PROJECT_DIR/dsp/core")
env.BuildSources("$BUILD_DIR/hydra-esp32", "$PROJECT_DIR/host/native/esp32")
