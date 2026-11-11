"""Replace only the timer queue object in the confirmed clock/controller runtime."""
import hashlib
import importlib.util
import json
from pathlib import Path
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('renderer', ROOT/'tools/Build-RendererOverlay.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
sdk = ROOT/'out/dependencies/rexglue-sdk'
build = sdk/'out/build/win-amd64'
parent = ROOT/'out/clock-runtime-20261026'
out = ROOT/'out/timer-runtime-20261026'
out.mkdir(parents=True, exist_ok=True)
manifest = json.loads((parent/'build-manifest.json').read_text())
sha = lambda path: hashlib.sha256(Path(path).read_bytes()).hexdigest()
for entry in manifest['inputs'] + manifest['libraries']:
    if sha(entry['path']) != entry['sha256']:
        raise RuntimeError('Frozen input changed: '+entry['path'])
decoder = ROOT/'out/audio-reset-runtime-20261019/wmaprodec.c.obj'
if sha(decoder) != manifest['decoder_object_sha256']:
    raise RuntimeError('Confirmed audio decoder object changed')
if sha(parent/'rexruntime.dll') != manifest['runtime_sha256']:
    raise RuntimeError('Parent runtime changed')
source = sdk/'src/core/timer_queue.cpp'
relative = 'src/core/CMakeFiles/rexcore.dir/Release/timer_queue.cpp.obj'
_, fields = m.edge((build/'CMakeFiles/impl-Release.ninja').read_text(), relative+':')
args = []
for name in ('DEFINES', 'FLAGS', 'INCLUDES'):
    args += shlex.split(fields[name])
target = out/'timer_queue.cpp.obj'
args += ['-c', str(source), '-o', str(target)]
clang = Path('C:/Program Files/LLVM/bin/clang++.exe')
m.run_rsp(clang, args, 'compile-timer', out, build)
old = [e['path'] for e in manifest['inputs'] if e['path'].replace('\\','/').endswith(relative)]
if len(old) != 1:
    raise RuntimeError('Ambiguous timer object')
objects = [str(target) if e['path'] == old[0] else e['path'] for e in manifest['inputs']]
(out/'exports.objs').write_text('\n'.join(objects)+'\n')
subprocess.run(['cmake','-E','__create_def',str(out/'exports.def'),str(out/'exports.objs'),
                '--nm=C:/Program Files/LLVM/bin/llvm-nm.exe'],check=True,capture_output=True)
args = shlex.split((parent/'link-runtime.rsp').read_text())
replaced = 0
for i, arg in enumerate(args):
    normal = arg.replace('\\','/')
    if normal == old[0].replace('\\','/'):
        args[i] = str(target)
        replaced += 1
    elif normal.endswith(('/rexruntime.dll','/rexruntime.lib','/rexruntime.pdb','/exports.def')):
        args[i] = normal.replace(parent.as_posix(),out.as_posix())
if replaced != 1 or not any(decoder.as_posix() == a.replace('\\','/') for a in args):
    raise RuntimeError('Unexpected timer/audio link inputs')
m.run_rsp(clang, args, 'link-runtime', out, build)
(out/'build-manifest.json').write_text(json.dumps({
    'runtime_sha256': sha(out/'rexruntime.dll'),
    'parent_runtime_sha256': manifest['runtime_sha256'],
    'timer_source_sha256': sha(source),
    'inputs': [{'path':p,'sha256':sha(p)} for p in objects],
    'decoder_object_sha256': sha(decoder),
    'libraries': manifest['libraries'],
    'guest_frequency_policy': 'Unchanged: 50000000 Hz, fixed 60 Hz video mode',
},indent=2)+'\n')
print('Built isolated timer runtime:', out)
