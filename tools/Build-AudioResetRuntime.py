"""Private, isolated relink: replace only FFmpeg's raw-XMA decoder object.

Retain the tested hybrid runtime's exact object cohort and libraries. Never
overwrite the baseline DLL, SDK sources, or a user installation.
"""
import hashlib
import importlib.util
import json
from pathlib import Path
import shlex
import shutil

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('renderer', ROOT/'tools/Build-RendererOverlay.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
sdk = ROOT/'out/dependencies/rexglue-sdk'
build = sdk/'out/build/win-amd64'
baseline = ROOT/'out/frame-pacing/dual'
out = ROOT/'out/audio-reset-runtime-20261019'
out.mkdir(parents=True, exist_ok=True)
manifest = json.loads((baseline/'build-manifest.json').read_text())
for entry in manifest['inputs']:
    if hashlib.sha256(Path(entry['path']).read_bytes()).hexdigest() != entry['sha256']:
        raise RuntimeError('Frozen SDK object changed: '+entry['path'])
source = sdk/'thirdparty/FFmpeg/libavcodec/wmaprodec.c'
original = source.read_bytes().replace(b'\r\n', b'\n')
needle = b'    .decode         = xmaframes_decode_packet,\n'
addition = b'    .flush          = wmapro_flush,\n'
if original.count(needle) != 1:
    raise RuntimeError('Unrecognized raw-XMA decoder registration')
patched = original if needle+addition in original else original.replace(needle, needle+addition)
(out/'wmaprodec.c').write_bytes(patched)
_, fields = m.edge((build/'CMakeFiles/impl-Release.ninja').read_text(),
                  'thirdparty/CMakeFiles/libavcodec.dir/Release/FFmpeg/libavcodec/wmaprodec.c.obj:')
args = []
for name in ('DEFINES', 'FLAGS', 'INCLUDES'):
    args += shlex.split(fields[name])
args += ['-I'+str(source.parent), '-c', str(out/'wmaprodec.c'), '-o', str(out/'wmaprodec.c.obj')]
m.run_rsp(Path('C:/Program Files/LLVM/bin/clang.exe'), args, 'compile-decoder', out, build)
shutil.copyfile(baseline/'exports.def', out/'exports.def')
args = shlex.split((baseline/'link-runtime.rsp').read_text())
args = [arg.replace(str(baseline).replace('\\','/'), str(out).replace('\\','/')) for arg in args]
# Supply the replacement object explicitly, so the archive's old decoder object
# is not extracted. Other FFmpeg codecs and all runtime objects remain original.
args.insert(0, str(out/'wmaprodec.c.obj'))
m.run_rsp(Path('C:/Program Files/LLVM/bin/clang++.exe'), args, 'link-runtime', out, build)
libraries = [Path(arg) for arg in args if arg.endswith('.lib') and Path(arg).is_file()]
(out/'build-manifest.json').write_text(json.dumps({
    'baseline_runtime_sha256': hashlib.sha256((baseline/'rexruntime.dll').read_bytes()).hexdigest(),
    'runtime_sha256': hashlib.sha256((out/'rexruntime.dll').read_bytes()).hexdigest(),
    'decoder_source_sha256': hashlib.sha256(patched).hexdigest(),
    'preserved_objects': manifest['inputs'],
    'libraries': [{'path':str(p), 'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in libraries]
}, indent=2))
print('Built isolated raw-XMA reset runtime:', out)
