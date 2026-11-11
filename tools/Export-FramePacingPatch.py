"""Maintainer tool: export reviewed local SDK edits relative to OutRun's base patches.
This does not modify SDK files. Get-Sdk.ps1 applies the exported patch on fresh clones.
"""
from pathlib import Path
import difflib,re,subprocess,tempfile
root=Path(__file__).resolve().parents[1]; sdk=root/'out/dependencies/rexglue-sdk'
frame=root/'patches/frame-pacing.patch'
base=[root/'patches/sdk-snapshot.patch',root/'patches/libmspack-real-sources.patch']
files=set(re.findall(r'^\+\+\+ b/(.+)$',frame.read_text(encoding='utf8'),re.M))
files.add('cmake/rexglue_install.cmake')
basefiles=set(files)
for patch in base:basefiles.update(re.findall(r'^\+\+\+ b/(.+)$',patch.read_text(encoding='utf8'),re.M))
with tempfile.TemporaryDirectory(dir=root/'out',prefix='sdk-baseline-') as temp:
    stage=Path(temp)
    for name in basefiles:
        r=subprocess.run(['git','-C',str(sdk),'show','HEAD:'+name],capture_output=True)
        if r.returncode==0:
            path=stage/name;path.parent.mkdir(parents=True,exist_ok=True)
            path.write_bytes(r.stdout.replace(b'\r\n',b'\n'))
    for patch in base:
        subprocess.run(['git','-C',str(stage),'apply',str(patch)],check=True)
        for path in stage.rglob('*'):
            if path.is_file():path.write_bytes(path.read_bytes().replace(b'\r\n',b'\n'))
    diff=[]
    for name in sorted(files):
        old=(stage/name).read_text(encoding='utf8') if (stage/name).exists() else ''
        new=(sdk/name).read_text(encoding='utf8')
        diff.extend(difflib.unified_diff(old.splitlines(True),new.splitlines(True),fromfile='a/'+name if old else '/dev/null',tofile='b/'+name))
    frame.write_bytes(''.join(diff).encode('utf8'))
print('Exported',len(files),'reviewed SDK paths')
