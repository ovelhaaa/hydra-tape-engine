# Unit Testing Implementation Plan

## Goal

Implement unit tests for critical DSP components to ensure correctness and prevent regressions.

---

## Proposed Changes

### Test Structure

PlatformIO supports native testing (runs on host PC) which is ideal for pure DSP math that doesn't require hardware.

```
test/
├── test_native/           # Native tests (run on PC)
│   ├── test_biquad.cpp    # BiquadFilter TDF2 tests
│   ├── test_dcblocker.cpp # DCBlocker tests
│   ├── test_allpass.cpp   # AllpassFilter tests
│   └── test_scales.cpp    # MelodyGen scale lookup tests
└── README                 # (existing)
```

---

### [NEW] platformio.ini additions

Add native test environment:

```ini
[env:native]
platform = native
test_framework = unity
build_flags =
    -DUNITY_INCLUDE_DOUBLE
    -std=c++14
```

---

### [NEW] test/test_native/test_biquad.cpp

Test the corrected BiquadFilter TDF2 implementation:

- Verify DC gain (input=1.0 constant should produce expected output)
- Verify lowpass behavior (sine at various frequencies)
- Test denormal protection
- Compare with known reference values

---

### [NEW] test/test_native/test_dcblocker.cpp

Test DCBlocker:

- Verify DC removal (constant input → output approaches 0)
- Verify AC passthrough (sine wave amplitude preserved)

---

### [NEW] test/test_native/test_allpass.cpp

Test AllpassFilter:

- Verify phase shift without amplitude change
- Test coefficient bounds

---

### [NEW] test/test_native/test_scales.cpp

Test MelodyGen scale lookup tables:

- Verify MAJOR scale returns {0, 2, 4, 5, 7, 9, 11}
- Verify MINOR scale returns {0, 2, 3, 5, 7, 8, 10}
- Verify octave wraparound for degrees > scale size
- Test negative degree handling

---

## Verification Plan

### Automated Tests

Run native tests on PC (no hardware required):

```bash
pio test -e native
```

**Expected output:** All tests pass with green OK status.

### Manual Verification

1. User compiles main firmware: `pio run -e esp32-s3-devkitc-1`
2. Upload and verify audio still works correctly with the DSP changes
