"""Build a project-local D3D12 runtime overlay using existing SDK Release objects.

Reads the project-local SDK only. Compiles the patched source and links all output in
out/renderer-overlay. Does not run SDK Ninja, install files, or edit the SDK.
This is an experimental development build, not part of release packaging yet.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]

def edge(text, needle):
    matches = [m for m in re.finditer(r'^build (.+)\n((?:  .*\n)*)', text, re.M) if needle in m[1]]
    if len(matches) != 1:
        raise ValueError(f'Expected one SDK build edge for {needle}, got {len(matches)}')
    match = matches[0]
    fields = dict(re.findall(r'^  (\w+) = (.*)$', match[2], re.M))
    return match[1], fields

def run_rsp(clang, arguments, name, out, cwd):
    response = out/(name+'.rsp')
    # clang's GNU-style response parser treats backslashes as escapes.
    arguments = [arg.replace('\\', '/') for arg in arguments]
    response.write_text(subprocess.list2cmdline(arguments), encoding='utf-8')
    result = subprocess.run([str(clang), '@'+str(response)], cwd=cwd, capture_output=True, text=True)
    (out/(name+'.log')).write_text(result.stdout+result.stderr, encoding='utf-8')
    if result.returncode:
        raise RuntimeError(f'{name} failed ({result.returncode}): '+result.stderr[-4000:])

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, default=ROOT/'out/dependencies/rexglue-sdk')
    parser.add_argument('--sdk-install', type=Path, default=ROOT/'out/sdk-install')
    parser.add_argument('--output', type=Path, default=ROOT/'out/renderer-overlay')
    parser.add_argument('--reuse-tested-renderer', action='store_true',
                        help='Reuse hash-pinned September 15 GPU overlay with matching stock runtime')
    parser.add_argument('--profile-race', action='store_true',
                        help='Build reviewed September 17 GPU cohort for race profiling; batching defaults off')
    parser.add_argument('--async-queries', action='store_true',
                        help='Include opt-in asynchronous real occlusion-result publication')
    parser.add_argument('--bulk-registers', action='store_true',
                        help='Include opt-in Type-0 sequential register range dispatch')
    args = parser.parse_args()
    if args.profile_race and args.reuse_tested_renderer:
        parser.error('Choose either profiling build or tested binary reuse')
    if args.async_queries and not args.profile_race:
        parser.error('--async-queries requires --profile-race')
    if args.bulk_registers and not args.profile_race:
        parser.error('--bulk-registers requires --profile-race')
    sdk, out = args.sdk.resolve(), args.output.resolve()
    sdk_install = args.sdk_install.resolve()
    if not out.is_relative_to(ROOT/'out'):
        raise ValueError('Overlay output must stay under project out/')
    out.mkdir(parents=True, exist_ok=True)
    if args.reuse_tested_renderer:
        baseline = ROOT/'out/native-menu-test-20260915'
        pinned = [
            ('rexruntime.dll', sdk_install/'bin/rexruntime.dll',
             '9508424882f3eb205d62925a70067be1b5c1be27f6476be89ae266d45e8279c7'),
            ('rexgpu-xenos.dll', baseline/'rexgpu-xenos.dll',
             'ebc942b45dba12fd55b028682c84030b605855c2403a2c966ae45b53ae7a0f49')]
        verified = []
        for name, source, expected in pinned:
            data = source.read_bytes()
            if hashlib.sha256(data).hexdigest() != expected:
                raise ValueError(f'Tested renderer changed: {source}')
            verified.append((name, data))
        for name, data in verified:
            (out/name).write_bytes(data)
        manifest = {'mode': 'reuse-tested-renderer', 'host_presentation': 'stock SDK runtime installed September 16; no host VSync patch',
                    'inputs': [{'path': str(source), 'sha256': expected} for _, source, expected in pinned]}
        (out/'build-manifest.json').write_text(json.dumps(manifest, indent=2))
        print('Verified and staged stock runtime and tested live-resolution GPU renderer')
        return
    build = sdk/'out/build/win-amd64'
    ninja = (build/'CMakeFiles/impl-Release.ninja').read_text()
    # Host VSync was removed after user-observed menu/race slowdown.
    # Ship the exact stock runtime; only live resolution needs a GPU overlay.
    stock_runtime = sdk_install/'bin/rexruntime.dll'
    if not stock_runtime.is_file():
        raise ValueError(f'Missing stock runtime {stock_runtime}')
    runtime_bytes = stock_runtime.read_bytes()
    inputs = [{'path': str(stock_runtime), 'sha256': hashlib.sha256(runtime_bytes).hexdigest()}]
    result = {'inputs': inputs, 'host_presentation': 'stock SDK (VSync option removed)',
              'runtime_sha256': hashlib.sha256(runtime_bytes).hexdigest()}
    clang = Path('C:/Program Files/LLVM/bin/clang++.exe')

    gpu_path = sdk/'src/graphics/d3d12/command_processor.cpp'
    gpu_original = gpu_path.read_bytes()
    gpu_hash = 'c29109415cd2d65ad3f2025d19ca76c4ca5bebd76e74c55d5d8c1e929ddff66b'
    if args.profile_race:
        gpu_hash = '8afea946b8ce35ceabd38033b0ec40a6bc75fb720cb5c85433cce86ad072bbd4'
        header = sdk/'include/rex/graphics/d3d12/command_processor.h'
        if hashlib.sha256(header.read_bytes()).hexdigest() != 'b91059bebb8128867471526e3f9bb570681eab8924655c81af88773e8631a737':
            raise ValueError('Reviewed profiling GPU header changed')
        inputs.append({'path':str(header), 'sha256':hashlib.sha256(header.read_bytes()).hexdigest()})
    if hashlib.sha256(gpu_original).hexdigest() != gpu_hash:
        raise ValueError('SDK GPU processor source changed; re-review live-resolution patch')
    gpu_source = gpu_original.decode().replace('\r\n','\n')
    gpu_source = '#include <stdexcept>\n#include <map>\n#include <mutex>\n#include <optional>\n'+gpu_source
    if args.profile_race:
        # Preserve the old per-query behavior until an A/B test justifies enabling batching.
        old = 'REXCVAR_DEFINE_BOOL(occlusion_resolve_per_frame, true,'
        if gpu_source.count(old) != 1: raise ValueError('Profiling toggle anchor changed')
        gpu_source = gpu_source.replace(old, 'REXCVAR_DEFINE_BOOL(occlusion_resolve_per_frame, false,')
    marker = 'namespace rex::graphics::d3d12 {'
    if gpu_source.count(marker) != 1: raise ValueError('GPU namespace anchor changed')
    gpu_source = gpu_source.replace(marker,
        'REXCVAR_DEFINE_BOOL(outrun_live_resolution, false, "OutRun",\n'
        '    "Experimental GPU resource recreation on resolution change");\n\n'+marker)
    gpu_source = gpu_source.replace(marker, marker+'\n'+(ROOT/'src/renderer_shader_storage.inl').read_text())
    old = '  pipeline_cache_->InitializeShaderStorage(cache_root, title_id, blocking);'
    if gpu_source.count(old) != 1: raise ValueError('Shader storage setup anchor changed')
    gpu_source = gpu_source.replace(old, '  OutrunRememberShaderStorage(this, cache_root, title_id);\n'+old)
    old = 'D3D12CommandProcessor::~D3D12CommandProcessor() = default;'
    if gpu_source.count(old) != 1: raise ValueError('GPU destructor anchor changed')
    gpu_source = gpu_source.replace(old, 'D3D12CommandProcessor::~D3D12CommandProcessor() { OutrunForgetShaderStorage(this); }')
    if args.async_queries:
        gpu_source = '#include "renderer_work_queue.h"\n'+gpu_source
        gpu_source = gpu_source.replace(marker,
            'REXCVAR_DEFINE_BOOL(outrun_async_queries, false, "OutRun",\n'
            '    "Experimental nonblocking publication of real GPU query results")\n'
            '    .lifecycle(rex::cvar::Lifecycle::kInitOnly);\n\n'+marker+'\n'+
            (ROOT/'src/renderer_async_queries.inl').read_text())
        # Only the reviewed batched-resolve function is modified. Original
        # per-query behavior remains available for a measured A/B comparison.
        start = gpu_source.index('void D3D12CommandProcessor::ResolveOcclusionSnapshots()')
        end = gpu_source.index('void D3D12CommandProcessor::FlushOcclusionFrameQueries()',start)
        resolve = gpu_source[start:end]
        anchor = '        const auto wait_start = std::chrono::steady_clock::now();'
        if resolve.count(anchor) != 1: raise ValueError('Async resolve anchor changed')
        resolve = resolve.replace(anchor, (ROOT/'src/renderer_async_query_submit.inl').read_text()+'\n'+anchor)
        gpu_source = gpu_source[:start]+resolve+gpu_source[end:]
        anchor = 'if (REXCVAR_GET(occlusion_resolve_per_frame)) {'
        if gpu_source.count(anchor) != 1: raise ValueError('Query mode anchor changed')
        gpu_source = gpu_source.replace(anchor, 'if (REXCVAR_GET(occlusion_resolve_per_frame) || REXCVAR_GET(outrun_async_queries)) {')
        gpu_source = gpu_source.replace(
            'if (REXCVAR_GET(occlusion_resolve_per_frame) && occlusion_query_resources_available_)',
            'if ((REXCVAR_GET(occlusion_resolve_per_frame) || REXCVAR_GET(outrun_async_queries)) && occlusion_query_resources_available_)')
        anchor = '      WriteGuestOcclusionResult(sample_counts, occlusion_running_total_);'
        if gpu_source.count(anchor) != 1: raise ValueError('Opening snapshot anchor changed')
        gpu_source = gpu_source.replace(anchor,
            '      WriteGuestOcclusionResult(sample_counts, REXCVAR_GET(outrun_async_queries)\n'
            '          ? OutrunAsyncQueries(this)->published_total.load(std::memory_order_acquire)\n'
            '          : occlusion_running_total_);')
        for anchor, replacement in [
            ('void D3D12CommandProcessor::ShutdownOcclusionQueryResources() {',
             'void D3D12CommandProcessor::ShutdownOcclusionQueryResources() {\n  OutrunDrainAsyncQueries(this, true);'),
            ('  if (occlusion_query_cursor_ >= kMaxOcclusionQueries) {',
             '  if (occlusion_query_cursor_ >= kMaxOcclusionQueries) {\n    OutrunDrainAsyncQueries(this, false);')]:
            if gpu_source.count(anchor) != 1: raise ValueError('Async resource lifetime anchor changed')
            gpu_source = gpu_source.replace(anchor, replacement)
        # Inclusive CPU timings locate work beyond the query wait. These are
        # not GPU timestamps and nested timings must not be added together.
        gpu_source = gpu_source.replace('  double resolve_ms = 0.0;',
            '  double draw_ms = 0.0, submit_ms = 0.0, fence_ms = 0.0;\n  double resolve_ms = 0.0;')
        gpu_source = gpu_source.replace('GpuPerfStats g_gpu_perf;', '''GpuPerfStats g_gpu_perf;
struct OutrunCpuTimer {
  double& value;
  bool enabled = REXCVAR_GET(gpu_perf_stats);
  std::chrono::steady_clock::time_point start = enabled ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
  ~OutrunCpuTimer() { if (enabled) value += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-start).count(); }
};''')
        for anchor, field in [
            ('  ++g_gpu_perf.draws;', 'draw_ms'),
            ('bool D3D12CommandProcessor::EndSubmission(bool is_swap) {', 'submit_ms'),
            ('void D3D12CommandProcessor::CheckSubmissionFence(uint64_t await_submission) {', 'fence_ms')]:
            if gpu_source.count(anchor) != 1: raise ValueError('CPU timing anchor changed')
            gpu_source = gpu_source.replace(anchor, anchor+'\n  OutrunCpuTimer outrun_cpu_timer{g_gpu_perf.'+field+'};')
        anchor = '      st = GpuPerfStats{};'
        if gpu_source.count(anchor) != 1: raise ValueError('Timing report anchor changed')
        gpu_source = gpu_source.replace(anchor,
            '      REXGPU_INFO("[outrun_timing] draw {:.2f} ms, submit {:.2f} ms, fence {:.2f} ms (inclusive CPU/frame); async {}",\n'
            '          st.draw_ms/f, st.submit_ms/f, st.fence_ms/f, REXCVAR_GET(outrun_async_queries));\n'+anchor)
    marker = '  EndSubmission(true);\n}\n\nvoid D3D12CommandProcessor::OnPrimaryBufferEnd()'
    if gpu_source.count(marker) != 1: raise ValueError('GPU frame boundary anchor changed')
    snippet = (ROOT/'src/renderer_live_resolution.inl').read_text()
    gpu_source = gpu_source.replace(marker, '  EndSubmission(true);\n'+snippet+
        '\n}\n\nvoid D3D12CommandProcessor::OnPrimaryBufferEnd()')
    gpu_patched = out/'command_processor.cpp'
    gpu_patched.write_text(gpu_source, encoding='utf-8')
    gpu_obj_relative = 'src/graphics/CMakeFiles/rexgpu-xenos.dir/Release/d3d12/command_processor.cpp.obj'
    _, gpu_compile = edge(ninja,gpu_obj_relative+':')
    gpu_compile_args = []
    for key in ('DEFINES','INCLUDES','FLAGS'): gpu_compile_args += shlex.split(gpu_compile[key])
    if args.profile_race:
        # Keep optimized diagnostic code symbolizable; PDB stays outside payloads.
        gpu_compile_args += ['-g', '-gcodeview']
    gpu_compile_args += ['-I'+str(gpu_path.parent)]
    gpu_compile_args += ['-I'+str(ROOT/'src')]
    gpu_obj = out/'command_processor.cpp.obj'
    run_rsp(clang,gpu_compile_args+['-c',str(gpu_patched),'-o',str(gpu_obj)],'compile-gpu',out,build)
    replacement_objects = {gpu_obj_relative: gpu_obj}
    if args.bulk_registers:
        source_path = sdk/'src/graphics/command_processor.cpp'
        source_bytes = source_path.read_bytes()
        source_hash = hashlib.sha256(source_bytes).hexdigest()
        if source_hash != '060b68a6a2501783b3268e01728e7282562e6b5149e7a3b1dab80b20aff39fb3':
            raise ValueError('Reviewed base command processor changed')
        base_source = source_bytes.decode().replace('\r\n','\n')
        begin = base_source.index('bool CommandProcessor::ExecutePacketType0(')
        end = base_source.index('bool CommandProcessor::ExecutePacketType1(', begin)
        function = base_source[begin:end]
        first = function.index('  for (uint32_t m = 0; m < count; m++) {')
        last = function.index('\n\n  trace_writer_.WritePacketEnd();', first)
        recipe = ROOT/'src/renderer_type0_registers.inl'
        function = function[:first]+recipe.read_text().rstrip()+function[last:]
        base_source = base_source[:begin]+function+base_source[end:]
        marker = 'namespace rex::graphics {'
        if base_source.count(marker) != 1: raise ValueError('Base namespace anchor changed')
        base_source = base_source.replace(marker,
            'REXCVAR_DEFINE_BOOL(outrun_bulk_registers, false, "OutRun",\n'
            '    "Experimental sequential Type-0 register range dispatch");\n'+marker)
        base_patched = out/'base_command_processor.cpp'
        base_patched.write_text(base_source, encoding='utf-8')
        relative = 'src/graphics/CMakeFiles/rexgpu-xenos.dir/Release/command_processor.cpp.obj'
        _, settings = edge(ninja, relative+':')
        compile_args = []
        for key in ('DEFINES','INCLUDES','FLAGS'): compile_args += shlex.split(settings[key])
        base_obj = out/'base_command_processor.cpp.obj'
        run_rsp(clang, compile_args+['-g','-gcodeview','-c',str(base_patched),'-o',str(base_obj)],
                'compile-base',out,build)
        replacement_objects[relative] = base_obj
        inputs.append({'path':str(source_path), 'sha256':source_hash})
        result['bulk_register_recipe_sha256'] = hashlib.sha256(recipe.read_bytes()).hexdigest()
    gpu_edge, gpu_link = edge(ninja, ': CXX_SHARED_LIBRARY_LINKER__rexgpu-xenos_Release ')
    gpu_explicit = gpu_edge.split(': CXX_SHARED_LIBRARY_LINKER__rexgpu-xenos_Release ',1)[1].split(' |',1)[0]
    gpu_objects = []
    for name in gpu_explicit.split():
        path = (build/name.replace('$:',':')).resolve()
        if name in replacement_objects: gpu_objects.append(str(replacement_objects[name]))
        else:
            if not path.is_file(): raise ValueError(f'Missing SDK GPU object {path}')
            gpu_objects.append(str(path))
            inputs.append({'path':str(path),'sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
    gpu_args = ['-nostartfiles','-nostdlib']
    for key in ('LANGUAGE_COMPILE_FLAGS','LINK_FLAGS'):
        gpu_args += shlex.split(gpu_link[key].replace('\\','/'))
    if args.profile_race:
        gpu_args += ['-Xlinker', '/DEBUG', '-Xlinker', '/OPT:REF', '-Xlinker', '/OPT:ICF']
    gpu_args += ['-o',str(out/'rexgpu-xenos.dll'),'-Xlinker','/MANIFEST:EMBED',
                 '-Xlinker','/implib:'+str(out/'rexgpu-xenos.lib'),
                 '-Xlinker','/pdb:'+str(out/'rexgpu-xenos.pdb'),'-Xlinker','/version:0.0']
    gpu_libraries = shlex.split(gpu_link['LINK_LIBRARIES'].replace('\\','/'))
    for library in gpu_libraries:
        if library.startswith('-'): continue
        path = Path(library)
        if not path.is_absolute(): path = build/path
        inputs.append({'path':str(path), 'sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
    run_rsp(clang,gpu_args+gpu_objects+gpu_libraries,'link-gpu',out,build)
    result.update({'gpu_source_sha256':gpu_hash,
        'mode':'profile-race' if args.profile_race else 'live-resolution',
        'async_queries_included':args.async_queries,
        'bulk_registers_included':args.bulk_registers,
        'shader_storage_recipe_sha256':hashlib.sha256((ROOT/'src/renderer_shader_storage.inl').read_bytes()).hexdigest(),
        'gpu_overlay_sha256':hashlib.sha256((out/'rexgpu-xenos.dll').read_bytes()).hexdigest(),
        'live_resolution_recipe_sha256':hashlib.sha256((ROOT/'src/renderer_live_resolution.inl').read_bytes()).hexdigest()})
    if args.async_queries:
        result['async_recipe_sha256'] = {
            name: hashlib.sha256((ROOT/'src'/name).read_bytes()).hexdigest()
            for name in ('renderer_work_queue.h', 'renderer_async_queries.inl',
                         'renderer_async_query_submit.inl')}
    (out/'build-manifest.json').write_text(json.dumps(result,indent=2))
    (out/'rexruntime.dll').write_bytes(runtime_bytes)
    print('Built experimental live-resolution GPU overlay with unchanged SDK runtime: '+str(out))

if __name__ == '__main__': main()
