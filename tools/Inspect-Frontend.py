"""Read-only SMOS package inventory for the user's imported OutRun executable.

Format inferred from original package bytes and PPC loader 0x821DC338.
This is an inventory, not a bytecode decompiler. Unknown fields stay unknown.
No original script data is copied into the report.
"""
import argparse
import hashlib
import json
import struct
from collections import Counter
from pathlib import Path
from smos_bytecode import decode


def smos_hash(text):
    """Matches game hash 0x8214FF50: lookup2 mixing with seed 0x04C11DB7."""
    mask = 0xFFFFFFFF

    def mix(a, b, c):
        a = ((a - b - c) & mask) ^ (c >> 13)
        b = ((b - c - a) & mask) ^ ((a << 8) & mask)
        c = ((c - a - b) & mask) ^ (b >> 13)
        a = ((a - b - c) & mask) ^ (c >> 12)
        b = ((b - c - a) & mask) ^ ((a << 16) & mask)
        c = ((c - a - b) & mask) ^ (b >> 5)
        a = ((a - b - c) & mask) ^ (c >> 3)
        b = ((b - c - a) & mask) ^ ((a << 10) & mask)
        c = ((c - a - b) & mask) ^ (b >> 15)
        return a, b, c

    data = text.encode('utf-8')
    a = b = 0x9E3779B9
    c = 0x04C11DB7
    pos = 0
    while len(data) - pos >= 12:
        x, y, z = struct.unpack_from('<III', data, pos)
        a, b, c = mix((a + x) & mask, (b + y) & mask, (c + z) & mask)
        pos += 12
    tail = data[pos:]
    a = (a + int.from_bytes(tail[:4], 'little')) & mask
    b = (b + int.from_bytes(tail[4:8], 'little')) & mask
    c = (c + len(data) + (int.from_bytes(tail[8:], 'little') << 8)) & mask
    return mix(a, b, c)[2]


def inspect(path, menu_inventory=None):
    data = path.read_bytes()

    def u32(pos):
        if pos < 0 or pos + 4 > len(data):
            raise ValueError(f'Invalid field offset: {pos:#x}')
        return struct.unpack_from('>I', data, pos)[0]

    def relative(pos):
        value = struct.unpack_from('>i', data, pos)[0]
        target = pos + value
        if not 0 <= target < len(data):
            raise ValueError(f'Invalid relative pointer: {pos:#x}')
        return target

    if data[:8] != b'smospkg2':
        raise ValueError('Not a SMOS package version 2')
    table = relative(8)
    count = u32(12)
    if table + count * 32 > len(data):
        raise ValueError('Script table exceeds package')
    texts = {}
    if menu_inventory:
        for asset in json.loads(menu_inventory.read_text(encoding='utf-8')):
            for chunk in asset['chunks']:
                for text in chunk.get('text_entries', []):
                    texts[int(text['id'], 16)] = text['text']
    scripts = []
    for index in range(count):
        row = table + index * 32
        name_at = relative(row + 20)
        name = data[name_at:data.index(0, name_at)].decode('ascii')
        script_hash = u32(row + 16)
        if smos_hash(name) != script_hash:
            raise ValueError(f'Hash mismatch for {name}')
        start = relative(row + 28)
        if data[start:start+8] != b'smosbinB':
            raise ValueError(f'Unsupported script header: {name}')
        size = u32(start + 8)
        code_offset, code_size = u32(start + 12), u32(start + 16)
        if size < 80 or start + size > len(data) or code_offset + code_size > size:
            raise ValueError(f'Invalid script bounds: {name}')
        # The native loader visits the +24 table as count and 8-byte entries.
        link_offset = u32(start + 24)
        link_count = u32(start + link_offset)
        if link_offset + 4 + 8 * link_count > size:
            raise ValueError(f'Invalid link table: {name}')
        references = []
        code = data[start+code_offset:start+code_offset+code_size]
        instructions = decode(code)
        call_counts = Counter(i['native_index'] for i in instructions if 'native_index' in i)
        literals = [(i['offset'], int(i['operands'], 16)) for i in instructions if i['opcode'] == 2]
        for offset, value in literals:
            if value in texts:
                references.append({'script_offset': hex(code_offset + offset),
                                   'text_id': f'0x{value:08X}', 'text': texts[value]})
        scripts.append({'name': name, 'hash': f'0x{script_hash:08X}',
                        'package_offset': hex(start), 'size': size,
                        'code_offset': hex(code_offset), 'code_size': code_size,
                        'link_count': link_count,
                        'instruction_count': len(instructions),
                        'branch_count': sum('target' in i for i in instructions),
                        'branch_targets_verified': True,
                        'native_call_counts': dict(sorted(call_counts.items())),
                        '_literals': literals,
                        'text_references': sorted(references, key=lambda r: int(r['script_offset'], 16))})
    names = {int(s['hash'], 16): s['name'] for s in scripts}
    for script in scripts:
        script['script_name_references'] = sorted({names[value] for _, value in script.pop('_literals') if value in names})
    ordered = sorted(scripts, key=lambda s: int(s['package_offset'], 16))
    for left, right in zip(ordered, ordered[1:]):
        if int(left['package_offset'], 16) + left['size'] > int(right['package_offset'], 16):
            raise ValueError('Overlapping scripts')
    return {'file': str(path), 'sha256': hashlib.sha256(data).hexdigest(),
            'script_count': count, 'all_name_hashes_verified': True, 'scripts': scripts}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, default=Path('private/assets/PL_XB/Scripts/frontend.sbn'))
    parser.add_argument('--menu-inventory', type=Path, default=Path('out/menu-assets-inventory.json'))
    parser.add_argument('--output', type=Path, default=Path('out/frontend-inventory.json'))
    args = parser.parse_args()
    result = inspect(args.input, args.menu_inventory)
    args.output.write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding='utf-8')
    total = sum(s['instruction_count'] for s in result['scripts'])
    branches = sum(s['branch_count'] for s in result['scripts'])
    print(f"Verified {result['script_count']} scripts, {total} instructions and {branches} branch targets; {args.output}")
