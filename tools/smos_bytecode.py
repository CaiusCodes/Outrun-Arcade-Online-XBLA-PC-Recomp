"""Instruction boundaries recovered from OutRun VM 0x8218BF20.

Only name operations whose behavior has been inspected. This does not execute
bytecode, resolve native function indices, or claim a full decompilation.
"""
import struct

WIDTHS = {
    0x00: 0, 0x01: 4, 0x02: 4, 0x03: 4, 0x05: 2, 0x06: 4,
    0x07: 1, 0x08: 2, 0x09: 2, 0x0A: 4, 0x0B: 1, 0x0C: 2,
    0x0D: 1, 0x0E: 4, 0x0F: 1, 0x10: 2, 0x11: 0, 0x12: 3,
    0x13: 2, 0x14: 2, 0x15: 2, 0x16: 1, 0x17: 0, 0x18: 0,
    0x19: 1, 0x1A: 5, 0x1B: 0, 0x1C: 5, 0x1D: 0, 0x1E: 0,
    0x1F: 0, 0x20: 0, 0x21: 0, 0x22: 6, 0x23: 0, 0x24: 3,
    0x25: 0, 0x26: 4, 0x27: 2, 0x28: 3, 0x29: 2, 0x2A: 0,
    0x2C: 2, 0x2D: 4, 0x2E: 5, 0x2F: 4, 0x30: 0, 0x31: 0,
    0x32: 7, 0x33: 4, 0x34: 9, 0x35: 9,
    **{op: 0 for op in range(0x36, 0x51)},
    0x51: 1, 0x52: 2, 0x53: 1, 0x54: 2, 0x55: 1, 0x56: 2,
    0x57: 1, 0x58: 1, 0x59: 0, 0x5A: 0, 0x5B: 1, 0x5C: 5, 0x5D: 0,
}
NAMES = {0: 'end', 1: 'push_i32', 2: 'push_u32', 3: 'push_float',
         4: 'push_string', 0x13: 'branch_if_true', 0x14: 'branch_if_false',
         0x15: 'branch', 0x12: 'native_call', 0x32: 'native_call_debug',
         0x51: 'push_i8', 0x52: 'push_i16', 0x53: 'push_u8',
         0x54: 'push_u16', 0x55: 'push_uint8', 0x56: 'push_uint16'}


def decode(code):
    result = []
    offset = 0
    while offset < len(code):
        start = offset
        op = code[offset]
        offset += 1
        if op in (4, 0x2B):
            offset = code.index(0, offset) + 1
        elif op in WIDTHS:
            offset += WIDTHS[op]
        else:
            raise ValueError(f'Unknown opcode {op:#x} at {start:#x}')
        if offset > len(code):
            raise ValueError(f'Truncated instruction at {start:#x}')
        operands = code[start+1:offset]
        entry = {'offset': start, 'opcode': op, 'name': NAMES.get(op, f'op_{op:02x}'),
                 'operands': operands.hex(), 'size': offset-start}
        if op in (0x13, 0x14, 0x15):
            entry['target'] = offset + struct.unpack('>h', operands)[0]
        if op in (0x12, 0x32):
            entry['argument_count'] = operands[0]
            entry['native_index'] = int.from_bytes(operands[1:3], 'big')
        result.append(entry)
    boundaries = {item['offset'] for item in result}
    for item in result:
        if 'target' in item and item['target'] not in boundaries:
            raise ValueError(f"Branch at {item['offset']:#x} misses an instruction: {item['target']:#x}")
    return result
