"""Exercise the actual C++ menu patcher against private data and malformed input.

All copies live in a temporary directory under out; original assets are read only.
The patch gives Help & Options a fifth card, Graphics, whose rows are the PC
settings, and a sixth, Check for Updates; Settings keeps the game's five rows.
"""
import argparse
import hashlib
import importlib.util
import json
import struct
import subprocess
import tempfile
from pathlib import Path
from smos_bytecode import decode

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('frontend', ROOT/'tools/Inspect-Frontend.py')
frontend = importlib.util.module_from_spec(spec)
spec.loader.exec_module(frontend)
original_path = ROOT/'private/assets/PL_XB/Scripts/frontend.sbn'
package = original_path.read_bytes()
assert hashlib.sha256(package).hexdigest() == '1df321009d8ed8c87f4e448102f52cd2f7b6a8f5092a0ebf783e0f278266a6bf'
start, size = 0x395C, 7410
script = package[start:start+size]
u32 = lambda b, p: struct.unpack_from('>I', b, p)[0]
parser = argparse.ArgumentParser()
parser.add_argument('--probe', type=Path, default=ROOT/'out/build/win-amd64-release/pc_menu_patch_probe.exe')
probe = parser.parse_args().probe

# Insertions (script offsets of the original instructions they precede):
# the card pair, heading and description, then the Graphics block before the
# card dispatch. The block's size is what is left of the delta.
CARDS, HEADINGS, DESCRIPTIONS, DISPATCH, AFTER_DISPATCH, EDITOR = 0xA7, 0xC2, 0xDD, 0x590, 0x5F8, 0x607
PAIRS, HEADING_PUSHES, DESCRIPTION_PUSHES, LOAD_MENU, SECTION_LOOKUP = 0x63, 0xAE, 0xC9, 0x592, 0x597
ORDER = [2, None, 3, 1, 0, 'U', 'S']  # shown cards as original positions; None = Graphics, 'U' = Check for Updates, 'S' = Setup
UPDATE_SETTING = 1100  # the setter id the Check for Updates card sends
SETUP_SETTING = 1101  # and the Setup card's
GRAPHICS_SETTINGS = [1024, 1027, 1032, 1033, 1028, 1026, 1025, 1029, 1031]
GRAPHICS_CHOICES = [2, 4, 5, 4, 3, 2, 3, 2, 2]
SETTINGS_ROWS = 0x23F  # op_22 of Settings' five rows; the picture rows go before it
PICTURE_SETTINGS = [1034, 1035, 1036, 1037]  # Vivid Colours, Contrast, Black Level, HUD
PAUSE_ROWS = 0x2F7  # op_22 of the in-race Settings' three rows (pause menu, global 8); HUD goes before it


def initializer(code, stop):
    """Evaluate a straight-line initializer: pushes, pops and op_22 arrays."""
    stack = []
    for instruction in code:
        if instruction is stop:
            break
        op = instruction['opcode']
        operand = bytes.fromhex(instruction['operands'])
        if op in (2, 0x51, 0x52):
            stack.append(int.from_bytes(operand, 'big', signed=op != 2))
        elif op == 0x18:
            stack.pop()
        elif op == 0x22:
            assert operand[:2] == b'\x08\x00'
            count = int.from_bytes(operand[2:], 'big')
            values = stack[-count:]
            del stack[-count:]
            stack.append(values)
        else:
            raise AssertionError(f'Unexpected initializer opcode {op:#x}')
    return stack


with tempfile.TemporaryDirectory(prefix='pc-menu-test-', dir=ROOT/'out') as directory:
    directory = Path(directory)
    source, destination = directory/'original.bin', directory/'patched.bin'
    source.write_bytes(script)
    subprocess.run([str(probe), str(source), str(destination)], check=True)
    patched = destination.read_bytes()
    delta = len(patched)-len(script)
    assert delta % 4 == 0
    # (at, inserted bytes, replaced bytes); the block is what is left of the delta.
    edits = [(PAIRS, 17*len(ORDER), 68), (HEADING_PUSHES, 5*len(ORDER), 20), (DESCRIPTION_PUSHES, 5*len(ORDER), 20), (LOAD_MENU, 2*len(ORDER)+7, 0),
             (SECTION_LOOKUP, 1, 0), (SETTINGS_ROWS, 3*(26+15)+(26+20), 0), (PAUSE_ROWS, 26+20, 0)]
    edits.append((DISPATCH, delta-sum(n-r for _, n, r in edits), 0))

    def replaced(offset):
        return any(at <= offset < at+r for at, _, r in edits)

    def moved(offset, land=False):
        return offset+sum(n-r for at, n, r in edits if at < offset or (not land and not r and at == offset))

    before = decode(script[80:80+u32(script, 16)])
    after = decode(patched[80:80+u32(patched, 16)])
    after_map = {i['offset']+80: i for i in after}
    branch_count = call_count = 0
    for old in before:
        position = old['offset']+80
        if replaced(position):
            continue
        new = after_map[moved(position)]
        assert new['opcode'] == old['opcode']
        if 'target' in old:
            assert new['target']+80 == moved(old['target']+80, True), hex(position)
            branch_count += 1
        elif old['opcode'] == 0x34:
            operand_old, operand_new = bytes.fromhex(old['operands']), bytes.fromhex(new['operands'])
            target = position+old['size']+int.from_bytes(operand_old[1:5], 'big', signed=True)
            assert moved(position)+new['size']+int.from_bytes(operand_new[1:5], 'big', signed=True) == moved(target, True)
            assert operand_new[5:] == operand_old[5:]
            call_count += 1
        elif position in (CARDS, HEADINGS, DESCRIPTIONS):
            assert old['operands'] == '080000000004' and new['operands'] == '%012x' % (0x080000000000+len(ORDER))
        elif position == SETTINGS_ROWS:
            assert old['operands'] == '080000000005' and new['operands'] == '080000000009'
        elif position == PAUSE_ROWS:
            assert old['operands'] == '080000000003' and new['operands'] == '080000000004'
        else:
            assert new['operands'] == old['operands'], hex(position)
    # Six cards: [Graphics, the Settings gear] and [Check for Updates, How to
    # play's icon]; the game's five Settings rows are untouched.
    menu = initializer([i for i in after if moved(0x55) <= i['offset']+80 < moved(0xEB)], None)[-1]
    original_menu = initializer([i for i in before if 0x55 <= i['offset']+80 < 0xEB], None)[-1]
    graphics = ([0xFE504316, 0x9CA663AB], 0xFE504316, 0xFE504317)
    update = ([0xFE504318, 0xFA1C6ADA], 0xFE504318, 0xFE504319)
    setup = ([0xFE50431A, 0x9CA663AB], 0xFE50431A, 0xFE50431E)
    for field, extra, extra_update, extra_setup in zip((4, 5, 6), graphics, update, setup):
        assert menu[field] == [extra if o is None else extra_update if o == 'U' else extra_setup if o == 'S' else original_menu[field][o]
                               for o in ORDER], field
    assert menu[:4] == original_menu[:4]
    # The section lookup maps the shown card back to its original position.
    lookup = [i for i in after if moved(LOAD_MENU, True) <= i['offset']+80 < moved(LOAD_MENU)]
    assert initializer(lookup, None)[-1] == [o if isinstance(o, int) else 2 for o in ORDER]
    extra = after_map[moved(SECTION_LOOKUP, True)]
    assert extra['opcode'] == 0x23 and after_map[moved(SECTION_LOOKUP)]['opcode'] == 0x23
    settings_old = initializer([i for i in before if 0x181 <= i['offset']+80 < 0x24D], None)
    settings_new = initializer([i for i in after if moved(0x181) <= i['offset']+80 < moved(0x24D)], None)
    assert settings_new[0][:2] == settings_old[0][:2] and settings_new[0][2][:5] == settings_old[0][2]
    assert [row[1] for row in settings_new[0][2][5:]] == PICTURE_SETTINGS and [len(row[4]) for row in settings_new[0][2][5:]] == [3, 3, 3, 4]
    # The in-race Settings (pause menu, global 8): the game's three rows, then HUD.
    pause_old = initializer([i for i in before if 0x293 <= i['offset']+80 < 0x305], None)
    pause_new = initializer([i for i in after if moved(0x293) <= i['offset']+80 < moved(0x305)], None)
    assert pause_new[0][:2] == pause_old[0][:2] and pause_new[0][2][:3] == pause_old[0][2]
    assert [row[1] for row in pause_new[0][2][3:]] == [1037] and len(pause_new[0][2][3][4]) == 4
    # The Graphics block: card index 4 (global 1, field 0), the pane in
    # global 9, the shared editor, then past the original dispatch.
    block = [i for i in after if moved(DISPATCH, True) <= i['offset']+80 < moved(DISPATCH)]
    assert [i['name'] for i in block[:6]] == ['op_57', 'push_i8', 'op_23', 'push_i8', 'op_4a', 'branch_if_false']
    assert block[3]['operands'] == '%02x' % ORDER.index(None)
    graphics_exit = next(n for n, i in enumerate(block) if i['name'] == 'branch')
    update_block, block = block[graphics_exit+1:], block[:graphics_exit+1]
    assert block[5]['target'] == update_block[0]['offset']
    rows_end = next(i for i in block if i['opcode'] == 0x27)
    pane = initializer(block[6:], rows_end)[-1]
    assert pane[:2] == [0x21836792, 0x6D5DA8A8] and len(pane[2]) == len(GRAPHICS_SETTINGS)
    assert [row[1] for row in pane[2]] == GRAPHICS_SETTINGS
    assert [len(row[4]) for row in pane[2]] == GRAPHICS_CHOICES
    call = next(i for i in block if i['opcode'] == 0x34)
    assert call['offset']+80+call['size']+int.from_bytes(bytes.fromhex(call['operands'])[1:5], 'big', signed=True) == moved(EDITOR, True)
    assert block[-1]['name'] == 'branch' and block[-1]['target']+80 == moved(AFTER_DISPATCH, True)
    # The Check for Updates block: its card, then native 64 (setter) with the
    # port's id, value 0, commit 0; then past the original dispatch.
    # Then the Setup block, the same with its own card and id; the update
    # block's branch-if-false lands on it, and its own on the original dispatch.
    setup_block, update_block = update_block[12:], update_block[:12]
    for card_block, card, setting, miss in ((update_block, 'U', UPDATE_SETTING, setup_block[0]['offset']+80),
                                            (setup_block, 'S', SETUP_SETTING, moved(DISPATCH))):
        assert [i['opcode'] for i in card_block] == [0x57, 0x51, 0x23, 0x51, 0x4A, 0x14, 0x52, 0x51, 0x51, 0x32, 0x18, 0x15]
        assert card_block[3]['operands'] == '%02x' % ORDER.index(card)
        assert card_block[5]['target']+80 == miss
        assert int(card_block[6]['operands'], 16) == setting
        assert card_block[9]['native_index'] == 64 and card_block[9]['operands'][:2] == '03'
        assert card_block[-1]['target']+80 == moved(AFTER_DISPATCH, True)
    # Reassemble a private package solely to run the whole-package validator.
    expanded = bytearray(package[:start]+patched+package[start+size:])
    for index in range(27):
        field = 0x28+index*32+28
        target = field+struct.unpack_from('>i', package, field)[0]
        if target > start:
            struct.pack_into('>i', expanded, field, target+delta-field)
    combined = directory/'frontend.sbn'
    combined.write_bytes(expanded)
    checked = frontend.inspect(combined)
    assert checked['script_count'] == 27
    for entry in checked['scripts']:
        if entry['name'] != 'main_menu_options':
            new_start = int(entry['package_offset'], 16)
            old_start = new_start-(delta if new_start > start else 0)
            assert expanded[new_start:new_start+entry['size']] == package[old_start:old_start+entry['size']]
    # The tables after the code are unchanged but for the debug table's entries (moved with them).
    assert patched[u32(patched, 20):u32(patched, 44)] == script[u32(script, 20):u32(script, 44)]
    debug_end = 4*u32(script, 48)
    assert patched[u32(patched, 44)+debug_end:] == script[u32(script, 44)+debug_end:]
    assert u32(patched, u32(patched, 44)) == u32(script, u32(script, 44))+delta
    for index, malformed in enumerate([script[:-1], bytes(size), script[:100]+b'\xff'+script[101:]]):
        bad, rejected = directory/f'bad-{index}.bin', directory/f'rejected-{index}.bin'
        bad.write_bytes(malformed)
        run = subprocess.run([str(probe), str(bad), str(rejected)], capture_output=True)
        assert run.returncode != 0 and not rejected.exists()
    run = subprocess.run([str(probe), str(source), str(destination)], capture_output=True)
    assert run.returncode != 0 and destination.read_bytes() == patched
    inventory = json.loads((ROOT/'out/menu-assets-inventory.json').read_text())
    existing = {int(t['id'], 16) for a in inventory for c in a['chunks'] for t in c.get('text_entries', [])}
    assert not (existing & (set(range(0xFE504300, 0xFE50431F)) | set(range(0xFE504330, 0xFE504341))))
    report = {'settings_rows_preserved': 5, 'pause_settings_rows': len(pause_new[0][2]), 'graphics_rows': len(pane[2]), 'cards': len(ORDER), 'update_card': ORDER.index('U'), 'setup_card': ORDER.index('S'), 'delta': delta,
              'relative_branches_verified': branch_count, 'relative_local_calls_verified': call_count,
              'other_scripts_unchanged': 26, 'malformed_inputs_rejected': 3,
              'original_assets_unchanged': original_path.read_bytes() == package,
              'patched_script_sha256': hashlib.sha256(patched).hexdigest()}
    (ROOT/'out/pc-menu-patch-tests.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report, indent=2))
