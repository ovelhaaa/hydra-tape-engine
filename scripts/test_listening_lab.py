"""Delivery failures must prevent publication; no DSP thresholds are changed."""
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import wave
import numpy as np
from package_listening_lab import validate
from render_phase6_review import resolve_library
import musical_sources

class ListeningDeliveryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def fixture(self):
        manifest = {"schema_version": 1, "human_listening_signoff": "PENDING_HUMAN_AUDITION", "reference_commit": "a" * 40, "api_version": 10700, "scenarios": {}}
        for i in range(1, 21):
            scenario = {"duration_sec": 512 / 48000, "sample_rate_hz": 48000, "dsp_params": {}, "markers": [], "analysis_metrics": {}, "events": [], "tempo_source": 0, "random_seed": i}
            scenario["reference_commit"] = manifest["reference_commit"]
            for kind in ("original_wav", "processed_wav", "telemetry_csv"):
                path = self.root / f"{i:02}_{kind}.{'csv' if kind.endswith('csv') else 'wav'}"
                if kind.endswith("wav"):
                    with wave.open(str(path), "wb") as wav:
                        wav.setparams((2, 3, 48000, 0, "NONE", "not compressed"))
                        wav.writeframes(bytes(512 * 6))
                else:
                    path.write_text("time_sec,tempo_source\n0,0\n", encoding="utf-8")
                scenario[f"{kind}_file"] = path.name
                scenario[f"{kind}_sha256"] = hashlib.sha256(path.read_bytes()).hexdigest()
            manifest["scenarios"][f"{i:02}_test"] = scenario
        (self.root / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
        return manifest

    def test_valid_manifest_and_twenty_filesets(self):
        self.fixture()
        self.assertGreater(validate(self.root)[1], 0)

    def test_missing_wav_csv_empty_hash_and_truncation(self):
        for failure in ("missing_wav", "missing_csv", "empty", "hash", "truncated", "truncated_data"):
            with self.subTest(failure=failure):
                manifest = self.fixture()
                scenario = manifest["scenarios"]["01_test"]
                kind = "telemetry_csv" if failure == "missing_csv" else "processed_wav"
                path = self.root / scenario[f"{kind}_file"]
                if failure.startswith("missing"):
                    path.unlink()
                elif failure == "empty":
                    path.write_bytes(b"")
                elif failure == "hash":
                    path.write_bytes(b"corrupt")
                elif failure == "truncated_data":
                    path.write_bytes(path.read_bytes()[:-6])
                    scenario[f"{kind}_sha256"] = hashlib.sha256(path.read_bytes()).hexdigest()
                    (self.root / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
                else:
                    with wave.open(str(path), "wb") as wav:
                        wav.setparams((2, 3, 48000, 0, "NONE", "not compressed"))
                        wav.writeframes(bytes(128 * 6))
                    scenario[f"{kind}_sha256"] = hashlib.sha256(path.read_bytes()).hexdigest()
                    (self.root / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
                with self.assertRaises(ValueError):
                    validate(self.root)

    def test_wrong_count_and_invalid_manifest(self):
        manifest = self.fixture()
        del manifest["scenarios"]["20_test"]
        (self.root / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
        with self.assertRaises(ValueError):
            validate(self.root)
        (self.root / "manifest.json").write_text("{", encoding="utf-8")
        with self.assertRaises(ValueError):
            validate(self.root)

    def test_platform_library_discovery_and_active_configuration(self):
        for platform, name in (("win32", "hydra_abi_test_library.dll"), ("linux", "libhydra_abi_test_library.so"), ("darwin", "libhydra_abi_test_library.dylib")):
            with patch("sys.platform", platform):
                active = self.root / "Release" / name
                active.parent.mkdir(exist_ok=True)
                active.touch()
                (self.root / name).touch()
                self.assertEqual(resolve_library(None, self.root, "Release"), active)
                self.assertEqual(resolve_library(active, self.root, "Debug"), active)
        with self.assertRaises(FileNotFoundError):
            resolve_library(self.root / "missing", self.root, "Release")

    def test_fixed_source_seeds(self):
        for kind in ("clean_guitar", "driven_guitar", "mono_synth", "poly_synth", "drum_machine", "irregular_percussion"):
            np.random.seed(6101)
            first = musical_sources.get_musical_source(kind, 1, 48000)
            np.random.seed(6101)
            second = musical_sources.get_musical_source(kind, 1, 48000)
            np.testing.assert_array_equal(first, second)

if __name__ == "__main__":
    unittest.main()
