"""Apply layered SDK patches safely and idempotently, including overlapping hunks.
Validate a scratch reconstruction from pinned HEAD before changing the checkout.
Recognize only original, intermediate and final file contents; preserve unknown edits.
"""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('sdk',type=Path)
p.add_argument('--check-only',action='store_true')
a=p.parse_args(); sdk=a.sdk.resolve()
patches=[ROOT/'patches'/name for name in ['sdk-snapshot.patch','libmspack-real-sources.patch','frame-pacing.patch','ffmpeg-xmaframes-reset.patch','sdl-controller-polling.patch','d3d12-fence-result.patch','timer-queue-wait.patch','async-query-ordering.patch','async-query-fence-event.patch','query-pacing-diagnostics.patch','query-early-submission.patch','query-clock-diagnostics.patch','host-present-policy.patch','spatial-filters.patch','video-aspect.patch']]
files=set()
for patch in patches:
    for name in re.findall(r'^\+\+\+ b/(.+)$',patch.read_text(),re.M):
        if Path(name).is_absolute() or '..' in Path(name).parts: raise ValueError('Unsafe patch path')
        files.add(name)
known={name:[] for name in files}
with tempfile.TemporaryDirectory(prefix='sdk-patch-check-',dir=ROOT/'out') as temp:
    stage=Path(temp)
    for name in sorted(files):
        # FFmpeg is pinned as a git submodule; its files cannot be read with
        # `git show HEAD:path` against the parent SDK repository.
        prefix='thirdparty/FFmpeg/'
        repo=sdk/'thirdparty/FFmpeg' if name.startswith(prefix) else sdk
        relative=name[len(prefix):] if name.startswith(prefix) else name
        result=subprocess.run(['git','-C',str(repo),'show','HEAD:'+relative],capture_output=True)
        data=result.stdout.replace(b'\r\n',b'\n') if result.returncode==0 else None
        known[name].append(data)
        if data is not None:
            path=stage/name; path.parent.mkdir(parents=True,exist_ok=True); path.write_bytes(data)
    for patch in patches:
        result=subprocess.run(['git','-C',str(stage),'apply',str(patch)],capture_output=True,text=True)
        if result.returncode: raise RuntimeError(patch.name+': '+result.stderr)
        for name in files:
            path=stage/name
            normalized=path.read_bytes().replace(b'\r\n',b'\n') if path.exists() else None
            if normalized is not None: path.write_bytes(normalized)
            known[name].append(normalized)
    # Validate every file before the first write.
    for name,versions in known.items():
        path=sdk/name
        current=path.read_bytes().replace(b'\r\n',b'\n') if path.exists() else None
        if current not in versions: raise RuntimeError('Unrecognized SDK changes: '+name)
        if a.check_only and current!=versions[-1]: raise RuntimeError('SDK patch incomplete: '+name)
    if not a.check_only:
        for name,versions in known.items():
            path=sdk/name; final=versions[-1]
            current=path.read_bytes().replace(b'\r\n',b'\n') if path.exists() else None
            if current==final: continue
            if final is None: raise RuntimeError('Unexpected deletion in patch set')
            path.parent.mkdir(parents=True,exist_ok=True)
            path.write_bytes(final)
print('Verified SDK patch layers:',len(files),'files')
