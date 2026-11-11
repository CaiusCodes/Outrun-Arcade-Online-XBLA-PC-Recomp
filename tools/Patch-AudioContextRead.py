"""Create a separate compatibility-runtime test DLL; never modify the input."""
import argparse,hashlib,struct
from pathlib import Path

ORIGINAL='9508424882f3eb205d62925a70067be1b5c1be27f6476be89ae266d45e8279c7'
RVA=0x11c5b3
# Verified disassembly: copy decoded.output_buffer_read_offset (old snapshot)
# over the freshly read consumer offset. Other decoder-owned fields are intact.
BEFORE=bytes.fromhex('8b472483e01f8b4c244c83e1e009c1894c244c')
def patch(source,dest):
    if dest.exists():raise ValueError('Output exists; refusing overwrite')
    data=bytearray(source.read_bytes())
    if hashlib.sha256(data).hexdigest()!=ORIGINAL:raise ValueError('Unsupported runtime cohort')
    pe=struct.unpack_from('<I',data,0x3c)[0]
    if data[pe:pe+4]!=b'PE\0\0':raise ValueError('Invalid PE')
    sections=struct.unpack_from('<H',data,pe+6)[0];optional=struct.unpack_from('<H',data,pe+20)[0]
    offset=None
    for i in range(sections):
        at=pe+24+optional+40*i
        virtual_size,virtual,raw_size,raw=struct.unpack_from('<IIII',data,at+8)
        if virtual<=RVA and RVA+len(BEFORE)<=virtual+raw_size:offset=raw+RVA-virtual;break
    if offset is None or data[offset:offset+len(BEFORE)]!=BEFORE:raise ValueError('Instruction signature differs')
    data[offset:offset+len(BEFORE)]=b'\x90'*len(BEFORE)
    dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(data)
    print('Patched only stale consumer-offset assignment:',len(BEFORE),'bytes; SHA256',hashlib.sha256(data).hexdigest())
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('source',type=Path);p.add_argument('output',type=Path);a=p.parse_args();patch(a.source,a.output)
