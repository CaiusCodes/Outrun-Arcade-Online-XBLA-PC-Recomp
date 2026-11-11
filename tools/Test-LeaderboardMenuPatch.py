"""Independently verify local-first filter cycle and all original bytecode."""
import hashlib,json,struct,subprocess,tempfile
from pathlib import Path
from smos_bytecode import decode
root=Path(__file__).resolve().parents[1]
asset=root/'private/assets/PL_XB/Scripts/frontend.sbn'
package=asset.read_bytes();original=package[0x960:0x960+7562]
u=lambda b,p:struct.unpack_from('>I',b,p)[0]
def labels(b):
    link=u(b,24)+4+13*8;array=link+u(b,link)
    result=[]
    for p in range(array+16,array+16+u(b,array)*8,8):
        text=(p+u(b,p))&0xffffffff
        result.append(b[text:b.index(0,text)].decode())
    return result
with tempfile.TemporaryDirectory(dir=root/'out',prefix='leaderboard-test-') as temp:
    source=Path(temp)/'original.bin';output=Path(temp)/'patched.bin'
    source.write_bytes(original)
    probe=root/'out/build/win-amd64-release/pc_menu_patch_probe.exe'
    subprocess.run([str(probe),str(source),str(output)],check=True)
    patched=output.read_bytes()
    assert len(patched)==len(original)
    before=decode(original[80:80+u(original,16)]);after=decode(patched[80:80+u(patched,16)])
    old_instructions={i['offset']+80:i for i in before}
    instructions={i['offset']+80:i for i in after}
    changed={0x589,0x5d5,0x5b5,0x601,0xc23,0xc30,0xea8,0xeb5,0xe59}
    for p,i in old_instructions.items():
        if p not in changed:assert instructions[p]==i
        if 'target'in i:
            assert not any(a<i['target']+80<b for a,b in [(0x589,0x59f),(0x5d5,0x5eb)])
    # Evaluate the actual footer-building instructions, including arrays and
    # branches, to prove the icon/text descriptor is removed without losing Y.
    def footers(code):
        ops={i['offset']+80:i for i in code};pc=0x55d;stack=[];variables={}
        while pc<0x60b:
            i=ops[pc];op=i['opcode'];args=bytes.fromhex(i['operands']);pc+=i['size']
            if op==2:stack.append(int.from_bytes(args,'big'))
            elif op==0x22:
                n=int.from_bytes(args[1:],'big');values=stack[-n:];del stack[-n:];stack.append(values)
            elif op==0x27:variables[int.from_bytes(args,'big')]=stack.pop()
            elif op==0x15:pc=i['target']+80
            else:raise AssertionError(f'Unexpected executed footer opcode {op:x}')
        assert not stack
        return variables
    previous,current=footers(before),footers(after)
    assert [v[2] for v in previous[12]]==[0xa96f5fbe,0x88d78eb4,0x428c46f6,0x8b979764]
    assert [v[2] for v in previous[13]]==[0x88d78eb4,0x428c46f6,0x8b979764]
    assert current[12]==[previous[12][i] for i in (0,1,3)]
    assert current[13]==[previous[13][i] for i in (0,2)]
    # Both initial setup and Y cycling update the surviving Y descriptor.
    for p,var in [(0xc23,12),(0xc30,13),(0xea8,12),(0xeb5,13)]:
        row=int(instructions[p]['operands'],16)
        assert current[var][row][2]==0x8b979764
    card_calls=[i for i in after if i.get('native_index')==61]
    assert len(card_calls)==1 and card_calls[0]['offset']+80==0xe6b
    assert instructions[0xe59]['opcode']==0x52 and instructions[0xe59]['operands']=='0000'
    assert instructions[0xe5c]['opcode']==0x4c
    assert instructions[0xe5d]['opcode']==0x14 and instructions[0xe5d]['target']+80==0xe77
    assert all((mask & int(instructions[0xe59]['operands'],16))==0 for mask in range(65536))
    assert labels(original)==['LEGEND_FILTER_MINE','LEGEND_FILTER_FRIENDS','LEGEND_FILTER_GLOBAL']
    assert labels(patched)==['LEGEND_FILTER_MINE','LEGEND_FILTER_GLOBAL']
    assert u(patched,0x1cd0)==u(patched,0x1cd4)==2
    instructions={i['offset']+80:i for i in after}
    assert instructions[0xc17]['opcode']==0x51 and instructions[0xc17]['operands']=='00'
    assert instructions[0xc19]['operands']=='0015' # initial filter global/local
    # The original Y handler increments filter then subtracts array size at
    # >= size: with the modified constant only 0/1 can ever be queried.
    assert [instructions[p]['opcode'] for p in (0xe80,0xe82,0xe84,0xe85)]==[0x57,0x51,0x42,0x27]
    assert instructions[0xe91]['operands']=='0d' and instructions[0xe9a]['operands']=='0d'
    state=0;states=[]
    for _ in range(6):
        states.append(state);state+=1
        if state>=len(labels(patched)):state-=len(labels(patched))
    assert states==[0,1,0,1,0,1]
    for i,bad in enumerate((original[:-1],bytes(len(original)),original[:99]+b'\xff'+original[100:])):
        source.write_bytes(bad);badout=Path(temp)/f'bad{i}.bin'
        assert subprocess.run([str(probe),str(source),str(badout)],capture_output=True).returncode!=0
        assert not badout.exists()
    assert asset.read_bytes()==package
    report={'default':'local top scores','filter_cycle':states,'friends_filter_removed':True,
        'gamer_card_removed_from_both_footers':True,'x_action_disabled':True,
        'y_filter_label_updates_preserved':4,
        'original_branches_unchanged':sum('target'in i for i in before),'malformed_scripts_rejected':3,
        'assets_preserved':True,'patched_sha256':hashlib.sha256(patched).hexdigest()}
    (root/'out/leaderboard-menu-patch-tests.json').write_text(json.dumps(report,indent=2))
    print(json.dumps(report,indent=2))
