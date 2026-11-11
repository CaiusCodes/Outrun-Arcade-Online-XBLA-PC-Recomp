"""Build the isolated SDL polling candidate, retaining the tested runtime cohort.

Recompile the SDL driver and its SDK factory together. App consumers containing
SDLInputDriver by value MUST also be rebuilt against the matching header.
Keeps the proven raw-XMA decoder replacement and all other frozen objects.
"""
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
baseline = ROOT/'out/frame-pacing/dual'
audio = ROOT/'out/audio-reset-runtime-20261019'
out = ROOT/'out/controller-polling-runtime-20261025'
out.mkdir(parents=True, exist_ok=True)
manifest = json.loads((baseline/'build-manifest.json').read_text())
for entry in manifest['inputs']:
    if hashlib.sha256(Path(entry['path']).read_bytes()).hexdigest() != entry['sha256']:
        raise RuntimeError('Frozen SDK object changed: '+entry['path'])
audio_manifest = json.loads((audio/'build-manifest.json').read_text())
for entry in audio_manifest['libraries']:
    if hashlib.sha256(Path(entry['path']).read_bytes()).hexdigest() != entry['sha256']:
        raise RuntimeError('Frozen library changed: '+entry['path'])
ninja = (build/'CMakeFiles/impl-Release.ninja').read_text()
replacements = {}
for source, obj in [
    ('src/input/sdl/sdl_input_driver.cpp', 'src/input/CMakeFiles/rexinput.dir/Release/sdl/sdl_input_driver.cpp.obj'),
    ('src/input/input_system.cpp', 'src/input/CMakeFiles/rexinput.dir/Release/input_system.cpp.obj'),
]:
    _, fields = m.edge(ninja, obj+':')
    args = []
    for name in ('DEFINES', 'FLAGS', 'INCLUDES'):
        args += shlex.split(fields[name])
    target = out/Path(obj).name
    args += ['-c', str(sdk/source), '-o', str(target)]
    m.run_rsp(Path('C:/Program Files/LLVM/bin/clang++.exe'), args, 'compile-'+Path(source).stem, out, build)
    matching = [entry['path'] for entry in manifest['inputs'] if entry['path'].replace('\\','/').endswith(obj)]
    if len(matching) != 1:
        raise RuntimeError('Ambiguous frozen object for '+source)
    replacements[matching[0].replace('\\','/')] = str(target).replace('\\','/')
objects = [replacements.get(e['path'].replace('\\','/'), e['path']) for e in manifest['inputs']]
(out/'exports.objs').write_text('\n'.join(objects)+'\n')
subprocess.run(['cmake','-E','__create_def',str(out/'exports.def'),str(out/'exports.objs'),
                '--nm=C:/Program Files/LLVM/bin/llvm-nm.exe'],check=True,capture_output=True)
args = shlex.split((audio/'link-runtime.rsp').read_text())
for i, arg in enumerate(args):
    normal = arg.replace('\\','/')
    if normal in replacements:
        args[i] = replacements[normal]
    elif normal.endswith(('/rexruntime.dll','/rexruntime.lib','/rexruntime.pdb','/exports.def')):
        args[i] = normal.replace(str(audio).replace('\\','/'),str(out).replace('\\','/'))
m.run_rsp(Path('C:/Program Files/LLVM/bin/clang++.exe'), args, 'link-runtime', out, build)
(out/'build-manifest.json').write_text(json.dumps({
    'runtime_sha256': hashlib.sha256((out/'rexruntime.dll').read_bytes()).hexdigest(),
    'audio_baseline_sha256': audio_manifest['runtime_sha256'],
    'inputs': [{'path':p,'sha256':hashlib.sha256(Path(p).read_bytes()).hexdigest()} for p in objects],
    'decoder_object_sha256': hashlib.sha256((audio/'wmaprodec.c.obj').read_bytes()).hexdigest(),
    'libraries': audio_manifest['libraries'],
},indent=2))
print('Built isolated controller polling runtime:', out)
