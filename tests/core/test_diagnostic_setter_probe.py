"""Regression tests for the fail-closed current-core filter setter locator."""
import unittest
from pathlib import Path

from diagnose_equivalence import find_current_filter_setter


class CurrentFilterSetterLocatorTests(unittest.TestCase):
    def test_unannotated_setter(self):
        core = "  void setLowShelf(float fs){ float a0=1; }\n"
        match = find_current_filter_setter(core, "LowShelf")
        self.assertEqual(match.group(0).strip(), core.strip())

    def test_iram_annotated_setter(self):
        core = "  HYDRA_DSP_IRAM void setLowShelf(float fs){ float a0=1; }\n"
        match = find_current_filter_setter(core, "LowShelf")
        self.assertEqual(match.group(0).strip(), core.strip())

    def test_missing_setter_fails_closed(self):
        with self.assertRaisesRegex(ValueError, "missing or ambiguous"):
            find_current_filter_setter("  void unrelated(){}\n", "LowShelf")

    def test_duplicate_setters_fail_closed(self):
        core = ("  void setLowShelf(float fs){ float a0=1; }\n"
                "  HYDRA_DSP_IRAM void setLowShelf(float fs){ float a0=1; }\n")
        with self.assertRaisesRegex(ValueError, "found 2"):
            find_current_filter_setter(core, "LowShelf")

    def test_real_current_core_has_all_five_setters(self):
        core = (Path(__file__).resolve().parents[2] /
                "dsp/core/tape_core.cpp").read_text(encoding="utf-8")
        for name in ("LowShelf", "Lowpass", "Highpass", "Peak", "HighShelf"):
            with self.subTest(setter=name):
                match = find_current_filter_setter(core, name)
                self.assertIn("void set" + name + "(", match.group(0))


if __name__ == "__main__":
    unittest.main()
