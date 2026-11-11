"""Rebuild presenter-header consumers together, retain unrelated frozen objects."""
from pathlib import Path
import concurrent.futures, hashlib, importlib.util, json, re, shlex, subprocess
ROOT=Path(__file__).resolve().parents[1]
sdk=ROOT/'out/dependencies/rexglue-sdk'; build=sdk/'out/build/win-amd64'
out=ROOT/'out/spatial-filters-20261028'; out.mkdir(exist_ok=True)
spec=importlib.util.spec_from_file_location('renderer',ROOT/'tools/Build-RendererOverlay.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
clang=Path('C:/Program Files/LLVM/bin/clang++.exe')
ninja=(build/'CMakeFiles/impl-Release.ninja').read_text()
# Conservative transitive include closure, including both sides of #if.
reverse={}
for p in list((sdk/'src').rglob('*.cpp'))+list((sdk/'src').rglob('*.h'))+list((sdk/'include').rglob('*.h')):
    for inc in re.findall(r'^\s*#\s*include\s*[<"]([^>"]+)',p.read_text(errors='replace'),re.M):
        for base in (p.parent,sdk/'include',sdk/'src'):
            q=(base/inc).resolve()
            if q.is_file(): reverse.setdefault(q,set()).add(p.resolve());break
seen={(sdk/'include/rex/ui/presenter.h').resolve()};todo=list(seen)
while todo:
    for p in reverse.get(todo.pop(),set()):
        if p not in seen:seen.add(p);todo.append(p)
edges={}
for match in re.finditer(r'^build (.+?): CXX_COMPILER\S+ (.+?)(?: \|\|.*)?\n',ninja,re.M):
    source=Path(match[2].replace('$:',':').replace('$ ',' '))
    if source.resolve() in seen:edges[match[1]]=source
runtime_parent=ROOT/'out/host-present-runtime-20261028'
gpu_parent=ROOT/'out/query-clock-gpu-20261027'
rm=json.loads((runtime_parent/'build-manifest.json').read_text())
gm=json.loads((gpu_parent/'build-manifest.json').read_text())
for e in rm['inputs']+rm['libraries']+gm['original_inputs']:
    if sha(e['path'])!=e['sha256']:raise RuntimeError('Frozen input changed: '+e['path'])
if sha(runtime_parent/'rexruntime.dll')!=rm['runtime_sha256']:raise RuntimeError('Runtime changed')
if sha(gpu_parent/'rexgpu-xenos.dll')!=gm['gpu_sha256']:raise RuntimeError('GPU changed')
def compile_one(item):
    key,source=item;_,fields=m.edge(ninja,key+':')
    name=key.replace('/','_').replace('.obj','')
    target=out/(name+'.obj');args=[]
    for f in ('DEFINES','FLAGS','INCLUDES'):args+=shlex.split(fields[f])
    args+=['-DREX_HAS_FIDELITYFX_SDK=1','-c',str(source),'-o',str(target)]
    m.run_rsp(clang,args,name,out,build)
    return key,dict(path=str(target),sha256=sha(target),source=str(source),source_sha256=sha(source))
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
    compiled=dict(pool.map(compile_one,edges.items()))
(out/'compiled.json').write_text(json.dumps(compiled,indent=2))
special={
 str(runtime_parent/'d3d12_presenter.cpp.obj'):'src/ui/CMakeFiles/rexui.dir/Release/d3d12/d3d12_presenter.cpp.obj',
 str(gpu_parent/'d3d12-command-processor.obj'):'src/graphics/CMakeFiles/rexgpu-xenos.dir/Release/d3d12/command_processor.cpp.obj',
 str(gpu_parent/'base-command-processor.obj'):'src/graphics/CMakeFiles/rexgpu-xenos.dir/Release/command_processor.cpp.obj',
}
# Earlier patched input objects may have custom names. Map by source suffix.
for e in rm['inputs']:
    p=e['path'].replace('\\','/')
    if '/CMakeFiles/' not in p:
        matches=[k for k in compiled if k.endswith('/'+Path(p).name)]
        if len(matches)==1:special[str(Path(p))]=matches[0]
def replacement(arg):
    norm=arg.replace('\\','/')
    for old,key in special.items():
        if norm==old.replace('\\','/') and key in compiled:return compiled[key]['path']
    for key,e in compiled.items():
        if norm.endswith('/'+key):return e['path']
    return arg
for parent,dll in [(runtime_parent,'rexruntime'),(gpu_parent,'rexgpu-xenos')]:
    rsp=parent/('link-runtime.rsp' if dll=='rexruntime' else 'link-gpu.rsp')
    args=[replacement(a) for a in shlex.split(rsp.read_text())]
    if dll=='rexruntime':
        objs=[replacement(e['path']) for e in rm['inputs']]
        (out/'exports.objs').write_text('\n'.join(objs)+'\n')
        subprocess.run(['cmake','-E','__create_def',str(out/'exports.def'),str(out/'exports.objs'),'--nm=C:/Program Files/LLVM/bin/llvm-nm.exe'],check=True,capture_output=True)
    for i,a in enumerate(args):
        norm=a.replace('\\','/')
        if norm.endswith(('/'+dll+'.dll','/'+dll+'.lib','/'+dll+'.pdb','/exports.def')):
            args[i]=norm.replace(parent.as_posix(),out.as_posix())
    m.run_rsp(clang,args,'link-'+dll,out,build)
(out/'build-manifest.json').write_text(json.dumps({'compiled':compiled,'runtime_sha256':sha(out/'rexruntime.dll'),'gpu_sha256':sha(out/'rexgpu-xenos.dll'),'parent_runtime_sha256':rm['runtime_sha256'],'parent_gpu_sha256':gm['gpu_sha256'],'decoder_object_sha256':rm['decoder_object_sha256']},indent=2))
print('Built spatial runtime/GPU with',len(compiled),'coherent consumers:',out)
