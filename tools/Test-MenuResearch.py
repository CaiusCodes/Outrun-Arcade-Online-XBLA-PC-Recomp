"""Regression checks for the private, read-only menu analysis tools."""
import importlib.util
import struct
import tempfile
from pathlib import Path
from smos_bytecode import decode

spec = importlib.util.spec_from_file_location('frontend', Path(__file__).with_name('Inspect-Frontend.py'))
frontend = importlib.util.module_from_spec(spec)
spec.loader.exec_module(frontend)


def rejects(action):
    try:
        action()
    except (ValueError, struct.error):
        return
    raise AssertionError('Malformed data was accepted')


assert frontend.smos_hash('main_menu_options') == 0x6B185A6B
assert frontend.smos_hash('menu_manager') == 0x1C39026A
assert frontend.smos_hash('achievements') == 0x40EBD9BB
# Valid forward/backward branches, then a target inside an operand and a truncated push.
assert len(decode(bytes.fromhex('15 00 02 51 01 15 ff f8 00'))) == 4
rejects(lambda: decode(bytes.fromhex('15 00 01 51 01 00')))
rejects(lambda: decode(bytes.fromhex('02 01 02')))
rejects(lambda: decode(bytes.fromhex('fe')))
source = Path('private/assets/PL_XB/Scripts/frontend.sbn')
original = source.read_bytes()
result = frontend.inspect(source)
assert result['script_count'] == 27
assert all(s['branch_targets_verified'] for s in result['scripts'])
with tempfile.TemporaryDirectory(prefix='outrun-menu-test-') as tmp:
    path = Path(tmp) / 'invalid.sbn'
    for offset, value in [(0, b'INVALID!'), (12, b'\x7f\xff\xff\xff'),
                          (0x38, b'\0\0\0\0'), (0x44, b'\x7f\xff\xff\xff')]:
        malformed = bytearray(original)
        malformed[offset:offset+len(value)] = value
        path.write_bytes(malformed)
        rejects(lambda: frontend.inspect(path))
assert source.read_bytes() == original
print('PASS: 27 scripts; hashes, instruction boundaries, branch targets, malformed inputs and original-data preservation.')
