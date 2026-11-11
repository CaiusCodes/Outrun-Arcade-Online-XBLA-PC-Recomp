"""Build a separate GPU cohort replacing only the D3D12 fence-check object."""
import hashlib
import importlib.util
import json
from pathlib import Path
import shlex

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('renderer',ROOT/'tools/Build-RendererOverlay.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
sdk = ROOT/'out/dependencies/rexglue-sdk'
build = sdk/'out/build/win-amd64'
out = ROOT/'out/fence-gpu-20261026'
out.mkdir(parents=True,exist_ok=True)
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
baseline = sdk/'out/win-amd64/Release/rexgpu-xenos.dll'
expected = '9518e318fab580eea64ac8869702a4e66645b1882fb7ab3f6ea808f167751bd6'
if sha(baseline) != expected:
    raise RuntimeError('Expected dual-backend GPU cohort changed')
ninja = (build/'CMakeFiles/impl-Release.ninja').read_text()
relative = 'src/graphics/CMakeFiles/rexgpu-xenos.dir/Release/d3d12/command_processor.cpp.obj'
_, compile_settings = m.edge(ninja,relative+':')
link_edge, settings = m.edge(ninja,': CXX_SHARED_LIBRARY_LINKER__rexgpu-xenos_Release ')
object_names = link_edge.split(': CXX_SHARED_LIBRARY_LINKER__rexgpu-xenos_Release ',1)[1].split(' |',1)[0].split()
objects = [build/p.replace('$:',':') for p in object_names]
libraries = shlex.split(settings['LINK_LIBRARIES'].replace('\\','/'))
inputs = objects+[Path(p) if Path(p).is_absolute() else build/p for p in libraries if not p.startswith('-')]
inventory = [{'path':str(p),'sha256':sha(p)} for p in inputs]
# Freeze identities before compilation, and refuse a changed cohort on reruns.
inventory_file = out/'original-inputs.json'
if inventory_file.exists() and json.loads(inventory_file.read_text()) != inventory:
    raise RuntimeError('GPU link inputs changed since this cohort was reviewed')
inventory_file.write_text(json.dumps(inventory,indent=2)+'\n')
args = []
for name in ('DEFINES','FLAGS','INCLUDES'):
    args += shlex.split(compile_settings[name])
source = sdk/'src/graphics/d3d12/command_processor.cpp'
target = out/'command_processor.cpp.obj'
args += ['-c',str(source),'-o',str(target)]
clang = Path('C:/Program Files/LLVM/bin/clang++.exe')
m.run_rsp(clang,args,'compile-fence',out,build)
old = build/relative
if objects.count(old) != 1:
    raise RuntimeError('Missing/ambiguous D3D12 object')
objects = [target if p == old else p for p in objects]
args = ['-nostartfiles','-nostdlib']
for name in ('LANGUAGE_COMPILE_FLAGS','LINK_FLAGS'):
    args += shlex.split(settings[name].replace('\\','/'))
args += ['-o',str(out/'rexgpu-xenos.dll'),'-Xlinker','/MANIFEST:EMBED',
         '-Xlinker','/implib:'+str(out/'rexgpu-xenos.lib'),
         '-Xlinker','/pdb:'+str(out/'rexgpu-xenos.pdb'),'-Xlinker','/version:0.0']
m.run_rsp(clang,args+[str(p) for p in objects]+libraries,'link-gpu',out,build)
for item in inventory:
    if sha(item['path']) != item['sha256']:
        raise RuntimeError('GPU cohort changed during build')
(out/'build-manifest.json').write_text(json.dumps({
    'baseline_gpu_sha256':expected,'gpu_sha256':sha(out/'rexgpu-xenos.dll'),
    'source_sha256':sha(source),'replacement_object_sha256':sha(target),
    'original_inputs':inventory,'replacement_for':str(old),
    'scope':'Only D3D12 command processor recompiled; existing Vulkan objects retained',
},indent=2)+'\n')
print('Built separate fence GPU:',out)
