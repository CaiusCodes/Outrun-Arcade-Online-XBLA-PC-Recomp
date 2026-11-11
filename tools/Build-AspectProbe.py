"""Build an isolated 4:3 classification probe from the tested spatial runtime.

Only replaces the video-query object; no SDK source or playable copy is edited.
"""
import hashlib
import importlib.util
import json
import shlex
import subprocess
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
sdk=ROOT/'out/dependencies/rexglue-sdk'
build=sdk/'out/build/win-amd64'
parent=ROOT/'out/spatial-filters-20261028'
out=ROOT/'out/aspect-probe-20261029'
out.mkdir(parents=True,exist_ok=True)
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
manifest=json.loads((parent/'build-manifest.json').read_text())
assert sha(parent/'rexruntime.dll')==manifest['runtime_sha256']
spec=importlib.util.spec_from_file_location('renderer',ROOT/'tools/Build-RendererOverlay.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
source=sdk/'src/kernel/xboxkrnl/xboxkrnl_video.cpp'
original=source.read_text()
old='display_width * 3 >= display_height * 4'
assert original.count(old)==1 or 'uint64_t(display_width) * 3 > uint64_t(display_height) * 4' in original
patched=out/'xboxkrnl_video.cpp'
patched.write_text(original.replace(old,'uint64_t(display_width) * 3 > uint64_t(display_height) * 4'))
relative='src/system/CMakeFiles/rexruntime.dir/Release/__/kernel/xboxkrnl/xboxkrnl_video.cpp.obj'
_,fields=m.edge((build/'CMakeFiles/impl-Release.ninja').read_text(),relative+':')
args=[]
for field in ('DEFINES','FLAGS','INCLUDES'):args+=shlex.split(fields[field])
obj=out/'xboxkrnl_video.cpp.obj'
args+=['-I'+str(source.parent),'-c',str(patched),'-o',str(obj)]
clang=Path('C:/Program Files/LLVM/bin/clang++.exe')
m.run_rsp(clang,args,'compile-video',out,build)
args=shlex.split((parent/'link-rexruntime.rsp').read_text())
old_objs=[a for a in args if a.replace('\\','/').endswith('/'+relative)]
assert len(old_objs)==1
objects=(parent/'exports.objs').read_text().splitlines()
assert sum(p.replace('\\','/')==old_objs[0].replace('\\','/') for p in objects)==1
objects=[str(obj) if p.replace('\\','/')==old_objs[0].replace('\\','/') else p for p in objects]
(out/'exports.objs').write_text('\n'.join(objects)+'\n')
subprocess.run(['cmake','-E','__create_def',str(out/'exports.def'),str(out/'exports.objs'),
                '--nm=C:/Program Files/LLVM/bin/llvm-nm.exe'],check=True,capture_output=True)
for i,a in enumerate(args):
    if a==old_objs[0]:args[i]=str(obj)
    elif a.replace('\\','/').endswith(('/rexruntime.dll','/rexruntime.lib','/rexruntime.pdb','/exports.def')):
        args[i]=a.replace('\\','/').replace(parent.as_posix(),out.as_posix())
m.run_rsp(clang,args,'link-runtime',out,build)
(out/'build-manifest.json').write_text(json.dumps({
    'parent_sha256':manifest['runtime_sha256'],'runtime_sha256':sha(out/'rexruntime.dll'),
    'original_video_source_sha256':sha(source),'probe_source_sha256':sha(patched),
    'replaced_object':old_objs[0],'replaced_object_sha256':sha(old_objs[0]),
    'inputs':[{'path':p,'sha256':sha(p)} for p in objects],
    'change':'Exact 4:3 is not widescreen; dimensions and refresh-rate logic unchanged.'
},indent=2)+'\n')
print('Built isolated aspect probe:',out)
