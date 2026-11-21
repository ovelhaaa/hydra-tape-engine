"""Independent integrity check of the committed LUT and its decimal generator."""
from pathlib import Path
import hashlib
import re
import struct
from decimal import Decimal, localcontext

root = Path(__file__).resolve().parents[2]
text = (root / 'dsp/core/fast_tanh_table.inc').read_text()
values = [float.fromhex(v) for v in re.findall(r'^    ([^,]+),$', text, re.M)]
assert len(values) == 1025
bits = b''.join(struct.pack('<f', v) for v in values)
assert hashlib.sha256(bits).hexdigest() == 'd1724b91fed053e18aa3674b7862e8322b9ec9efbddab337fbddc030192238e5'
assert values[0] == -0.9993293285369873 and values[-1] == 0.9993293285369873
assert values[512] == 0 and values[640] == 0.7615941762924194
assert all(a < b for a,b in zip(values, values[1:]))
assert all(a == -b for a,b in zip(values, reversed(values)))
with localcontext() as ctx:
    ctx.prec = 80
    for i, value in enumerate(values):
        x = Decimal(-4) + Decimal(i) / 128
        e = (2*x).exp()
        expected = struct.pack('<f', float((e-1)/(e+1)))
        assert expected == struct.pack('<f', value), i
core = (root / 'dsp/core/tape_core.cpp').read_text()
assert '#include "fast_tanh_table.inc"' in core
assert 'static constexpr int N = kFastTanhTableSize;' in core
assert 'kFastTanhTable[i] + f * (kFastTanhTable[i + 1] - kFastTanhTable[i])' in core
print('PASS: 1025 float32 values, SHA-256, endpoints, symmetry, monotonicity, known points, decimal generation and LUT mapping')
