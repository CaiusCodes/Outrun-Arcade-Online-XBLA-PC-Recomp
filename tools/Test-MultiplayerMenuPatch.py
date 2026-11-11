"""Validate the actual C++ clone with an independent SMOS decoder/private asset."""
import hashlib, json, struct, subprocess, tempfile
from pathlib import Path
from smos_bytecode import decode
ROOT=Path(__file__).resolve().parents[1]
package_path=ROOT/'private/assets/PL_XB/Scripts/frontend.sbn'
package=package_path.read_bytes()
assert hashlib.sha256(package).hexdigest()=='1df321009d8ed8c87f4e448102f52cd2f7b6a8f5092a0ebf783e0f278266a6bf'
original=package[0xf654:0xf654+1748]
u32=lambda b,p:struct.unpack_from('>I',b,p)[0]
edits=[(0x8a,17),(0x9b,5),(0xac,5),(0x39b,25)]
def mapped(p,before=False):
    return p+sum(n for at,n in edits if p>at or (not before and p==at))
probe=ROOT/'out/build/win-amd64-release/pc_menu_patch_probe.exe'
with tempfile.TemporaryDirectory(prefix='multiplayer-menu-test-',dir=ROOT/'out') as temp:
    temp=Path(temp);src=temp/'input.bin';out=temp/'output.bin'
    src.write_bytes(original)
    subprocess.run([str(probe),str(src),str(out)],check=True)
    patched=out.read_bytes()
    assert len(patched)==1800 and u32(patched,16)==u32(original,16)+52
    before=decode(original[80:80+u32(original,16)])
    after=decode(patched[80:80+u32(patched,16)])
    after_map={i['offset']+80:i for i in after}
    branches=0
    for old in before:
        p=old['offset']+80;new=after_map[mapped(p)]
        assert old['opcode']==new['opcode']
        if 'target' in old:
            assert new['target']+80==mapped(old['target']+80,True)
            branches+=1
        elif p in (0x8a,0x9b,0xac):
            assert int(new['operands'][-8:],16)==3
        else:assert old['operands']==new['operands']
    assert branches==43
    for p in (20,24,28,44,52):assert u32(patched,p)==mapped(u32(original,p))
    assert patched[u32(patched,24):u32(patched,44)]==original[u32(original,24):u32(original,44)]
    assert u32(patched,u32(patched,44))==mapped(u32(original,u32(original,44)))
    # Evaluate menu initializer, proving two original tab/heading/description
    # tuples are unchanged and third tab has the existing Settings gear icon.
    def init(data,end):
        stack=[]
        for i in decode(data[0x5a:end]):
            op=i['opcode'];operand=bytes.fromhex(i['operands'])
            if op in (2,0x51):stack.append(int.from_bytes(operand,'big'))
            elif op==0x22:
                n=int.from_bytes(operand[2:],'big');a=stack[-n:];del stack[-n:];stack.append(a)
            else:raise AssertionError(i)
        return stack[-1]
    old_menu=init(original,0xba);menu=init(patched,mapped(0xba))
    assert len(menu)==7 and menu[:4]==old_menu[:4]
    for index in (4,5,6):assert menu[index][:2]==old_menu[index] and len(menu[index])==3
    assert menu[4][2]==[0xfe504320,0x9ca663ab]
    assert menu[5][2]==0xfe504320 and menu[6][2]==0xfe504321
    # Exercise injected dispatch for each tab. It must preserve the caller's
    # stack and either reach the old action handler or skip it entirely.
    for selection in (0,1,2):
        stack=['input-mask','old-selection'];pc=mapped(0x39b,True);calls=[]
        for _ in range(20):
            if pc in (mapped(0x39b),mapped(0x553)):break
            i=after_map[pc];op=i['opcode'];operand=bytes.fromhex(i['operands']);next_pc=pc+i['size']
            if op==0x57:stack.append([selection])
            elif op in (0x51,0x52):stack.append(int.from_bytes(operand,'big'))
            elif op==0x23:index=stack.pop();stack.append(stack.pop()[index])
            elif op==0x4a:b=stack.pop();stack.append(stack.pop()==b)
            elif op==0x14:
                if not stack.pop():next_pc=i['target']+80
            elif op==0x15:next_pc=i['target']+80
            elif op==0x12:calls.append((i['native_index'],stack.pop()));stack.append(0)
            elif op==0x18:stack.pop()
            else:raise AssertionError(i)
            pc=next_pc
        assert stack==['input-mask','old-selection']
        assert calls==([(66,0x7f01)] if selection==2 else [])
        assert pc==mapped(0x553 if selection==2 else 0x39b)
    for i,bad in enumerate((original[:-1],bytes(1748),original[:100]+b'\xff'+original[101:])):
        bad_path=temp/f'bad{i}.bin';bad_out=temp/f'badout{i}.bin';bad_path.write_bytes(bad)
        assert subprocess.run([str(probe),str(bad_path),str(bad_out)],capture_output=True).returncode!=0
        assert not bad_out.exists()
    assert subprocess.run([str(probe),str(src),str(out)],capture_output=True).returncode!=0
    assert out.read_bytes()==patched and package_path.read_bytes()==package
    report={'branches_relocated':branches,'original_tabs_preserved':2,'third_tab_dispatch_checked':True,
            'stack_balance_checked':True,'malformed_inputs_rejected':3,'original_package_unchanged':True,
            'patched_sha256':hashlib.sha256(patched).hexdigest()}
    (ROOT/'out/multiplayer-menu-patch-tests.json').write_text(json.dumps(report,indent=2))
    print(json.dumps(report,indent=2))
