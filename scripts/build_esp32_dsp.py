Import("env")

env.AppendUnique(CPPPATH=["$PROJECT_INCLUDE_DIR"])

# Compile the same translation units used by CMake, with ESP32 build flags.
env.BuildSources("$BUILD_DIR/hydra-core", "$PROJECT_DIR/dsp/core")
env.BuildSources("$BUILD_DIR/hydra-esp32", "$PROJECT_DIR/host/native/esp32")
