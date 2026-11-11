"""Validate Pause constant-pool removal using the actual C++ patch builder."""
import argparse,hashlib,json,struct,subprocess,tempfile
from pathlib import Path
from smos_bytecode import decode
root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser();parser.add_argument('--probe',type=Path,required=True);args=parser.parse_args()
asset=root/'private/assets/PL_XB/Scripts/frontend.sbn';package=asset.read_bytes();original=package[0x12174:0x12174+3902]
u=lambda b,p:struct.unpack_from('>I',b,p)[0]
def array(b,p):
    assert u(b,p)<=u(b,p+4)
    return [array(b,p+16+i*8+u(b,p+16+i*8)) if u(b,p+20+i*8)==0x88 else u(b,p+16+i*8) for i in range(u(b,p))]
with tempfile.TemporaryDirectory(prefix='pause-menu-',dir=root/'out') as work:
    work=Path(work);source=work/'original.bin';dest=work/'patched.bin';source.write_bytes(original)
    subprocess.run([str(args.probe.resolve()),str(source),str(dest)],check=True)
    patched=dest.read_bytes();assert len(patched)==len(original)
    before=array(original,0x850+u(original,0x850));after=array(patched,0x850+u(patched,0x850))
    for old,new in zip(before[:3],after[:3]):
        index=old[1].index(5);assert old[0][index]==0x0237644B
        assert new[0]==old[0][:index]+old[0][index+1:]
        assert new[1]==old[1][:index]+old[1][index+1:]
        assert 5 not in new[1] and new[1][-1]==7
    assert before[3]==after[3]
    assert patched[:0x84c]==original[:0x84c]
    assert decode(patched[80:80+u(patched,16)])==decode(original[80:80+u(original,16)])
    for i,bad in enumerate((original[:-1],bytes(len(original)),original[:99]+b'\xff'+original[100:])):
        source.write_bytes(bad);badout=work/f'rejected{i}.bin'
        assert subprocess.run([str(args.probe.resolve()),str(source),str(badout)],capture_output=True).returncode!=0
        assert not badout.exists()
    assert asset.read_bytes()==package
    report={'achievements_removed_from_all_three_pause_lists':True,'all_remaining_labels_actions_preserved':True,'exit_action_id':7,'settings_subpane_unchanged':True,'bytecode_and_branches_unchanged':True,'malformed_inputs_rejected':3,'original_asset_unchanged':True,'patched_sha256':hashlib.sha256(patched).hexdigest()}
    (root/'out/pause-menu-patch-tests.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
