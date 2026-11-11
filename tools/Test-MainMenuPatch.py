"""Execute original/patched menu initialization and compare the actual arrays."""
import argparse, hashlib, json, struct, subprocess, tempfile
from pathlib import Path
from smos_bytecode import decode
root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser();parser.add_argument('--probe',type=Path,required=True);args=parser.parse_args()
asset=root/'private/assets/PL_XB/Scripts/frontend.sbn';package=asset.read_bytes()
original=package[0x2EAC:0x2EAC+2734]
u=lambda b,p:struct.unpack_from('>I',b,p)[0]
def initialize(script):
    instructions={i['offset']+80:i for i in decode(script[80:80+u(script,16)])}
    stack=[];saved={};pc=80
    while pc<0x1E1:
        i=instructions[pc];op=i['opcode'];data=bytes.fromhex(i['operands']);pc+=i['size']
        if op==2:stack.append(int.from_bytes(data,'big'))
        elif op==0x51:stack.append(int.from_bytes(data,'big',signed=True))
        elif op==0x22:
            count=int.from_bytes(data[2:],'big');values=stack[-count:];del stack[-count:];stack.append(values)
        elif op==0x27:saved[int.from_bytes(data,'big')]=stack.pop()
        elif op==0x15:pc=i['target']+80
        elif op==0x18:stack.pop()
        else:raise AssertionError(i)
    assert not stack
    return saved
with tempfile.TemporaryDirectory(prefix='main-menu-',dir=root/'out') as directory:
    directory=Path(directory);source=directory/'original.bin';dest=directory/'patched.bin';source.write_bytes(original)
    subprocess.run([str(args.probe.resolve()),str(source),str(dest)],check=True)
    patched=dest.read_bytes();assert len(patched)==len(original)
    before=initialize(original);after=initialize(patched)
    assert set(before)==set(after)=={0,1}
    for variant in before:
        old,new=before[variant],after[variant];assert len(old)==len(new)==7
        assert old[:4]==new[:4]
        for index in (4,5,6):assert new[index]==old[index][:3]+old[index][4:]
        assert old[4][3][0]==0x29D3B70F and old[5][3]==0xC914F3FE and old[6][3]==0x61C21ACA
        assert new[4][3][0]==0x16FD6B27 and new[5][3]==0xF4CC5F54 and new[6][3]==0x2B8CE70D
    # All navigation, dispatch, yield and input code after initialization is exact.
    assert patched[0x1DE:0x890]==original[0x1DE:0x890]
    # The footer keeps both rows: Select and B (Return to Game Library, shown as Exit Game).
    assert u(patched,0x9B0)==u(patched,0x9B4)==2 and patched[0x9B0:0xA20]==original[0x9B0:0xA20]
    for rows in (0x8C0,0x900,0x940,0x978):
        count=u(original,rows);assert u(patched,rows)==u(patched,rows+4)==count-1
        old=[original[rows+16+i*8:rows+24+i*8] for i in range(count)]
        new=[patched[rows+16+i*8:rows+24+i*8] for i in range(count-1)]
        assert new==old[:3]+old[4:]
    assert u(patched,0x8C0+16+3*8)==4 and u(patched,0x940+16+3*8)==4
    assert u(patched,0x900+16+3*8)==0x6B185A6B and u(patched,0x978+16+3*8)==0x6B185A6B
    old_branches=[(i['offset'],i['target']) for i in decode(original[80:2192]) if 'target' in i]
    new_branches=[(i['offset'],i['target']) for i in decode(patched[80:2192]) if 'target' in i]
    assert all(branch in new_branches for branch in old_branches)
    for n,bad in enumerate((original[:-1],bytes(len(original)),original[:99]+b'\xff'+original[100:])):
        source.write_bytes(bad);rejected=directory/f'rejected{n}.bin'
        assert subprocess.run([str(args.probe.resolve()),str(source),str(rejected)],capture_output=True).returncode!=0
        assert not rejected.exists()
    assert asset.read_bytes()==package
    report={'both_variants_achievements_removed':True,'parallel_arrays_match':True,'help_options_retained':True,
            'remaining_entries_unchanged':True,'footer_exit_row_kept':True,'navigation_dispatch_code_unchanged':True,'action_ids_and_scripts_preserved':True,'original_branches_retained':True,
            'malformed_inputs_rejected':3,'original_asset_unchanged':True,'sha256':hashlib.sha256(patched).hexdigest()}
    (root/'out/main-menu-patch-tests.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
