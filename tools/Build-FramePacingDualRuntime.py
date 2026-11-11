"""Link dual-renderer UI with the original compatibility kernel/audio/network objects.
Only UI, XAM UI dialog vtables and instrumented VdSwap use the rebuilt SDK objects.
This avoids silently replacing unrelated compatibility fixes during a Vulkan build.
"""
from pathlib import Path
import hashlib, importlib.util, json, shlex, subprocess
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('renderer',ROOT/'tools/Build-RendererOverlay.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
original=ROOT.parent/'rexglue-sdk-0.9.0/out/build/win-amd64'
dual=ROOT/'out/dependencies/rexglue-sdk/out/build/win-amd64'
out=ROOT/'out/frame-pacing/dual';out.mkdir(parents=True,exist_ok=True)
header,settings=m.edge((dual/'CMakeFiles/impl-Release.ninja').read_text(),': CXX_SHARED_LIBRARY_LINKER__rexruntime_Release ')
explicit=header.split(': CXX_SHARED_LIBRARY_LINKER__rexruntime_Release ',1)[1].split(' |',1)[0]
objects=[]; inputs=[]
for name in explicit.split():
    old=original/name.replace('$:',':')
    rebuild=name.startswith('src/ui/') or name.endswith('/xam_ui.cpp.obj') or name.endswith('/xboxkrnl_video.cpp.obj')
    path=dual/name.replace('$:',':') if rebuild or not old.is_file() else old
    if not path.is_file():raise RuntimeError('Missing object '+str(path))
    objects.append(str(path))
    inputs.append({'path':str(path),'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'rebuilt':rebuild})
args=['-nostartfiles','-nostdlib']
objlist=out/'exports.objs'; objlist.write_text('\n'.join(objects)+'\n')
subprocess.run(['cmake','-E','__create_def',str(out/'exports.def'),str(objlist),'--nm=C:/Program Files/LLVM/bin/llvm-nm.exe'],check=True,capture_output=True)
for key in ('LANGUAGE_COMPILE_FLAGS','LINK_FLAGS'):
    for arg in shlex.split(settings[key].replace('\\','/')):
        args.append('/DEF:'+str(out/'exports.def') if arg.startswith('/DEF:') else arg)
args+=['-o',str(out/'rexruntime.dll'),'-Xlinker','/MANIFEST:EMBED','-Xlinker','/implib:'+str(out/'rexruntime.lib'),'-Xlinker','/pdb:'+str(out/'rexruntime.pdb'),'-Xlinker','/version:0.0']
args+=objects+shlex.split(settings['LINK_LIBRARIES'].replace('\\','/'))
m.run_rsp(Path('C:/Program Files/LLVM/bin/clang++.exe'),args,'link-runtime',out,dual)
gpu=ROOT/'out/sdk-frame-pacing-install/bin/rexgpu-xenos.dll'
(out/gpu.name).write_bytes(gpu.read_bytes())
(out/'build-manifest.json').write_text(json.dumps({'inputs':inputs,'outputs':{name:hashlib.sha256((out/name).read_bytes()).hexdigest() for name in ['rexruntime.dll','rexgpu-xenos.dll']}},indent=2))
print('Built dual-renderer compatibility runtime:',out)
