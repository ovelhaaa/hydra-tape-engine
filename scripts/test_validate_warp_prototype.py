import unittest
from unittest.mock import patch

import numpy as np

from validate_warp_prototype import ReferenceAccidentalWarpDSP, WARP_DEFAULTS, run_python


class PersistentReferenceTest(unittest.TestCase):
    def test_impulse_preserves_buffer_phase_and_filter_memory(self):
        dsp = ReferenceAccidentalWarpDSP(fs=1000, grain_ms=64, speed=1, stereo=0, feedback=50)
        self.assertEqual(dsp.process_frame(1.0, 1.0), (0.0, 0.0))
        self.assertEqual(dsp.delay_l.buffer[0], 1.0)
        self.assertEqual(dsp.write_pos, 1)

        for frame in range(1, 16):
            self.assertEqual(dsp.process_frame(0.0, 0.0), (0.0, 0.0))
            self.assertEqual(dsp.write_pos, frame + 1)
            self.assertEqual(dsp.delay_l.buffer[0], 1.0)

        # At n=16 the half-period tap reads index 0 with triangular gain 1/2.
        self.assertEqual(dsp.process_frame(0.0, 0.0), (0.5, 0.5))
        filter_state = dsp.fb_lpf_l.z1
        self.assertNotEqual(filter_state, 0)
        dsp.process_frame(0.0, 0.0)
        self.assertNotEqual(dsp.fb_lpf_l.z1, 0)
        self.assertNotEqual(dsp.fb_lpf_l.z1, filter_state)
        self.assertEqual(dsp.write_pos, 18)
        self.assertEqual(dsp.dc_l.x1, dsp.dc_r.x1)

        for _ in range(64 - 18):
            dsp.process_frame(0.0, 0.0)
        self.assertEqual(dsp.write_pos, 0)
        self.assertEqual(dsp.delay_l.write_idx, 0)

    def test_runner_constructs_once_per_render_and_emits_delayed_impulse(self):
        stimulus = np.zeros((4096, 2), dtype=np.float32)
        stimulus[0] = 1.0
        with patch("validate_warp_prototype.ReferenceAccidentalWarpDSP",
                   wraps=ReferenceAccidentalWarpDSP) as constructor:
            output = run_python(stimulus, WARP_DEFAULTS, [])
            constructor.assert_called_once()
        self.assertEqual(output[0, 0], 0.0)
        self.assertEqual(output[960, 0], 0.5)
        self.assertGreater(np.count_nonzero(output[960:]), 2)


if __name__ == "__main__":
    unittest.main()
