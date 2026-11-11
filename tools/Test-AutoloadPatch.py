"""Verify first-run creation and successful-load notice skips preserve saving/errors."""
import hashlib,json,struct,subprocess,tempfile
from pathlib import Path
from smos_bytecode import decode
root=Path(__file__).resolve().parents[1]
asset=root/'private/assets/PL_XB/Scripts/frontend.sbn'
package=asset.read_bytes(); original=package[0x5650:0x5650+4667]
import argparse
parser=argparse.ArgumentParser()
parser.add_argument('--probe',type=Path,default=root/'out/build/win-amd64-release/pc_menu_patch_probe.exe')
probe=parser.parse_args().probe.resolve()
code_size=struct.unpack_from('>I',original,16)[0]
with tempfile.TemporaryDirectory(dir=root/'out',prefix='autoload-test-') as temp:
    source=Path(temp)/'original.bin';destination=Path(temp)/'patched.bin'
    source.write_bytes(original)
    subprocess.run([str(probe),str(source),str(destination)],check=True)
    patched=destination.read_bytes()
    before=decode(original[80:80+code_size]);after=decode(patched[80:80+code_size])
    old={i['offset']+80:i for i in before};new={i['offset']+80:i for i in after}
    assert len(patched)==len(original)
    changed=lambda i:0xD5A<=i<0xD5F or 0xEFA<=i<0xF09 or 0xB9B<=i<0xBAA
    assert all(a==b for i,(a,b) in enumerate(zip(original,patched)) if not changed(i))
    assert old[0xD5A]['opcode']==2 and old[0xD5A]['operands']=='3df892df'
    assert old[0xD5F]['opcode']==0x34 and old[0xD5F]['operands'].startswith('01')
    assert old[0xD69]['opcode']==0x18
    assert new[0xD5A]['opcode']==0x15 and new[0xD5A]['target']+80==0xD6A
    assert [new[p]['native_index'] for p in (0xD6A,0xD73,0xD7C)]==[274,278,301]
    for p,i in old.items():
        if p not in (0xD5A,0xEFA,0xEFF,0xB9B,0xBA0):assert new[p]==i
        if 'target' in i:assert not 0xD5A<i['target']+80<0xD6A
    # The no-data prompt returns zero, which the existing code routes to
    # CreateData (0xC7C9EFDB), without taking storage selection or no-save paths.
    assert old[0xEFA]['opcode']==2 and old[0xEFA]['operands']=='89a7376f'
    assert old[0xEFF]['opcode']==0x34 and old[0xEFF]['operands'].startswith('01')
    assert new[0xEFA]['opcode']==2 and new[0xEFA]['operands']=='00000000'
    assert new[0xEFF]['opcode']==0x15 and new[0xEFF]['target']+80==0xF09
    assert old[0xF0D]['operands']=='00' and old[0xF10]['target']+80==0xF1A
    assert old[0xF13]['operands']=='c7c9efdb' and old[0xF18]['operands']=='04'
    assert old[0xB9B]['operands']=='89a7376f' and new[0xB9B]['operands']=='00000000'
    assert new[0xBA0]['opcode']==0x15 and new[0xBA0]['target']+80==0xBAA
    assert old[0xBB4]['operands']=='c7c9efdb' and old[0xBB9]['operands']=='04'
    preserved=Path(temp)/'preserved.bin'
    subprocess.run([str(probe),str(source),str(preserved),'preserve-saves'],check=True)
    safe=preserved.read_bytes()
    assert safe[0xB9B:0xBAA]==original[0xB9B:0xBAA]
    assert safe[0xEFA:0xF09]==original[0xEFA:0xF09]
    assert all(a==b for i,(a,b) in enumerate(zip(original,safe)) if not 0xD5A<=i<0xD5F)
    # Other dialog calls stay byte-identical. Existing-content mode retains
    # both no-data recovery dialogs too. No old target enters new padding.
    calls=[i for i in before if i['opcode']==0x34]
    for i in calls:
        target=i['offset']+i['size']+int.from_bytes(bytes.fromhex(i['operands'])[1:5],'big',signed=True)+80
        assert target in old and not 0xD5A<target<0xD6A
        assert not 0xEFF<target<0xF09
        assert not 0xBA0<target<0xBAA
        if i['offset']+80 not in (0xEFF,0xBA0):assert new[i['offset']+80]==i
    for j,bad in enumerate((original[:-1],bytes(len(original)),original[:90]+b'\xff'+original[91:])):
        source.write_bytes(bad);out=Path(temp)/f'bad{j}.bin'
        assert subprocess.run([str(probe),str(source),str(out)],capture_output=True).returncode!=0
        assert not out.exists()
    assert asset.read_bytes()==package
    report={'notice_skipped':True,'first_run_choice_zero_create_data':True,'existing_save_recovery_dialogs_retained':True,'save_enable_natives_retained':[274,278,301],
            'original_branches_unchanged':sum('target'in i for i in before),
            'local_calls_unchanged':len(calls)-2,'first_run_dialog_calls_removed':2,'malformed_scripts_rejected':3,
            'assets_preserved':True,'patched_sha256':hashlib.sha256(patched).hexdigest()}
    (root/'out/autoload-patch-tests.json').write_text(json.dumps(report,indent=2))
    print(json.dumps(report,indent=2))
