"""Check lobby footer/action removal against the private original and VM decoder."""
import hashlib, json, struct, subprocess, tempfile
from pathlib import Path
from smos_bytecode import decode

ROOT=Path(__file__).resolve().parents[1]
asset=ROOT/'private/assets/PL_XB/Scripts/frontend.sbn'
package=asset.read_bytes()
original=package[0xc9d8:0xc9d8+11386]
u32=lambda b,p:struct.unpack_from('>I',b,p)[0]
probe=ROOT/'out/build/win-amd64-release/pc_menu_patch_probe.exe'

def array(data,address):
    count,capacity=u32(data,address),u32(data,address+4)
    assert count<=capacity
    result=[]
    for i in range(count):
        field=address+16+i*8
        value,kind=u32(data,field),u32(data,field+4)
        result.append(array(data,field+value) if kind==0x88 else value)
    return result

with tempfile.TemporaryDirectory(prefix='lobby-menu-test-',dir=ROOT/'out') as temp:
    temp=Path(temp);source=temp/'original.bin';destination=temp/'patched.bin'
    source.write_bytes(original)
    subprocess.run([str(probe),str(source),str(destination)],check=True)
    patched=destination.read_bytes()
    assert len(patched)==len(original)
    before=decode(original[80:80+u32(original,16)])
    after=decode(patched[80:80+u32(patched,16)])
    changes=[(a,b) for a,b in zip(before,after) if a!=b]
    assert len(before)==len(after) and len(changes)==1
    old,new=changes[0]
    assert old['opcode']==new['opcode']==0x52
    assert old['operands']=='0400' and new['operands']=='0000'
    # The sole gamer-card native61 call is guarded by input & this mask.
    action=next(i for i,v in enumerate(after) if v.get('native_index')==61)
    guard=after[action-9:action]
    assert [v['opcode'] for v in guard]==[0x52,0x4c,0x57,0x51,0x48,0x4f,0x14,0x57,0x3a]
    assert guard[6]['target']>after[action]['offset']
    assert all((mask & int(new['operands'],16))==0 for mask in range(65536))
    # Resolve constant-pool entry 14 rather than guessing text/icon addresses.
    entry=u32(original,24)+4+14*8
    footer=entry+u32(original,entry)
    rows=array(original,footer)
    assert [r[2] for r in rows]==[0xa96f5fbe,0x88d78eb4,0x428c46f6] # Select, Back, Gamer Card (TRC_EN).
    assert array(patched,footer)==rows[:2]
    assert {i for i,(a,b) in enumerate(zip(original,patched)) if a!=b}=={old['offset']+81,footer+3,footer+7}
    for i,bad in enumerate((original[:-1],bytes(len(original)),original[:99]+b'\xff'+original[100:])):
        source.write_bytes(bad);badout=temp/f'rejected{i}.bin'
        assert subprocess.run([str(probe),str(source),str(badout)],capture_output=True).returncode!=0
        assert not badout.exists()
    assert asset.read_bytes()==package
    report={'footer_rows_retained':['Select','Back'],'gamer_card_text_and_icon_removed':True,
            'x_action_disabled':True,'original_branches_unchanged':sum('target' in i for i in before),
            'original_assets_unchanged':True,'malformed_scripts_rejected':3,
            'patched_sha256':hashlib.sha256(patched).hexdigest()}
    (ROOT/'out/lobby-menu-patch-tests.json').write_text(json.dumps(report,indent=2))
    print(json.dumps(report,indent=2))
