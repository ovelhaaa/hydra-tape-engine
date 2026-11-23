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
from render_phase6_review import resolve_library, decode_api_version, verify_runner_abi_compatibility
import musical_sources

class ListeningDeliveryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def fixture(self, count=20):
        manifest = {"schema_version": 1, "human_listening_signoff": "PENDING_HUMAN_AUDITION", "reference_commit": "a" * 40, "api_version": 10700, "scenarios": {}}
        if count == 60:
            manifest["suite"] = "Hydra Phase 6.2 Bias A/B"
        for i in range(1, count + 1):
            scenario = {"duration_sec": 512 / 48000, "sample_rate_hz": 48000, "dsp_params": {}, "markers": [], "analysis_metrics": {}, "events": [], "tempo_source": 0, "random_seed": i}
            scenario["reference_commit"] = manifest["reference_commit"]
            scenario["markers"] = [{"time": 0, "label": "Audio start"}, {"time": scenario["duration_sec"], "label": "Audio end"}]
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

    def test_bias_supplement_requires_sixty_complete_filesets(self):
        manifest = self.fixture(60)
        self.assertGreater(validate(self.root)[1], 0)
        del manifest["scenarios"]["60_test"]
        (self.root / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "60"):
            validate(self.root)

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

    def test_abi_compatibility_checks(self):
        class FakeLib:
            def __init__(self, specs):
                self._specs = specs

            def hydra_dsp_get_param_spec(self, pid, out_ptr):
                import ctypes
                if pid not in self._specs:
                    return -1
                spec = ctypes.cast(out_ptr, ctypes.POINTER(self._specs[pid].__class__)).contents
                src = self._specs[pid]
                spec.id = src.id
                spec.min_value = src.min_value
                spec.max_value = src.max_value
                spec.default_value = src.default_value
                spec.smoothing = src.smoothing
                spec.flags = src.flags
                return 0

        from render_phase6_review import HydraDspParamSpec
        class FakeRunner:
            def __init__(self, api_ver, param_count, specs):
                self.api_version = api_ver
                self.param_count = param_count
                self.lib = FakeLib(specs)

        specs_v17 = {
            i: HydraDspParamSpec(id=i, name=b"p", min_value=0.0, max_value=100.0, default_value=50.0, smoothing=1, flags=0)
            for i in range(38)
        }
        specs_v18 = dict(specs_v17)
        specs_v18[38] = HydraDspParamSpec(id=38, name=b"m", min_value=0.0, max_value=3.0, default_value=0.0, smoothing=0, flags=0)

        v17 = (1 << 16) | (7 << 8)
        v18 = (1 << 16) | (8 << 8)
        v16 = (1 << 16) | (6 << 8)
        v20 = (2 << 16) | (0 << 8)
        self.assertEqual(decode_api_version(v18), (1, 8, 0))
        res = verify_runner_abi_compatibility(FakeRunner(v17, 38, specs_v17), FakeRunner(v18, 47, specs_v18))
        self.assertEqual(res["before_api_semver"], [1, 7, 0])
        self.assertEqual(res["after_api_semver"], [1, 8, 0])

        with self.assertRaisesRegex(RuntimeError, "major"):
            verify_runner_abi_compatibility(FakeRunner(v17, 38, specs_v17), FakeRunner(v20, 47, specs_v18))
        with self.assertRaisesRegex(RuntimeError, "minimum"):
            verify_runner_abi_compatibility(FakeRunner(v16, 38, specs_v17), FakeRunner(v18, 47, specs_v18))
        with self.assertRaisesRegex(RuntimeError, "Insufficient"):
            verify_runner_abi_compatibility(FakeRunner(v17, 37, specs_v17), FakeRunner(v18, 47, specs_v18))
        bad_specs = dict(specs_v18)
        bad_specs[12] = HydraDspParamSpec(id=12, name=b"d", min_value=1.0, max_value=100.0, default_value=50.0, smoothing=1, flags=0)
        with self.assertRaisesRegex(RuntimeError, "ABI mismatch"):
            verify_runner_abi_compatibility(FakeRunner(v17, 38, specs_v17), FakeRunner(v18, 47, bad_specs))

if __name__ == "__main__":
    unittest.main()
