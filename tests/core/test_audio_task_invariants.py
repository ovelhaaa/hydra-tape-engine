"""Static firmware guards for cross-core source switching and trace attribution.

These tests validate the actual ESP32 audioTask source structure. They are NOT
substitutes for on-device concurrency or real-time stress testing.
"""
import re
import unittest
from pathlib import Path


MAIN_CPP = (Path(__file__).resolve().parents[2] / "src" / "main.cpp").read_text(encoding="utf-8")


class AudioTaskInvariantTests(unittest.TestCase):
    def test_source_is_sampled_once_for_acquisition_and_conversion(self):
        task = MAIN_CPP.split("void audioTask(void *parameter) {", 1)[1]
        source_span = task.split("    // === STAGE A2: TAPECORE / DSP ENGINE ===", 1)[0]
        capture = "const AudioSource blockSource = p_source;"
        self.assertEqual(source_span.count(capture), 1)
        # After the block snapshot, all four selection paths must use the same
        # value even if the command task changes p_source on the other core.
        after = source_span.split(capture, 1)[1]
        self.assertNotRegex(after, r"\bp_source\b")
        self.assertEqual(after.count("if (blockSource == SOURCE_I2S_IN)"), 2)
        self.assertIn("if (blockSource == SOURCE_MP3 && mp3 && file && out)", after)
        self.assertIn("else if (blockSource == SOURCE_SYNTH && p_genActive && melody)", after)
        acquisition, conversion = after.split("int64_t block_start_us =", 1)
        self.assertIn("memset(rxBuf, 0, sizeof(rxBuf));", acquisition)
        self.assertIn("i2s_read(I2S_PORT, rxBuf", acquisition)
        self.assertIn("blockL[i] = (float)rxBuf[i * 2]", conversion)

    def test_heap_trace_is_tied_to_real_queries_including_boundaries(self):
        task = MAIN_CPP.split("void audioTask(void *parameter) {", 1)[1]
        after = task.split("    if (g_bench.state == BENCH_WARMUP) {", 1)[1]
        bench = after.split("    AudioHealth health;", 1)[0]
        self.assertEqual(bench.count("dlHeapChecked = true;"), 3)
        for suffix in (
            "g_bench.startHeap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);",
            "const uint32_t hNow = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);",
            "g_bench.endHeap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);",
        ):
            pattern = r"dlHeapChecked = true;\s*#endif\s*" + re.escape(suffix)
            self.assertRegex(bench, pattern)
        self.assertIn("dlBenchSampled = true;", bench)
        trace = task.split("const bool benchRun =", 1)[1].split("if (isOverrun)", 1)[0]
        self.assertIn("dlBenchSampled;", trace)
        self.assertIn("const bool heapChk = dlHeapChecked;", trace)
        self.assertNotIn("g_bench.collected", trace)

    def test_trace_reports_the_used_source_not_later_control_state(self):
        task = MAIN_CPP.split("void audioTask(void *parameter) {", 1)[1]
        self.assertIn("ev.mode = (uint8_t)blockSource;", task)


if __name__ == "__main__":
    unittest.main()
