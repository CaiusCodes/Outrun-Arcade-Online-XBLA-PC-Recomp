"""Resolve SMOS native indices against private sections from menu_image_probe.

Uses the dispatch layout verified in original function 0x82191D18. Does not
modify a game image or execute any guest function.
"""
import argparse
import csv
import json
import struct
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--image-dir', type=Path, required=True)
parser.add_argument('--inventory', type=Path, default=Path('out/frontend-inventory.json'))
parser.add_argument('--output', type=Path, default=Path('out/menu-native-functions.json'))
args = parser.parse_args()
sections = []
for section in csv.DictReader((args.image_dir / 'sections.tsv').read_text().splitlines(), delimiter='\t'):
    contents = (args.image_dir / section['file']).read_bytes()
    if len(contents) != int(section['size'], 16):
        raise ValueError('Incomplete section dump')
    sections.append((int(section['address'], 16), contents, section['executable'] == '1'))


def read_word(address):
    for base, contents, _ in sections:
        if base <= address and address + 4 <= base + len(contents):
            return struct.unpack_from('>I', contents, address - base)[0]
    raise ValueError(f'Address not in dumped sections: {address:#x}')


if read_word(0x820C5808) != 0x82191D18:
    raise ValueError('Unexpected VM vtable; this tool is specific to the verified OutRun image')
inventory = json.loads(args.inventory.read_text(encoding='utf-8'))
indices = sorted({int(index) for script in inventory['scripts'] for index in script['native_call_counts']})
functions = []
for index in indices:
    group, member = divmod(index, 256)
    table = read_word(0x825C5954 + group * 8)
    count = read_word(read_word(0x825C5958 + group * 8))
    if member >= count:
        raise ValueError(f'Native {index} is outside its registered group')
    function = read_word(table + member * 4)
    if not any(executable and base <= function < base + len(data) for base, data, executable in sections):
        raise ValueError(f'Native {index} does not resolve to executable code')
    functions.append({'index': index, 'function': f'0x{function:08X}',
                      'scripts': [s['name'] for s in inventory['scripts'] if str(index) in s['native_call_counts']]})
args.output.write_text(json.dumps({'dispatcher': '0x82191D18', 'functions': functions}, indent=2), encoding='utf-8')
print(f'PASS: resolved {len(functions)} native indices to executable functions; {args.output}')
